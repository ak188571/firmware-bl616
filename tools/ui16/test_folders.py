#!/usr/bin/env python3
import subprocess,sys,tempfile,unittest
from pathlib import Path
from titles_common import read_titles,csv_paths,walk_directories

TOOLS=Path(__file__).resolve().parent


def run(tool,*args):
    return subprocess.run([sys.executable,str(TOOLS/tool),*map(str,args)],capture_output=True,text=True)


class FolderTests(unittest.TestCase):
    def test_recursive_check_and_scope(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);child=root/'action';child.mkdir();nested=child/'nested';nested.mkdir()
            (root/'root.nes').write_bytes(b'');(child/'xxx.NES').write_bytes(b'');(nested/'deep.nes').write_bytes(b'')
            (root/'titles.csv').write_text('name\ttitle\naction\tアクション\nroot.nes\tROOT\n',encoding='utf-8')
            (child/'titles.csv').write_text('name\ttitle\nnested\t日本\nxxx.nes\t日本、 "ABC"!\n',encoding='utf-8')
            (nested/'titles.csv').write_text('name\ttitle\ndeep.nes\t\n')
            result=run('check_titles.py',root,'--core','nes')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('3 folders',result.stdout)
            self.assertEqual(len(csv_paths([root])),3)
            self.assertEqual(read_titles(child/'titles.csv')[1][2],'日本、 "ABC"!')
            (child/'titles.csv').write_text('name\ttitle\nroot.nes\tROOT\n')
            self.assertNotEqual(run('check_titles.py',root,'--core','nes').returncode,0) # parent mapping never inherited
            for invalid in ['name,title\naction,ACTION\n','filename\ttitle\naction\tA\n','name\ttitle\naction/\tA\n','name\ttitle\naction/xxx.nes\tA\n','name\ttitle\naction\tA\nACTION\tB\n']:
                (root/'titles.csv').write_text(invalid)
                with self.assertRaises(ValueError):read_titles(root/'titles.csv')

    def test_migration_preview_apply_and_conflicts(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);child=root/'action';child.mkdir()
            original=b'\xef\xbb\xbffilename\ttitle\r\na.nes\tABC, "DEF"!\r\n'
            (root/'titles.tsv').write_bytes(original)
            (child/'titles.tsv').write_text('filename\ttitle\na.nes\t日本\n',encoding='utf-8')
            preview=run('migrate_titles.py',root);self.assertEqual(preview.returncode,0);self.assertFalse((root/'titles.csv').exists())
            with self.assertRaises(ValueError):csv_paths([root])
            apply=run('migrate_titles.py',root,'--apply');self.assertEqual(apply.returncode,0,apply.stderr)
            self.assertEqual((root/'titles.tsv').read_bytes(),original)
            self.assertEqual(read_titles(root/'titles.csv')[0][2],'ABC, "DEF"!')
            self.assertEqual(len(csv_paths([root])),2)
            again=run('migrate_titles.py',root,'--apply');self.assertEqual(again.returncode,0);self.assertIn('Already migrated',again.stdout)
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);child=root/'action';child.mkdir()
            for folder in [root,child]:(folder/'titles.tsv').write_text('filename\ttitle\na.nes\tA\n')
            (child/'titles.csv').write_text('name\ttitle\na.nes\tB\n')
            self.assertNotEqual(run('migrate_titles.py',root,'--apply').returncode,0)
            self.assertFalse((root/'titles.csv').exists()) # conflicts checked before any writes

    def test_depth_and_visible_count(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for i in range(11):(root/f'{i}.nes').write_bytes(b'')
            result=run('check_titles.py',root,'--core','nes');self.assertEqual(result.returncode,0);self.assertNotIn('first 10',result.stdout)
            self.assertNotIn('device list limit',result.stdout)
            for i in range(11,257):(root/f'{i}.nes').write_bytes(b'')
            result=run('check_titles.py',root,'--core','nes');self.assertEqual(result.returncode,0);self.assertIn('device list limit is 256',result.stdout)
            folder=root
            for _ in range(17):folder=folder/'d';folder.mkdir()
            with self.assertRaises(ValueError):list(walk_directories(root))

if __name__=='__main__':unittest.main()
