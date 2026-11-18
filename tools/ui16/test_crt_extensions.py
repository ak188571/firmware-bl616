"""Exercise production CRT menu, ROM identification and two-slot persistence on a PC."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib

repo = Path(__file__).resolve().parents[2]
env = os.environ.copy()
compiler = [env.get("CXX", "g++")]
if os.name == "nt":
    bindir = Path("C:/msys64/ucrt64/bin")
    compiler = [str(bindir/"g++.exe"), "-B"+bindir.as_posix()+"/"]
    env["PATH"] = str(bindir)+os.pathsep+env["PATH"]
with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp)
    exe = work/"test.exe"
    subprocess.run([*compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                    "-DUI16_SYSTEM_MENU", "-Itests/ui16", "-Iui", "tests/ui16/crt_extensions_test.cpp",
                    "ui/ui16_crt.cpp", "ui/ui16_osd.cpp", "ui/ui16_crt_save.cpp",
                    "ui/ui16_crt_restore.cpp", "ui/ui16_rom_id.cpp", "-o", str(exe)], cwd=repo,env=env,check=True)
    # Same two ROM byte streams on both systems also exercise system-key isolation.
    for system in (1,2):
        for name in ("a","b"):
            (work/(name+".rom")).write_bytes(name.encode()*790)
        data = (work/"a.rom").read_bytes()
        crc = zlib.crc32(data)
        legacy = struct.pack("<4sBBBBIQII",b"CRT1",1,system,0,0,crc,len(data),19,7)
        legacy += struct.pack("<I",zlib.crc32(legacy))
        (work/"crt").mkdir(exist_ok=True)
        slot = work/"crt"/f"{system:02x}-{crc:08x}-{len(data):016x}.a"
        slot.write_bytes(legacy)
        other = {p:p.read_bytes() for p in (work/"crt").glob(f"{3-system:02x}-*")}
        subprocess.run([str(exe),str(system)],cwd=work,env=env,check=True)
        assert slot.read_bytes()==legacy and all(p.read_bytes()==v for p,v in other.items())
