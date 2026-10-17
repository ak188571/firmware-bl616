"""PC-side TSV validation shared by font generation and the pre-copy checker."""
from pathlib import Path

MAX_BYTES=262144
MAX_TITLE_BYTES=512
EXTENSIONS={'nes':{'.nes'},'snes':{'.smc','.sfc'},'gba':{'.gba'},'genesis':{'.bin','.md'},'sms':{'.sms'},'pc':{'.img'}}


def valid_filename(name):
    return bool(name) and len(name.encode())<=255 and not any(ord(c)<32 or ord(c)>126 or c in '<>:"/\\|?*' for c in name) and not name.endswith((' ','.'))


def read_tsv(path):
    raw=Path(path).read_bytes()
    if len(raw)>MAX_BYTES: raise ValueError(f'{path}: TSV exceeds {MAX_BYTES} bytes')
    text=raw.decode('utf-8-sig',errors='strict')
    lines=text.split('\n')
    if lines[0].removesuffix('\r')!='filename\ttitle':
        raise ValueError(f'{path}:1: expected filename<TAB>title')
    seen=set(); result=[]
    for line_no,line in enumerate(lines[1:],2):
        line=line.removesuffix('\r')
        if not line: continue
        if line.count('\t')!=1: raise ValueError(f'{path}:{line_no}: exactly two tab-separated fields required')
        name,title=line.split('\t')
        if not valid_filename(name):
            raise ValueError(f'{path}:{line_no}: invalid ASCII ROM filename')
        if name.lower() in seen: raise ValueError(f'{path}:{line_no}: duplicate filename {name}')
        seen.add(name.lower())
        if len(title.encode())>MAX_TITLE_BYTES or any(ord(c)<32 or 0x7f<=ord(c)<=0x9f for c in title):
            raise ValueError(f'{path}:{line_no}: invalid title/control character or title exceeds {MAX_TITLE_BYTES} bytes')
        result.append((line_no,name,title))
    return result
