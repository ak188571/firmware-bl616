#!/usr/bin/env python3
"""Copy legacy titles.tsv to strict tab-separated titles.csv, changing only the header."""
import argparse
from pathlib import Path
from titles_common import read_titles,walk_directories


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path,help='ROM root folder, processed recursively')
    p.add_argument('--apply',action='store_true',help='Create files; otherwise preview only. Legacy files are retained.')
    a=p.parse_args();jobs=[]
    # Validate every source/conflict before writing anything.
    for directory in walk_directories(a.root):
        old=directory/'titles.tsv';new=directory/'titles.csv'
        if not old.is_file():continue
        read_titles(old,header='filename')
        text=old.read_bytes().decode('utf-8-sig')
        converted=('name'+text[len('filename'):]).encode('utf-8')
        if new.exists():
            if new.read_bytes()==converted:print('Already migrated:',new);continue
            raise ValueError(f'Existing different file will not be overwritten: {new}')
        jobs.append((new,converted))
    for new,data in jobs:
        if a.apply:
            with new.open('xb') as f:f.write(data)
            print('Created:',new)
        else:print('Would create:',new)
    print(f'{len(jobs)} files; legacy titles.tsv retained.'+('' if a.apply else ' Add --apply to write.'))
    return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (ValueError,OSError) as e:raise SystemExit('ERROR: '+str(e))
