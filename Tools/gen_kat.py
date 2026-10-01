#!/usr/bin/env python3
"""Generate Source/Generated/CryptoKat.h: known-answer vectors parsed out of
RFC 7693 (BLAKE2b), RFC 8439 (ChaCha20-Poly1305) and draft-irtf-cfrg-xchacha-03
(XChaCha20-Poly1305), never retyped by hand.

Needs network access. Run:  python Tools/gen_kat.py
"""
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "Manifold-Vault", "Source", "Generated")

SOURCES = {
    "rfc7693": "https://www.rfc-editor.org/rfc/rfc7693.txt",
    "rfc8439": "https://www.rfc-editor.org/rfc/rfc8439.txt",
    "xchacha": "https://www.ietf.org/archive/id/draft-irtf-cfrg-xchacha-03.txt",
}

def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read().decode("utf-8", "replace").replace("\r\n", "\n")

def strip_pagination(text):
    keep = []
    for ln in text.split("\n"):
        if ln.startswith("\f") or re.match(r"^.*\[Page \d+\]\s*$", ln):
            continue
        if re.match(r"^(RFC \d+|Internet-Draft)\s+.*\d{4}\s*$", ln):
            continue
        keep.append(ln)
    return "\n".join(keep)

def section(text, start_pat, end_pat):
    m = re.search(start_pat, text, re.M)
    if not m:
        sys.exit("section not found: %s" % start_pat)
    rest = text[m.end():]
    e = re.search(end_pat, rest, re.M)
    return rest[: e.start()] if e else rest

def dump_hex(block):
    """Parse 'NNN  aa bb cc ...  ascii' offset dump lines."""
    out = []
    for ln in block.split("\n"):
        m = re.match(r"^\s*\d{3}\s+((?:[0-9a-fA-F]{2} )*[0-9a-fA-F]{2})(?:\s\s|\s*$)", ln)
        if m:
            out.append(m.group(1).replace(" ", ""))
    return "".join(out).lower()

def run_hex(block):
    """Parse blocks of continuous hex lines (the draft's style)."""
    out = []
    for ln in block.split("\n"):
        s = ln.strip()
        if re.fullmatch(r"[0-9a-fA-F]{8,}", s):
            out.append(s)
    return "".join(out).lower()

def colon_hex(line):
    return line.replace(":", "").strip().lower()

