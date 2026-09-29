#!/usr/bin/env python3
"""Convert Fugazi PO translations into its escaped runtime TSV table."""
import argparse
import re
from pathlib import Path

def unescape(value):
    return value.replace(r"\\", "\0").replace(r"\n", "\n").replace(r"\t", "\t").replace(r'\"', '"').replace("\0", "\\")

def entries(text):
    result=[]; key=value=None; state=None; fuzzy=False; quote=re.compile(r'^"((?:[^"\\]|\\.)*)"$')
    def flush():
        nonlocal key,value,fuzzy
        if key and value and not fuzzy: result.append((key,value))
        key=value=None; fuzzy=False
    for raw in text.splitlines()+[""]:
        line=raw.strip()
        if not line: flush(); state=None; continue
        if line.startswith('#,'): fuzzy |= 'fuzzy' in line
        elif line.startswith('msgid '): flush(); key=unescape(quote.search(line[6:]).group(1)); state='key'
        elif line.startswith('msgstr '): value=unescape(quote.search(line[7:]).group(1)); state='value'
        elif quote.match(line) and state:
            part=unescape(quote.match(line).group(1))
            if state == 'key': key += part
            else: value = (value or '') + part
    return result

def esc(value): return value.replace('\\','\\\\').replace('\t','\\t').replace('\n','\\n')
def fmt_sig(value):
    return tuple(re.findall(r'%(?:[-+ #0-9.*\']*)(?:h|l|L|q|j|z|t)?[A-Za-z]', value))
parser=argparse.ArgumentParser(); parser.add_argument('po'); parser.add_argument('-o','--output',required=True); args=parser.parse_args()
rows=entries(Path(args.po).read_text(encoding='utf-8'))
seen=set()
for key, value in rows:
    if key in seen: raise SystemExit(f'duplicate i18n key: {key}')
    if fmt_sig(key) != fmt_sig(value): raise SystemExit(f'placeholder mismatch: {key}')
    seen.add(key)
if any('\t' in k or '\t' in v for k,v in rows): raise SystemExit('tabs are not allowed in i18n entries')
out=Path(args.output); out.parent.mkdir(parents=True,exist_ok=True); out.write_text('# Generated from '+args.po+'\n'+'\n'.join(esc(k)+'\t'+esc(v) for k,v in rows)+'\n',encoding='utf-8')
print(f'{out}: {len(rows)} entries')
