#!/usr/bin/env python3
"""Recursively check tab-separated titles.csv, ROMs and folders against a font."""
import argparse,json,unicodedata
from pathlib import Path
from titles_common import read_titles,EXTENSIONS,valid_filename,walk_directories,visible,decode_title,layout_text


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('path',type=Path,help='ROM root directory, or its titles.csv')
    p.add_argument('--core',choices=EXTENSIONS,required=True)
    p.add_argument('--font-manifest',type=Path,default=Path(__file__).resolve().parents[2]/'ui/ui16_font_manifest.json')
    a=p.parse_args()
    root=a.path
    if not root.is_dir():
        if root.name!='titles.csv' or not root.is_file():raise ValueError('Specify an existing titles.csv or ROM root directory')
        root=root.parent
    manifest=json.loads(a.font_manifest.read_text(encoding='utf-8'));chars={e['cp'] for e in manifest['glyphs']}
    errors=warnings=folders=0
    def report(kind,msg):
        nonlocal errors,warnings
        if kind=='ERROR':errors+=1
        else:warnings+=1
        print(f'{kind}: {msg}')
    def check(text,label,is_dir,encoded=False):
        text=decode_title(text) if encoded else text
        try:
            _,clipped=layout_text(text+('/' if is_dir else ''))
        except ValueError as e:report('ERROR',f'{label}: {e}');clipped=False
        for cp in sorted(set(map(ord,text.replace(chr(10),'')))-chars):report('ERROR',f'{label}: missing U+{cp:04X} {chr(cp)}')
        if clipped:report('WARN',f'{label}: display clipped after two lines (30/29 halfwidth units)')
        if unicodedata.normalize('NFC',text)!=text:report('WARN',f'{label}: text is not NFC normalized')
    for directory in walk_directories(root):
        folders+=1;path=directory/'titles.csv';entries={}
        for f in directory.iterdir():
            if not visible(f):continue
            if f.is_symlink():report('ERROR',f'Symbolic links are not supported: {f}');continue
            if f.is_dir() or (f.is_file() and f.suffix.lower() in EXTENSIONS[a.core]):
                if not valid_filename(f.name):report('ERROR',f'ASCII file/folder name required: {f}')
                key=f.name.lower()
                if key in entries:report('ERROR',f'Ambiguous case-insensitive name: {f}')
                entries[key]=f
        if len(entries)>256:report('WARN',f'{directory}: {len(entries)} entries; device list limit is 256 (split into subfolders)')
        if not path.is_file() and (directory/'titles.tsv').exists():report('ERROR',f'{directory}: migrate legacy titles.tsv to titles.csv (name<TAB>title)')
        try:rows=read_titles(path) if path.is_file() else []
        except ValueError as e:report('ERROR',str(e));continue
        mapped=set();used_titles=set()
        for line,name,title in rows:
            label=f'{path}:{line}';entry=entries.get(name.lower())
            if entry is None:report('ERROR',f'{label}: file/folder missing, hidden or wrong ROM extension: {name}')
            mapped.add(name.lower())
            if not title.strip(' \u3000'):title=name;report('WARN',f'{label}: blank title; original name will be used')
            elif title!=title.strip(' \u3000'):report('WARN',f'{label}: leading/trailing title spaces')
            if title in used_titles:report('WARN',f'{label}: duplicate display title: {title}')
            used_titles.add(title);check(title,label,bool(entry and entry.is_dir()),True)
        for key,entry in entries.items():
            if key not in mapped:report('WARN',f'{entry}: unregistered; original name will be used');check(entry.name,str(entry),entry.is_dir())
    print(f'{errors} errors, {warnings} warnings, {folders} folders; font {manifest["sha256"]}')
    return 1 if errors else 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (ValueError,OSError,KeyError) as e:raise SystemExit('ERROR: '+str(e))
