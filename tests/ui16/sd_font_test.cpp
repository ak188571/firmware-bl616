#include "ui16_sd_font.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>
#include <iterator>

static std::vector<std::uint8_t> image;
static bool fail_read=false,fail_seek=false;
static unsigned closes=0;
static unsigned reads=0,seeks=0;
extern "C" FRESULT f_open(FIL *file,const char *path,unsigned char mode) {
    assert(mode==FA_READ);
    if (!path || std::strcmp(path,"sd:/font16.ui16")) return FR_NO_FILE;
    file->position=0;file->size=image.size();return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *out,UINT n,UINT *r) {
    ++reads;
    if (fail_read) return FR_DISK_ERR;
    const auto available=f->position<image.size()?image.size()-f->position:0;
    *r=static_cast<UINT>(available<n?available:n);
    if(*r)std::memcpy(out,image.data()+f->position,*r);
    f->position+=*r;return FR_OK;
}
extern "C" FRESULT f_lseek(FIL *f,FSIZE_t p) {
    ++seeks;
    if(fail_seek || p>image.size())return FR_DISK_ERR;
    f->position=p;return FR_OK;
}
extern "C" FRESULT f_close(FIL *) {++closes;return FR_OK;}
int main(int argc,char**argv) {
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    image={std::istreambuf_iterator<char>(input),{}};
    Ui16SdFont font;
    assert(font.open("sd:/font16.ui16"));
    assert(font.count()>1024);
    reads=seeks=0;
    std::uint8_t rows[32]{};
    assert(font.glyph(0x30d5,rows));
    assert(reads==1 && seeks==1);
    const std::uint8_t preserved=rows[0];
    assert(!font.glyph(0x10ffff,rows));
    assert(reads==1 && seeks==1);
    assert(rows[0]==preserved);
    fail_seek=true;assert(!font.glyph(0x30d5,rows));assert(!font.ready());fail_seek=false;
    assert(font.open("sd:/font16.ui16"));
    font.close();
    auto broken=image;broken.back()^=1;image=broken;
    assert(!font.open("sd:/font16.ui16"));assert(!font.ready());
    image.resize(image.size()-1);assert(!font.open("sd:/font16.ui16"));
    image.clear();assert(!font.open("sd:/font16.ui16"));
    fail_read=true;assert(!font.open("sd:/font16.ui16"));
    assert(!font.open("sd:/absent.ui16"));
    assert(closes>=5);
    std::puts("BL616 FatFs SD font reader: CRC, Unicode, RAM index, one-record lookup, I/O failures, fallback OK");
}
