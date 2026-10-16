#!/usr/bin/env python3
"""Add an OFF-by-default fixed-frame test option to the v0.9 BL616 firmware."""
import argparse
from pathlib import Path
import shutil
import sys

ROOT=Path(__file__).resolve().parents[2]
TAG='// UI16 fixed-frame test BEGIN'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    try:
        main_file=ROOT/'main.cpp';cmake_file=ROOT/'CMakeLists.txt';make_file=ROOT/'Makefile'
        main_source=main_file.read_text(encoding='utf-8');cmake_source=cmake_file.read_text(encoding='utf-8')
        patched_main=main_source;patched_cmake=cmake_source
        make_source=make_file.read_text(encoding='utf-8');patched_make=make_source
        if TAG not in main_source:
            anchor='''    // load monitor core at startup
    string fname;
    if (find_core_for_board(fname, "monitor.bin")) {
        fpga_program(fname.c_str());'''
            addition='''
        // UI16 fixed-frame test BEGIN
#ifdef UI16_UART_SELFTEST
        extern void ui16_send_selftest();
        ui16_send_selftest();
        for (;;) delay(1000);
#endif
        // UI16 fixed-frame test END'''
            if main_source.count(anchor)!=1:raise ValueError('monitor-load anchor differs; inspect main.cpp')
            patched_main=main_source.replace(anchor,anchor+addition,1)
        cmake_tag='# UI16 fixed-frame test option'
        if cmake_tag not in cmake_source:
            anchor='set(CMAKE_CXX_STANDARD_REQUIRED ON)'
            if cmake_source.count(anchor)!=1:raise ValueError('CMake C++17 anchor differs')
            addition='''

# UI16 fixed-frame test option
option(UI16_UART_SELFTEST "Send fixed UI16 frame after monitor loads" OFF)
if(UI16_UART_SELFTEST)
    add_definitions(-DUI16_UART_SELFTEST)
endif()'''
            patched_cmake=cmake_source.replace(anchor,anchor+addition,1)
        if 'UI16_UART_SELFTEST ?= OFF' not in make_source:
            anchor='cmake_definition+=-DTANG_BOARD=$(TANG_BOARD)'
            if make_source.count(anchor)!=1:raise ValueError('Makefile board-option anchor differs')
            patched_make=make_source.replace(anchor,anchor+'\nUI16_UART_SELFTEST ?= OFF\ncmake_definition+=-DUI16_UART_SELFTEST=$(UI16_UART_SELFTEST)',1)
        print('Changes: main.cpp test hook; CMakeLists.txt/Makefile OFF-by-default option. USB sources untouched.')
        if not args.apply:
            print('Dry run succeeded; add --apply to write.');return 0
        for path,old,new in [(main_file,main_source,patched_main),(cmake_file,cmake_source,patched_cmake),(make_file,make_source,patched_make)]:
            if old==new:continue
            backup=path.with_name(path.name+'.ui16_backup')
            if backup.exists():raise ValueError('backup exists: '+str(backup))
        for path,old,new in [(main_file,main_source,patched_main),(cmake_file,cmake_source,patched_cmake),(make_file,make_source,patched_make)]:
            if old!=new:
                shutil.copy2(path,path.with_name(path.name+'.ui16_backup'))
                path.write_text(new,encoding='utf-8',newline='\n')
        print('Applied. Test build: make TANG_BOARD=console138k UI16_UART_SELFTEST=ON')
        return 0
    except (OSError,ValueError) as e:
        print('UI16 firmware apply error: '+str(e),file=sys.stderr);return 1

if __name__=='__main__':sys.exit(main())
