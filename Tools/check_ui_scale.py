#!/usr/bin/env python3
"""Find fixed pixel sizes in the UI code that do not follow the display scale.

ImGui scales fonts through style.FontScaleDpi, but sizes the UI code writes
itself (ImVec2(190, 32)) and font sizes passed to AddText/CalcTextSizeA stay
as they are. Every such value must go through Px() (UI/UiScale.h).

  Usage:    python Tools/check_ui_scale.py
  Returns:  0 when clean, 1 when something was found.

A text check: it catches the common forms, not arithmetic on raw numbers or
values passed through variables.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
UI = os.path.join(ROOT, "Manifold-Vault", "Source", "UI")

SKIP = {"UiScale.h", "BunkerFonts.cpp"}

IMVEC2 = re.compile(r"ImVec2\(\s*(-?\d+(?:\.\d+)?f?)\s*,\s*(-?\d+(?:\.\d+)?f?)\s*\)")

RAW_FONT = re.compile(r"(AddText|CalcTextSizeA)\s*\([^;]*?(?<!Px\()Fonts::Sizes::")

def is_zero(text):
    return text.rstrip("f") in ("0", "0.0", "-0")

def wrapped_in_px(line, at):
    """True when the ImVec2 at 'at' sits inside a Px(...) call."""
    return line[:at].rstrip().endswith("Px(")

def main():
    if not os.path.isdir(UI):
        sys.exit("UI directory not found: %s" % UI)

    findings = []
    for name in sorted(os.listdir(UI)):
        if not name.endswith((".cpp", ".h")) or name in SKIP:
            continue
        path = os.path.join(UI, name)
        with open(path, encoding="utf-8-sig") as f:
            lines = f.readlines()

        for n, line in enumerate(lines, 1):
            stripped = line.lstrip()
            if stripped.startswith("//"):
                continue

            for m in IMVEC2.finditer(line):
                if is_zero(m.group(1)) and is_zero(m.group(2)):
                    continue
                if wrapped_in_px(line, m.start()):
                    continue
                findings.append((name, n, "fixed size without Px", m.group(0)))

            if RAW_FONT.search(line):
                findings.append((name, n, "font size used raw, without Px",
                                 stripped.rstrip()[:70]))

    if not findings:
        print("  clean -- no fixed pixel size without Px() in Source/UI")
        return 0

    print("  %d place(s) that do not follow the display scale:\n" % len(findings))
    for name, n, why, what in findings:
        print("    %-20s %5d  %-34s %s" % (name, n, why, what))
    print("\n  See Source/UI/UiScale.h: Px(...) around every fixed size and every")
    print("  font size for AddText/CalcTextSizeA -- but NOT around a PushFont")
    print("  argument, which would scale it twice.")
    return 1

if __name__ == "__main__":
    sys.exit(main())