def main():
    txt = {k: strip_pagination(fetch(u)) for k, u in SOURCES.items()}
    v = {}

    m = re.search(r'BLAKE2b-512\("abc"\)\s*=\s*(.*)$', txt["rfc7693"], re.M)
    if not m:
        sys.exit("BLAKE2b vector not found in RFC 7693")
    lines = [m.group(1)]
    tail = txt["rfc7693"][m.end():].split("\n")[1:]
    for ln in tail:
        s = ln.strip()
        if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}\s*)+", s):
            break
        lines.append(s)
    v["blake2b_abc"] = "".join(re.sub(r"\s", "", x) for x in lines).lower()

    s = section(txt["rfc8439"], r"^2\.8\.2\.\s+Example and Test Vector", r"^3\.\s+Implementation")
    v["c20p1305_plain"] = dump_hex(section(s, r"^\s*Plaintext:", r"^\s*AAD:"))
    v["c20p1305_aad"] = dump_hex(section(s, r"^\s*AAD:", r"^\s*Key:"))
    v["c20p1305_key"] = dump_hex(section(s, r"^\s*Key:", r"^\s*IV:"))
    iv = dump_hex(section(s, r"^\s*IV:", r"^\s*32-bit fixed-common part:"))
    fixed = dump_hex(section(s, r"^\s*32-bit fixed-common part:", r"^\s*Setup for generating"))
    v["c20p1305_nonce"] = fixed + iv
    v["c20p1305_cipher"] = dump_hex(section(s, r"^\s*Ciphertext:", r"^\s*AEAD Construction"))
    mt = re.search(r"^\s*Tag:\s*\n\s*([0-9a-fA-F:]+)\s*$", s, re.M)
    if not mt:
        sys.exit("ChaCha20-Poly1305 tag not found")
    v["c20p1305_tag"] = colon_hex(mt.group(1))

    s = section(txt["xchacha"], r"^A\.3\.1\.\s+AEAD_XCHACHA20_POLY1305", r"^A\.3\.2\.")
    v["xc20p1305_plain"] = run_hex(section(s, r"^\s*Plaintext:", r"^\s*AAD:"))
    v["xc20p1305_aad"] = run_hex(section(s, r"^\s*AAD:", r"^\s*Key:"))
    v["xc20p1305_key"] = run_hex(section(s, r"^\s*Key:", r"^\s*IV:"))
    v["xc20p1305_nonce"] = run_hex(section(s, r"^\s*IV:", r"^\s*32-bit fixed-common part:"))
    v["xc20p1305_cipher"] = run_hex(section(s, r"^\s*Ciphertext:", r"^\s*Tag:"))
    v["xc20p1305_tag"] = run_hex(section(s, r"^\s*Tag:", r"^A\.3\.2\."))

    expect = {
        "blake2b_abc": 64,
        "c20p1305_key": 32, "c20p1305_nonce": 12, "c20p1305_aad": 12,
        "c20p1305_plain": 114, "c20p1305_cipher": 114, "c20p1305_tag": 16,
        "xc20p1305_key": 32, "xc20p1305_nonce": 24, "xc20p1305_aad": 12,
        "xc20p1305_plain": 114, "xc20p1305_cipher": 114, "xc20p1305_tag": 16,
    }
    bad = []
    for name, want in expect.items():
        digits = len(v[name])
        got = digits // 2
        status = "ok" if (got == want and digits % 2 == 0) else "WRONG"
        if status == "WRONG":
            bad.append("%s: parsed %d hex digits (%d bytes), expected %d bytes"
                       % (name, digits, got, want))
        print("  %-20s %4d bytes  %s" % (name, got, status))
    if bad:
        sys.exit("\nparsing produced the wrong lengths:\n  " + "\n  ".join(bad))

    if v["c20p1305_plain"] != v["xc20p1305_plain"]:
        sys.exit("the two plaintexts should be identical but are not")
    if not v["c20p1305_plain"].startswith("4c616469657320616e64"):
        sys.exit("plaintext does not start with 'Ladies and' -- wrong block parsed")

    def carr(name, hexstr):
        b = bytes.fromhex(hexstr)
        lines = ["inline constexpr unsigned char %s[%d] = {\n" % (name, len(b))]
        for i in range(0, len(b), 12):
            lines.append("    " + " ".join("0x%02X," % x for x in b[i:i + 12]) + "\n")
        lines.append("};\n")
        return "".join(lines)

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "CryptoKat.h"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// Generated by Tools/gen_kat.py from RFC 7693, RFC 8439 and "
                "draft-irtf-cfrg-xchacha-03. Do not edit.\n"
                "#pragma once\n\nnamespace Bunker::Kat\n{\n")
        f.write(carr("kBlake2bAbc", v["blake2b_abc"]))
        f.write("\n")
        for k, n in (("c20p1305_key", "kC20Key"), ("c20p1305_nonce", "kC20Nonce"),
                     ("c20p1305_aad", "kC20Aad"), ("c20p1305_plain", "kC20Plain"),
                     ("c20p1305_cipher", "kC20Cipher"), ("c20p1305_tag", "kC20Tag")):
            f.write(carr(n, v[k]))
        f.write("\n")
        for k, n in (("xc20p1305_key", "kXC20Key"), ("xc20p1305_nonce", "kXC20Nonce"),
                     ("xc20p1305_aad", "kXC20Aad"), ("xc20p1305_plain", "kXC20Plain"),
                     ("xc20p1305_cipher", "kXC20Cipher"), ("xc20p1305_tag", "kXC20Tag")):
            f.write(carr(n, v[k]))
        f.write("}\n")

    print("\n  wrote %s" % os.path.join(OUT, "CryptoKat.h"))

if __name__ == "__main__":
    main()
