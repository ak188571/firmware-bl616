#!/usr/bin/env python3
import contextlib,hashlib,io,json,shutil,subprocess,sys,tempfile,unittest
from pathlib import Path
from titles_common import read_tsv
import build_font

ROOT=Path(__file__).resolve().parents[2]

class TitlesTests(unittest.TestCase):
    def test_formats_and_checker(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);tsv=root/'titles.tsv';(root/'game001.nes').write_bytes(b'')
            tsv.write_text('\ufefffilename\ttitle\r\ngame001.nes\t日本「テスト」、ABC!\r\n',encoding='utf-8',newline='')
            self.assertEqual(read_tsv(tsv)[0][2],'日本「テスト」、ABC!')
            def check():return subprocess.run([sys.executable,str(Path(__file__).with_name('check_titles.py')),str(tsv),'--core','nes'],capture_output=True,text=True)
            self.assertEqual(check().returncode,0)
            for content in ['filename\ttitle\na.nes\tx\nA.NES\ty\n','filename\ttitle\n\tx\n','filename\ttitle\na.nes\tx\ty\n','filename\ttitle\na.nes\t'+('x'*513)]:
                tsv.write_text(content)
                with self.assertRaises(ValueError):read_tsv(tsv)
            tsv.write_text('filename\ttitle\nmissing.nes\tABC\n');self.assertNotEqual(check().returncode,0)
            tsv.write_text('filename\ttitle\ngame001.nes\t😀\n');self.assertIn('U+1F600',check().stdout)
            tsv.write_text('filename\ttitle\ngame001.nes\t\n');r=check();self.assertEqual(r.returncode,0);self.assertIn('filename will be used',r.stdout)
            tsv.write_bytes(b'filename\ttitle\na.nes\t\xff')
            with self.assertRaises(UnicodeError):read_tsv(tsv)

    def test_font_stability_and_capacity(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);fw=root/'firmware';mon=root/'monitor';(fw/'ui').mkdir(parents=True);(mon/'src/iosys/ui16').mkdir(parents=True)
            for name in ['ui16_font_manifest.json','ui16_font_map.h','ui16_config.h']:shutil.copyfile(ROOT/'ui'/name,fw/'ui'/name)
            # Initial config can be generated from current hardware-profile values.
            config=(ROOT/'ui/ui16_config.h').read_text()
            import re
            sv=''.join(f'`define UI_{key} {value}\n' for key,value in re.findall(r'inline constexpr int (\w+) = (\d+);',config))
            (mon/'src/iosys/ui16/ui_config.svh').write_text(sv)
            tsv=root/'titles.tsv';tsv.write_text('filename\ttitle\na.nes\t冒険\n',encoding='utf-8')
            original=json.loads((fw/'ui/ui16_font_manifest.json').read_text());old_root=build_font.ROOT;old_argv=sys.argv
            try:
                build_font.ROOT=fw
                sys.argv=['build_font.py','--monitor',str(mon),'--tsv',str(tsv),'--capacity','1024']
                with contextlib.redirect_stdout(io.StringIO()):build_font.main()
                first=(fw/'ui/ui16_font_manifest.json').read_bytes()
                new=json.loads(first)
                self.assertEqual(new['glyphs'][:len(original['glyphs'])],original['glyphs'])
                self.assertGreater(len(new['glyphs']),len(original['glyphs']))
                with contextlib.redirect_stdout(io.StringIO()):build_font.main()
                self.assertEqual(first,(fw/'ui/ui16_font_manifest.json').read_bytes())
                sys.argv[-1]='256'
                with self.assertRaises(ValueError):build_font.main()
                self.assertEqual(first,(fw/'ui/ui16_font_manifest.json').read_bytes())
            finally:build_font.ROOT=old_root;sys.argv=old_argv

if __name__=='__main__':unittest.main()
