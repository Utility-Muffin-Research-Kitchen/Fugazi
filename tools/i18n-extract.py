#!/usr/bin/env python3
"""Validate Fugazi's committed translation catalog and UI boundary."""
import argparse
import sys
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def ids(path):
    keys = set(); current = ""; state = None
    for raw in path.read_text(encoding="utf-8").splitlines() + [""]:
        line = raw.strip()
        if not line:
            if current: keys.add(current)
            current = ""; state = None; continue
        if line.startswith("msgid "):
            if current: keys.add(current)
            match = re.match(r'^msgid "(.*)"$', line)
            current = match.group(1) if match else ""; state = "id"
        elif line.startswith("msgstr "):
            state = "str"
        elif state == "id" and re.match(r'^".*"$', line):
            current += line[1:-1]
    return {key for key in keys if key}

def fmt_sig(s):
    return tuple(re.findall(r'%(?:[-+ #0-9.*\']*)(?:h|l|L|q|j|z|t)?[A-Za-z]', s))

def source_ids():
    text = (ROOT / "cmd" / "fugazi" / "main.c").read_text(encoding="utf-8")
    return {m.group(1) for m in re.finditer(r'\bT\("((?:[^"\\]|\\.)*)"\)', text)}

pot_path = ROOT / "i18n" / "fugazi.pot"
pot = ids(pot_path)
po_path = ROOT / "i18n" / "zh_CN.po"
po = ids(po_path)
source = (ROOT / "cmd" / "fugazi" / "main.c").read_text(encoding="utf-8")
if not pot <= po:
    raise SystemExit("PO is missing a POT key")
entries = []
for block in po_path.read_text(encoding="utf-8").split("\n\n"):
    mid = re.search(r'^msgid "(.*)"$', block, re.M)
    mstr = re.search(r'^msgstr "(.*)"$', block, re.M)
    if mid and mstr and mid.group(1): entries.append((mid.group(1), mstr.group(1), "#, fuzzy" in block))
if len({k for k, _, _ in entries}) != len(entries): raise SystemExit("PO contains duplicate keys")
if any(k not in pot for k, _, _ in entries): raise SystemExit("PO contains orphan keys: " + repr([k for k, _, _ in entries if k not in pot][:3]))
if any(v and fmt_sig(k) != fmt_sig(v) for k, v, _ in entries): raise SystemExit("PO contains placeholder mismatch")
reviewed = sum(bool(v) and not f for k, v, f in entries if k in pot)
if reviewed * 100 // max(1, len(pot)) < 90: raise SystemExit("PO reviewed coverage is below 90%")
if "fz_i18n_message" not in source or "fz_i18n_footer" not in source:
    raise SystemExit("Fugazi UI translation boundary is missing")
print(f"i18n catalog check passed: {len(pot)} keys")
