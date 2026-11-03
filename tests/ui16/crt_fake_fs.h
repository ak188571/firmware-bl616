#pragma once
extern "C" {
#include "ff.h"
}
#include <map>
#include <string>
#include <cstring>
#include <algorithm>
namespace fake_fs {
std::map<std::string,std::string> files;
std::map<FIL *,std::string> handles;
std::map<FIL *,bool> writing;
bool write_error=false,read_error=false,sync_error=false,close_error=false,mkdir_error=false;
bool corrupt_close=false,write_close_error=false,write_result_error=false;
int short_write=-1,writes=0;
void reset(){files.clear();handles.clear();writing.clear();write_error=read_error=sync_error=close_error=mkdir_error=corrupt_close=write_close_error=write_result_error=false;short_write=-1;writes=0;}
}
extern "C" FRESULT f_open(FIL *f,const char *name,unsigned char mode){
    const std::string path=name;
    if(mode&FA_WRITE){if(fake_fs::write_error)return FR_DISK_ERR;fake_fs::files[path].clear();}
    else if(fake_fs::read_error)return FR_DISK_ERR;
    else if(!fake_fs::files.count(path))return FR_NO_FILE;
    f->position=0;f->size=fake_fs::files[path].size();fake_fs::handles[f]=path;fake_fs::writing[f]=(mode&FA_WRITE)!=0;return FR_OK;
}
extern "C" FRESULT f_read(FIL *f,void *buffer,UINT length,UINT *n){
    if(fake_fs::read_error)return FR_DISK_ERR;
    const auto &text=fake_fs::files.at(fake_fs::handles.at(f));
    *n=static_cast<UINT>(std::min<std::size_t>(length,text.size()-f->position));
    std::memcpy(buffer,text.data()+f->position,*n);f->position+=*n;return FR_OK;
}
extern "C" FRESULT f_write(FIL *f,const void *buffer,UINT length,UINT *n){
    if(fake_fs::write_result_error){*n=0;return FR_DISK_ERR;}
    ++fake_fs::writes;*n=fake_fs::short_write<0?length:std::min(length,UINT(fake_fs::short_write));
    auto &text=fake_fs::files.at(fake_fs::handles.at(f));text.append(static_cast<const char *>(buffer),*n);f->position+=*n;f->size=text.size();return FR_OK;
}
extern "C" FRESULT f_sync(FIL *){return fake_fs::sync_error?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_close(FIL *f){
    const bool failed=fake_fs::close_error||(fake_fs::write_close_error&&fake_fs::writing[f]);
    if(fake_fs::corrupt_close&&fake_fs::writing[f]){fake_fs::files.at(fake_fs::handles.at(f))[0]^=1;fake_fs::corrupt_close=false;}
    fake_fs::writing.erase(f);fake_fs::handles.erase(f);return failed?FR_DISK_ERR:FR_OK;
}
extern "C" FRESULT f_mkdir(const char *){return fake_fs::mkdir_error?FR_DISK_ERR:FR_OK;}
