#!/usr/bin/env python3
"""Check titles.tsv against ROM files and the exact deployed font manifest."""
import argparse,json,unicodedata
from pathlib import Path
from titles_common import read_tsv,EXTENSIONS,valid_filename


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('tsv',type=Path)
    p.add_argument('--core',choices=EXTENSIONS,required=True)
    p.add_argument('--font-manifest',type=Path,default=Path(__file__).resolve().parents[2]/'ui/ui16_font_manifest.json')
    a=p.parse_args(); rows=read_tsv(a.tsv)
    manifest=json.loads(a.font_manifest.read_text(encoding='utf-8'))
    chars={e['cp'] for e in manifest['glyphs']}
    errors=warnings=0
    def report(kind,msg):
        nonlocal errors,warnings
        if kind=='ERROR': errors+=1
        else: warnings+=1
        print(f'{kind}: {msg}')
    roms={}
    for f in a.tsv.parent.iterdir():
        if f.is_file() and f.suffix.lower() in EXTENSIONS[a.core]:
            if not valid_filename(f.name): report('ERROR',f'ROM filename must be valid ASCII: {f.name}')
            key=f.name.lower()
            if key in roms: report('ERROR',f'ambiguous case-insensitive ROM name: {f.name}')
            roms[key]=f.name
    mapped=set(); used_titles=set()
    def check(text,label):
        for cp in sorted(set(map(ord,text))-chars): report('ERROR',f'{label}: missing U+{cp:04X} {chr(cp)}')
        if sum(1 if ord(c)<128 else 2 for c in text)>30: report('WARN',f'{label}: title clipped at 30 halfwidth units')
        if unicodedata.normalize('NFC',text)!=text: report('WARN',f'{label}: text is not NFC normalized')
    for line,name,title in rows:
        label=f'{a.tsv}:{line}'
        if name.lower() not in roms: report('ERROR',f'{label}: ROM missing or wrong extension: {name}')
        mapped.add(name.lower())
        if not title.strip(' \u3000'):
            title=name; report('WARN',f'{label}: blank title; filename will be used')
        elif title!=title.strip(' \u3000'): report('WARN',f'{label}: leading/trailing title spaces')
        if title in used_titles: report('WARN',f'{label}: duplicate display title: {title}')
        used_titles.add(title); check(title,label)
    for key,name in roms.items():
        if key not in mapped:
            report('WARN',f'{name}: unregistered; filename will be used'); check(name,name)
    print(f'{errors} errors, {warnings} warnings; font {manifest["sha256"]}')
    return 1 if errors else 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,OSError,KeyError) as e: raise SystemExit('ERROR: '+str(e))
