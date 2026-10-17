#!/usr/bin/env python3
import json,os,random,subprocess,sys,tempfile,unittest
from pathlib import Path
from titles_common import decode_title,layout_text,read_titles

TOOLS=Path(__file__).resolve().parent
ROOT=TOOLS.parents[1]
ENV={**os.environ,'PYTHONUTF8':'1'}

def run(tool,*args):
    return subprocess.run([sys.executable,str(TOOLS/tool),*map(str,args)],capture_output=True,text=True,encoding='utf-8',env=ENV)

class WrapTests(unittest.TestCase):
    def test_escapes_and_widths(self):
        self.assertEqual(decode_title(r'日本\nテスト'),'日本\nテスト')
        self.assertEqual(decode_title(r'A\\nB'),r'A\nB')
        for raw in [r'\nA',r'A\n',r'A\nB\nC',r'A\tB','A\\',r'A\n　','A'*31+r'\nB',r'A\n'+'B'*30]:
            with self.assertRaises(ValueError):decode_title(raw)
        self.assertEqual(layout_text('A'*29+'日本'),(['A'*29,'日本'],False))
        self.assertEqual(layout_text('日'*30),(['日'*15,'日'*14],True))
        self.assertFalse(layout_text('A'*59)[1]);self.assertTrue(layout_text('A'*60)[1])

    def test_checker_csv_and_migration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'game.nes').write_bytes(b'');(root/'child').mkdir()
            csv=root/'titles.csv'
            def write(title,name='game.nes'):
                csv.write_text('name\ttitle\n'+name+'\t'+title+'\n',encoding='utf-8')
            for title in [r'日本\nテスト','A'*31,'A'*59,r'A\\nB']:
                write(title);result=run('check_titles.py',root,'--core','nes')
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertNotIn('display clipped',result.stdout)
                self.assertNotIn('U+000A',result.stdout)
            write('A'*60);self.assertIn('clipped after two lines',run('check_titles.py',root,'--core','nes').stdout)
            write(r'A\n'+'B'*29,'child');result=run('check_titles.py',root,'--core','nes')
            self.assertNotEqual(result.returncode,0);self.assertIn('30/29',result.stdout)
            write(r'A\n'+'B'*28,'child');self.assertEqual(run('check_titles.py',root,'--core','nes').returncode,0)
            for raw in [r'A\tB',r'\nA',r'A\nB\nC']:
                write(raw)
                with self.assertRaises(ValueError):read_titles(csv)
            csv.unlink();old=root/'titles.tsv';old.write_text('filename\ttitle\ngame.nes\t日本\\nテスト\n',encoding='utf-8')
            result=run('migrate_titles.py',root,'--apply');self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(read_titles(csv)[0][2],r'日本\nテスト') # Raw escape preserved, no record splitting.

    def test_font_generation_controls(self):
        import contextlib,io,re,shutil
        import build_font
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);fw=root/'firmware';mon=root/'monitor';fontdir=mon/'src/iosys/ui16'
            (fw/'ui').mkdir(parents=True);fontdir.mkdir(parents=True)
            for name in ['ui16_font_manifest.json','ui16_font_map.h','ui16_config.h']:
                shutil.copyfile(ROOT/'ui'/name,fw/'ui'/name)
            config=(ROOT/'ui/ui16_config.h').read_text(encoding='utf-8')
            fontdir.joinpath('ui_config.svh').write_text(''.join(f'`define UI_{key} {value}\n' for key,value in re.findall(r'inline constexpr int (\w+) = (\d+);',config)),encoding='utf-8')
            csv=root/'titles.csv';csv.write_text('name\ttitle\ngame.nes\t日本\\nテスト\n',encoding='utf-8')
            before=json.loads((fw/'ui/ui16_font_manifest.json').read_text(encoding='utf-8'))
            old_root,old_argv=build_font.ROOT,sys.argv
            try:
                build_font.ROOT=fw;sys.argv=['build_font.py','--monitor',str(mon),'--csv',str(csv),'--capacity','1024']
                with contextlib.redirect_stdout(io.StringIO()):build_font.main()
                after=json.loads((fw/'ui/ui16_font_manifest.json').read_text(encoding='utf-8'))
                self.assertEqual(before['glyphs'],after['glyphs']);self.assertNotIn(10,{g['cp'] for g in after['glyphs']})
                original=(fw/'ui/ui16_font_manifest.json').read_bytes()
                csv.write_text('name\ttitle\ngame.nes\tA\\tB\n',encoding='utf-8')
                with self.assertRaises(ValueError):build_font.main()
                self.assertEqual(original,(fw/'ui/ui16_font_manifest.json').read_bytes())
            finally:build_font.ROOT=old_root;sys.argv=old_argv

    def test_cpp_python_layout_parity(self):
        rng=random.Random(5917)
        samples=['','A'*30,'A'*31,'A'*59,'A'*60,'日'*15,'日'*16,'日'*29,'日'*30,'A'*29+'日本','日本\nテスト']
        for _ in range(180):
            s=''.join(rng.choice('ABaz 123日本テスト/\\') for _ in range(rng.randrange(90)))
            if rng.randrange(3)==0:
                at=rng.randrange(len(s)+1);s=s[:at]+'\n'+s[at:]
            samples.append(s)
        exe=ROOT/'build-ui16-host/wrap_test'
        if os.name=='nt':exe=exe.with_suffix('.exe')
        for text in samples:
            result=subprocess.run([str(exe),'--layout',text.encode().hex()],capture_output=True,text=True,encoding='utf-8',check=True)
            valid,clipped,count,one,two=result.stdout.strip().split('|')
            try:lines,cut=layout_text(text)
            except ValueError:self.assertEqual(valid,'0',repr(text));continue
            actual=[bytes.fromhex(one).decode(),bytes.fromhex(two).decode()][:int(count)]
            self.assertEqual(valid,'1',repr(text));self.assertEqual(actual,lines,repr(text));self.assertEqual(clipped=='1',cut)

if __name__=='__main__':unittest.main()
