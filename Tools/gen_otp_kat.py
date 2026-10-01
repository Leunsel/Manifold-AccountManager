#!/usr/bin/env python3
"""Generate Source/Generated/OtpKat.h: SHA-1, HMAC-SHA-1, HOTP, TOTP and Base32
known-answer vectors parsed out of RFC 3174, 2202, 4226, 6238 and 4648.

The TOTP seeds come from RFC 6238's reference code (Appendix A), not from its
prose: SHA-256 and SHA-512 use 32- and 64-byte seeds, and the lengths are
asserted.

Run:  python Tools/gen_otp_kat.py   (RFC texts are cached in .rfc-cache/)
"""
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "Manifold-Vault", "Source", "Generated")

SOURCES = {
    "rfc2202": "https://www.rfc-editor.org/rfc/rfc2202.txt",
    "rfc3174": "https://www.rfc-editor.org/rfc/rfc3174.txt",
    "rfc4226": "https://www.rfc-editor.org/rfc/rfc4226.txt",
    "rfc4648": "https://www.rfc-editor.org/rfc/rfc4648.txt",
    "rfc6238": "https://www.rfc-editor.org/rfc/rfc6238.txt",
}

CACHE = os.path.join(HERE, "..", ".rfc-cache")

def fetch(name, url):
    """Download, with a local cache so a re-run does not hammer rfc-editor.org."""
    cache_dir = os.path.abspath(CACHE)
    os.makedirs(cache_dir, exist_ok=True)
    path = os.path.join(cache_dir, name + ".txt")
    if os.path.exists(path):
        with open(path, "rb") as f:
            raw = f.read()
    else:
        with urllib.request.urlopen(url, timeout=60) as r:
            raw = r.read()
        with open(path, "wb") as f:
            f.write(raw)
    return raw.decode("utf-8", "replace").replace("\r\n", "\n")

def strip_pagination(text):
    keep = []
    for ln in text.split("\n"):
        if ln.startswith("\f") or re.match(r"^.*\[Page \d+\]\s*$", ln):
            continue
        if re.match(r"^RFC \d+\s+.*\d{4}\s*$", ln):
            continue
        keep.append(ln)
    return "\n".join(keep)

def unhex(s):
    s = re.sub(r"[^0-9a-fA-F]", "", s)
    if len(s) % 2:
        sys.exit("odd hex digit count: %r" % s[:40])
    return bytes.fromhex(s)

def parse_sha1(text):
    """testarray/repeatcount/resultarray out of the RFC's own test driver."""
    defines = dict(re.findall(r'#define\s+(TEST\w+)\s+"([^"]*)"', text))
    for name, a, b in re.findall(r"#define\s+(TEST\d)\s+(TEST\w+)\s+(TEST\w+)", text):
        defines[name] = defines[a] + defines[b]

    counts = re.search(r"repeatcount\[4\]\s*=\s*\{([^}]*)\}", text)
    if not counts:
        sys.exit("RFC 3174: repeatcount not found")
    repeats = [int(x) for x in counts.group(1).replace(" ", "").split(",") if x]

    block = re.search(r"resultarray\[4\]\s*=\s*\{(.*?)\}", text, re.S)
    if not block:
        sys.exit("RFC 3174: resultarray not found")
    digests = re.findall(r'"([0-9A-Fa-f ]+)"', block.group(1))

    order = ["TEST1", "TEST2", "TEST3", "TEST4"]
    out = []
    for name, rep, dig in zip(order, repeats, digests):
        if name not in defines:
            sys.exit("RFC 3174: %s not defined" % name)
        out.append((defines[name], rep, unhex(dig)))
    if len(out) != 4:
        sys.exit("RFC 3174: expected 4 vectors, got %d" % len(out))
    return out

def parse_value(raw, declared_len, what, case):
    """A field is hex, a quoted string, or "0xNN repeated N times"."""
    raw = raw.strip()
    m = re.match(r"0x([0-9a-fA-F]{2})\s+repeated\s+(\d+)\s+times", raw)
    if m:
        value = bytes.fromhex(m.group(1)) * int(m.group(2))
    elif raw.startswith("0x"):
        value = unhex(raw[2:])
    elif raw.startswith('"'):
        parts = [p.strip() for p in raw.split("\n")]
        joined = " ".join(parts)
        m2 = re.match(r'^"(.*)"$', joined, re.S)
        if not m2:
            sys.exit("case %s: unterminated string in %s: %r" % (case, what, raw[:60]))
        value = m2.group(1).encode("ascii")
    else:
        sys.exit("case %s: cannot read %s: %r" % (case, what, raw[:60]))

    if declared_len is not None and len(value) != declared_len:
        sys.exit("case %s: %s parsed as %d bytes but the RFC says %d -- the parser drifted"
                 % (case, what, len(value), declared_len))
    return value

