#!/usr/bin/env python3
"""Find personal details in the sources and in the built executables.

Searches for this machine's user name, home directory name and the absolute
path of the working tree (derived at run time, never written down), consumer
e-mail addresses, foreign home directories, and anything listed in the
untracked file Tools/personal-needles.txt. The executables are scanned too,
narrow and UTF-16, because the linker embeds paths no source file contains.

  Usage:    python Tools/check_no_personal.py
  Returns:  0 when clean, 1 when something was found.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SKIP_DIRS = {
    ".git", ".vs", ".rfc-cache", "__pycache__", "Vendor",
    "x64", "Win32", "ARM64", "Debug", "Release", "obj", "bin", "out", "build",
}

TEXT_EXT = {
    ".c", ".cpp", ".h", ".hpp", ".inc", ".rc", ".py", ".ps1", ".bat", ".cmd",
    ".md", ".txt", ".json", ".csv", ".xml", ".yml", ".yaml",
    ".vcxproj", ".filters", ".sln", ".props", ".targets", ".gitignore",
}

GENERIC_LOGINS = {
    "user", "users", "admin", "administrator", "default", "public", "guest",
    "owner", "dev", "build", "runner", "vsts", "test", "testuser", "ci",
}

CONSUMER_MAIL = re.compile(
    r"[A-Za-z0-9._%+-]+@"
    r"(?:gmail|googlemail|outlook|hotmail|live|msn|yahoo|ymail|aol|gmx|web|"
    r"freenet|t-online|protonmail|proton|icloud|posteo|zoho|yandex)\."
    r"(?:com|de|net|org|me|ru)\b",
    re.IGNORECASE)

HOME_DIR = re.compile(
    r"(?:[A-Za-z]:[\\/]{1,2}Users|/home|/Users)[\\/]{1,2}([^\\/\s\"<>|*?]+)",
    re.IGNORECASE)

PLACEHOLDER_NAMES = {
    "testuser",
    "alex.doe",
    "alex",
    "jörg",
    "joerg",
    "name", "username", "benutzer", "user", "you",
    "%username%", "$user", "$env:username", "<name>", "<user>", "...",
}

def machine_needles():
    """What this machine reveals about its user."""
    out = []
    for value in (os.environ.get("USERNAME"), os.environ.get("USER"),
                  os.path.basename(os.environ.get("USERPROFILE", "")),
                  os.path.basename(os.path.expanduser("~"))):
        v = (value or "").strip()
        if len(v) >= 3 and v.lower() not in GENERIC_LOGINS:
            out.append(v)

    out.append(ROOT)
    out.append(ROOT.replace("\\", "/"))

    private = os.path.join(HERE, "personal-needles.txt")
    if os.path.exists(private):
        with open(private, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    out.append(line)

    seen, uniq = set(), []
    for n in sorted(out, key=len, reverse=True):
        if n.lower() not in seen:
            seen.add(n.lower())
            uniq.append(n)
    return uniq

def scan_text(needles):
    findings = []
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if os.path.splitext(name)[1].lower() not in TEXT_EXT:
                continue
            path = os.path.join(base, name)
            rel = os.path.relpath(path, ROOT)
            try:
                with open(path, encoding="utf-8-sig", errors="replace") as f:
                    lines = f.read().splitlines()
            except OSError:
                continue

            for n, line in enumerate(lines, 1):
                low = line.lower()
                hit = next((x for x in needles if x.lower() in low), None)
                if hit:
                    findings.append((rel, n, "detail of this machine", line))
                    continue

                m = CONSUMER_MAIL.search(line)
                if m:
                    findings.append((rel, n, "consumer e-mail address",
                                     m.group(0)))
                    continue

                for m in HOME_DIR.finditer(line):
                    who = m.group(1).strip().strip("'").lower()
                    if who not in PLACEHOLDER_NAMES:
                        findings.append((rel, n, "foreign home directory",
                                         m.group(0)))
    return findings

def scan_binary(path, needles):
    """Search an executable for the same strings, narrow and as UTF-16."""
    try:
        with open(path, "rb") as f:
            data = f.read().lower()
    except OSError:
        return None

    hits = []
    for needle in needles:
        low = needle.lower()
        try:
            narrow = low.encode("utf-8")
            wide = low.encode("utf-16-le")
        except UnicodeError:
            continue
        if narrow in data:
            hits.append((needle, "narrow"))
        elif wide in data:
            hits.append((needle, "UTF-16"))
    return hits

def main():
    needles = machine_needles()
    print("Checking for personal details "
          "(%d derived string(s))\n" % len(needles))

    findings = scan_text(needles)
    if findings:
        print("  SOURCES -- %d finding(s):\n" % len(findings))
        for rel, n, why, what in findings:
            print("    %-44s %5d  %-26s %s"
                  % (rel, n, why, what.strip()[:60]))
        print("")
    else:
        print("  SOURCES     clean")

    binary_bad = False
    for config in ("Release", "Debug"):
        rel = "x64\\%s\\Vault.exe" % config
        exe = os.path.join(ROOT, "x64", config, "Vault.exe")
        if not os.path.exists(exe):
            print("  BUILD %-8s %s missing -- not checked" % (config, rel))
            continue
        hits = scan_binary(exe, needles)
        if hits:
            binary_bad = True
            print("\n  BUILD %s -- %d finding(s) in %s:\n" % (config, len(hits), rel))
            for needle, how in hits:
                print("    %-8s %s" % (how, needle))
            print("\n    Not in any source file means it comes from the build:")
            print("    /PDBALTPATH:%_PDB% keeps the .pdb path out,")
            print("    /d1trimfile the path in __FILE__ (assert).")
        else:
            print("  BUILD %-8s clean" % config)

    images = []
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if os.path.splitext(name)[1].lower() in (".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp"):
                images.append(os.path.relpath(os.path.join(base, name), ROOT))
    if images:
        print("  IMAGES      %d file(s) -- cannot be checked, review by eye:" % len(images))
        for rel in sorted(images):
            print("              %s" % rel)

    if findings or binary_bad:
        return 1
    if images:
        print("\n  Nothing found in text or build. The images above are not checked.")
    else:
        print("\n  Nothing found.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
