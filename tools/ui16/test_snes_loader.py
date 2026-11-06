"""Compile the real SNES loader with only platform includes substituted."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2]
build=root/'build-ui16-host';build.mkdir(exist_ok=True)
source=(root/'core/snes.cpp').read_text(encoding='utf-8')
source=source.replace('#include "utils.h"','#include "snes_loader_platform.h"').replace('#include "cores.h"','')
path=build/'snes_loader_under_test.cpp';path.write_text(source,encoding='utf-8')
exe=build/'snes_loader_test.exe'
subprocess.run([os.environ.get('CXX','g++'),'-BC:/msys64/ucrt64/bin/','-std=c++17','-Wall','-Wextra','-Werror',
    '-Wno-unused-parameter','-Wno-sign-compare','-I'+str(root/'tests/ui16'),'-I'+str(root/'ui'),
    str(path),str(root/'tests/ui16/snes_loader_test.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