def parse_hmac_sha1(text):
    start = text.index("Test Cases for HMAC-SHA-1")
    end = text.index("Test Cases for HMAC-MD5") if "Test Cases for HMAC-MD5" in text[start:] else len(text)
    body = text[start:start + end] if end < start else text[start:]
    chunks = re.split(r"\ntest_case\s*=\s*(\d+)\n", "\n" + body)
    cases = []
    for i in range(1, len(chunks) - 1, 2):
        number = chunks[i]
        block = chunks[i + 1]
        def field(name, optional=False):
            m = re.search(r"^%s\s*=\s*(.*?)(?=\n\w[\w-]*\s*=|\Z)" % re.escape(name),
                          block, re.S | re.M)
            if not m:
                if optional:
                    return None
                sys.exit("case %s: field %s missing" % (number, name))
            return m.group(1)
        def length(name):
            m = re.search(r"^%s\s*=\s*(\d+)" % re.escape(name), block, re.M)
            return int(m.group(1)) if m else None

        key = parse_value(field("key"), length("key_len"), "key", number)
        data = parse_value(field("data"), length("data_len"), "data", number)
        digest = parse_value(field("digest"), 20, "digest", number)
        cases.append((key, data, digest))
        if len(cases) == 7:
            break
    if len(cases) != 7:
        sys.exit("RFC 2202: expected 7 HMAC-SHA-1 cases, got %d" % len(cases))
    return cases

def parse_hotp(text):
    hmacs = dict((int(c), unhex(h)) for c, h in
                 re.findall(r"^\s*(\d)\s+([0-9a-f]{40})\s*$", text, re.M))
    rows = re.findall(r"^\s*(\d)\s+([0-9a-f]{7,8})\s+(\d+)\s+(\d{6})\s*$", text, re.M)
    truncated = {}
    for c, hexv, dec, hotp in rows:
        truncated[int(c)] = (int(hexv, 16), int(dec), hotp)
    if len(hmacs) != 10 or len(truncated) != 10:
        sys.exit("RFC 4226: expected 10 HMAC rows and 10 truncated rows, got %d/%d"
                 % (len(hmacs), len(truncated)))
    out = []
    for c in range(10):
        hexv, dec, hotp = truncated[c]
        if hexv != dec:
            sys.exit("RFC 4226 count %d: the table's hex and decimal disagree" % c)
        out.append((c, hmacs[c], dec, hotp))
    return out

