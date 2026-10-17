"""Strict tab-separated titles.csv and directory traversal shared by PC tools."""
from pathlib import Path

MAX_BYTES=262144
MAX_TITLE_BYTES=512
MAX_DEPTH=16
MAX_PATH=511
EXTENSIONS={'nes':{'.nes'},'snes':{'.smc','.sfc'},'gba':{'.gba'},'genesis':{'.bin','.md'},'sms':{'.sms'},'pc':{'.img'}}


def valid_filename(name):
    return bool(name) and len(name.encode())<=255 and not any(ord(c)<32 or ord(c)>126 or c in '<>:"/\\|?*' for c in name) and not name.endswith((' ','.'))


def read_titles(path,header='name'):
    if Path(path).is_symlink():raise ValueError(f'Symbolic links are not supported: {path}')
    raw=Path(path).read_bytes()
    if len(raw)>MAX_BYTES:raise ValueError(f'{path}: file exceeds {MAX_BYTES} bytes')
    lines=raw.decode('utf-8-sig',errors='strict').split('\n')
    if lines[0].removesuffix('\r')!=header+'\ttitle':
        raise ValueError(f'{path}:1: expected {header}<TAB>title; comma-separated CSV is not accepted')
    seen=set();result=[]
    for line_no,line in enumerate(lines[1:],2):
        line=line.removesuffix('\r')
        if not line:continue
        if line.count('\t')!=1:raise ValueError(f'{path}:{line_no}: exactly two tab-separated fields required')
        name,title=line.split('\t')
        if not valid_filename(name):raise ValueError(f'{path}:{line_no}: invalid ASCII file/folder name (no paths or trailing slash)')
        if name.lower() in seen:raise ValueError(f'{path}:{line_no}: duplicate name {name}')
        seen.add(name.lower())
        if len(title.encode())>MAX_TITLE_BYTES or any(ord(c)<32 or 0x7f<=ord(c)<=0x9f for c in title):
            raise ValueError(f'{path}:{line_no}: invalid title/control character or title exceeds {MAX_TITLE_BYTES} bytes')
        try:decode_title(title)
        except ValueError as e:raise ValueError(f'{path}:{line_no}: {e}') from e
        result.append((line_no,name,title))
    return result


def visible(path):
    return not (getattr(path.stat(follow_symlinks=False),'st_file_attributes',0)&6)


def walk_directories(root):
    root=Path(root)
    if not root.is_dir():raise ValueError(f'Not a directory: {root}')
    stack=[(root,0)]
    while stack:
        directory,depth=stack.pop()
        if directory.is_symlink():raise ValueError(f'Symbolic-link directories are not supported: {directory}')
        if depth>MAX_DEPTH or len(directory.relative_to(root).as_posix().encode())>MAX_PATH:
            raise ValueError(f'Folder depth/path exceeds device limit: {directory}')
        yield directory
        children=sorted((p for p in directory.iterdir() if visible(p) and p.is_dir()),key=lambda p:p.name.lower(),reverse=True)
        stack.extend((p,depth+1) for p in children)


def csv_paths(inputs):
    found=set()
    for source in inputs:
        source=Path(source)
        if source.is_dir():
            for directory in walk_directories(source):
                path=directory/'titles.csv'
                if path.is_file():found.add(path.resolve())
                elif (directory/'titles.tsv').exists():raise ValueError(f'Migrate legacy titles.tsv first: {directory}')
        elif source.name=='titles.csv' and source.is_file():found.add(source.resolve())
        else:raise ValueError(f'Expected titles.csv or a ROM root folder: {source}')
    return sorted(found)


def decode_title(raw):
    """Decode only literal backslash-n and doubled backslash in the title field."""
    out=[];i=0
    while i<len(raw):
        c=raw[i];i+=1
        if c!='\\':out.append(c);continue
        if i==len(raw):raise ValueError('incomplete title escape (use \\\\ for a literal backslash)')
        c=raw[i];i+=1
        if c=='n':out.append('\n')
        elif c=='\\':out.append('\\')
        else:raise ValueError('unknown title escape; only \\n and \\\\ are supported')
    result=''.join(out)
    layout_text(result) # Validate forced line breaks without a folder suffix.
    return result


def text_units(text):
    return sum(1 if ord(c)<128 else 2 for c in text)


def layout_text(text):
    """Decoded display text -> (lines, clipped); matches ui16_text_layout.h."""
    if '\n' in text:
        lines=text.split('\n')
        if len(lines)!=2:raise ValueError('at most one forced line break is allowed')
        if any(not line.strip(' \u3000') for line in lines):raise ValueError('forced line break must not create an empty line')
        if text_units(lines[0])>30 or text_units(lines[1])>29:
            raise ValueError('forced lines exceed 30/29 halfwidth units (second line includes one-unit indentation)')
        return lines,False
    lines=[];pos=0
    for width in (30,29):
        start=pos;used=0
        while pos<len(text):
            advance=1 if ord(text[pos])<128 else 2
            if used+advance>width:break
            used+=advance;pos+=1
        lines.append(text[start:pos])
        if pos==len(text):return lines,False
    return lines,True