def parse_totp(text):
    def seed(name):
        m = re.search(r'String\s+%s\s*=\s*((?:"[0-9a-fA-F]+"\s*\+?\s*)+);' % name, text)
        if not m:
            sys.exit("RFC 6238: seed %s not found in Appendix A" % name)
        return unhex("".join(re.findall(r'"([0-9a-fA-F]+)"', m.group(1))))

    seeds = {"SHA1": seed("seed"), "SHA256": seed("seed32"), "SHA512": seed("seed64")}
    expect = {"SHA1": 20, "SHA256": 32, "SHA512": 64}
    for k, v in seeds.items():
        if len(v) != expect[k]:
            sys.exit("RFC 6238: %s seed is %d bytes, expected %d" % (k, len(v), expect[k]))
        if v != b"1234567890" * (len(v) // 10) + b"1234567890"[:len(v) % 10]:
            sys.exit("RFC 6238: %s seed is not the repeated digit string" % k)

    rows = re.findall(
        r"^\s*\|\s*(\d+)\s*\|\s*[\d-]+\s*\|\s*([0-9A-F]{16})\s*\|\s*(\d{8})\s*\|\s*(SHA\d+)\s*\|",
        text, re.M)
    if len(rows) < 18:
        sys.exit("RFC 6238: expected at least 18 vector rows, got %d" % len(rows))
    out = []
    for t, thex, totp, mode in rows:
        t = int(t)
        if int(thex, 16) != t // 30:
            sys.exit("RFC 6238: row t=%s has T=%s, which is not t/30" % (t, thex))
        out.append((t, mode, totp))
    return seeds, out

def parse_base32(text):
    rows = re.findall(r'BASE32\("([^"]*)"\)\s*=\s*"([^"]*)"', text)
    if len(rows) != 7:
        sys.exit("RFC 4648: expected 7 BASE32 vectors, got %d" % len(rows))
    return rows

def cbytes(data):
    return "{ " + ", ".join("0x%02X" % b for b in data) + " }" if data else "{ 0 }"

def cstring(data):
    out = []
    for b in data:
        c = chr(b)
        if c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif 0x20 <= b < 0x7F:
            out.append(c)
        else:
            out.append("\\x%02x" % b)
    return '"' + "".join(out) + '"'

def main():
    texts = {}
    for name, url in SOURCES.items():
        try:
            texts[name] = strip_pagination(fetch(name, url))
        except Exception as exc:
            sys.exit("could not fetch %s: %s" % (name, exc))

    sha1 = parse_sha1(texts["rfc3174"])
    hmac = parse_hmac_sha1(texts["rfc2202"])
    hotp = parse_hotp(texts["rfc4226"])
    seeds, totp = parse_totp(texts["rfc6238"])
    b32 = parse_base32(texts["rfc4648"])

    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "OtpKat.h")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by Tools/gen_otp_kat.py from RFC 3174, 2202, 4226, 6238 and 4648. "
                "Do not edit.\n"
                "#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n"
                "namespace Bunker::Kat\n{\n")

        f.write("    struct Sha1Vector { const char* Input; int Repeat; unsigned char Digest[20]; };\n")
        f.write("    inline constexpr Sha1Vector kSha1[] = {\n")
        for text, rep, dig in sha1:
            f.write("        { %s, %d, %s },\n" % (cstring(text.encode()), rep, cbytes(dig)))
        f.write("    };\n\n")

        f.write("    struct HmacVector {\n"
                "        unsigned char Key[131]; std::size_t KeyLen;\n"
                "        unsigned char Data[131]; std::size_t DataLen;\n"
                "        unsigned char Digest[20];\n"
                "    };\n")
        f.write("    inline constexpr HmacVector kHmacSha1[] = {\n")
        for key, data, dig in hmac:
            f.write("        { %s, %d, %s, %d, %s },\n"
                    % (cbytes(key), len(key), cbytes(data), len(data), cbytes(dig)))
        f.write("    };\n\n")

        f.write('    inline constexpr char kHotpSecret[] = "12345678901234567890";\n')
        f.write("    struct HotpVector {\n"
                "        std::uint64_t Counter;\n"
                "        unsigned char Hmac[20];\n"
                "        std::uint32_t Truncated;\n"
                "        const char* Code;\n"
                "    };\n")
        f.write("    inline constexpr HotpVector kHotp[] = {\n")
        for c, h, dec, code in hotp:
            f.write("        { %d, %s, %uu, \"%s\" },\n" % (c, cbytes(h), dec, code))
        f.write("    };\n\n")

        for mode in ("SHA1", "SHA256", "SHA512"):
            f.write("    inline constexpr unsigned char kTotpSeed%s[] = %s;\n"
                    % (mode, cbytes(seeds[mode])))
        f.write("    struct TotpVector { std::uint64_t UnixTime; const char* Algorithm; const char* Code; };\n")
        f.write("    inline constexpr TotpVector kTotp[] = {\n")
        for t, mode, code in totp:
            f.write('        { %dull, "%s", "%s" },\n' % (t, mode, code))
        f.write("    };\n\n")

        f.write("    struct Base32Vector { const char* Plain; const char* Encoded; };\n")
        f.write("    inline constexpr Base32Vector kBase32[] = {\n")
        for plain, enc in b32:
            f.write('        { "%s", "%s" },\n' % (plain, enc))
        f.write("    };\n")
        f.write("}\n")

    print("  OtpKat.h")
    print("    SHA-1        %d vectors (longest repeat %d)"
          % (len(sha1), max(r for _, r, _ in sha1)))
    print("    HMAC-SHA-1   %d cases (key sizes %s)"
          % (len(hmac), sorted(set(len(k) for k, _, _ in hmac))))
    print("    HOTP         %d counters, with HMAC and truncation pinned separately" % len(hotp))
    print("    TOTP         %d rows; seeds SHA1=%dB SHA256=%dB SHA512=%dB"
          % (len(totp), len(seeds["SHA1"]), len(seeds["SHA256"]), len(seeds["SHA512"])))
    print("    Base32       %d vectors" % len(b32))

if __name__ == "__main__":
    main()
