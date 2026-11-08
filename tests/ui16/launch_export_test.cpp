#include "tc_launch_export.h"
#include "tc_launch_trace.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
static std::uint64_t now=100;
std::uint64_t tc_launch_now_us(){return now;}
static std::map<std::string,std::string> files;
static std::string opened;
static int opens=0,closes=0;
static bool short_write=false,bad_sync=false,bad_close=false,bad_open=false;
extern "C" FRESULT f_open(FIL *,const char *path,unsigned char mode){
    assert(mode==(FA_WRITE|FA_CREATE_NEW));++opens;
    if(bad_open)return FR_DISK_ERR;
    if(files.count(path))return FR_EXIST;
    opened=path;files[path]="";return FR_OK;
}
extern "C" FRESULT f_write(FIL *,const void *buffer,UINT n,UINT *written){
    *written=short_write?n-1:n;
    files[opened].append(static_cast<const char*>(buffer),*written);return FR_OK;
}
extern "C" FRESULT f_sync(FIL *){return bad_sync?FR_DISK_ERR:FR_OK;}
extern "C" FRESULT f_close(FIL *){++closes;return bad_close?FR_DISK_ERR:FR_OK;}
int main(){
    FIL file{};alignas(64) char buffer[8192];char path[32];
    tc_launch_begin();
    assert(!tc_launch_export("sd:snes/g.sfc",2,file,buffer,sizeof(buffer),path,sizeof(path))&&opens==0);
    now+=500000;tc_launch_mark(TcLaunchEvent::NoticeDone);
    tc_launch_block(2097152,1000,10500000);tc_launch_success();tc_launch_finish();
    files["sd:/uart-0001.txt"]="USER DATA";
    assert(tc_launch_export("sd:snes/g\n.sfc",2,file,buffer,sizeof(buffer),path,sizeof(path)));
    assert(std::string(path)=="sd:/uart-0002.txt"&&files["sd:/uart-0001.txt"]=="USER DATA");
    const auto &text=files[path];
    assert(text.find("rom_hex=73643a736e65732f670a2e736663\n")!=std::string::npos);
    assert(text.find("rom_bytes=2097152\n")!=std::string::npos);
    assert(text.find("NoticeDone_us=500000\n")!=std::string::npos);
    assert(text.substr(text.size()-6)=="END=1\n"&&closes==1);
    const int before=opens;
    assert(!tc_launch_export("x",1,file,buffer,sizeof(buffer),path,sizeof(path))&&opens==before);
    assert(!tc_launch_export("x",2,file,buffer,100,path,sizeof(path))&&opens==before);
    for(int failure=0;failure<4;++failure){
        short_write=failure==0;bad_sync=failure==1;bad_close=failure==2;bad_open=failure==3;
        const auto old_closes=closes;
        assert(!tc_launch_export("x",2,file,buffer,sizeof(buffer),path,sizeof(path)));
        assert(closes==old_closes+(bad_open?0:1));
    }
    short_write=bad_sync=bad_close=bad_open=false;
    tc_launch_record.rom_baud=8000000;tc_launch_record.requests=2057;tc_launch_record.retries=2;
    tc_launch_record.stream_crc=0x12345678;tc_launch_record.end_ack_us=3000000;
    tc_launch_record.rom_window=2;
    assert(tc_launch_export("sd:snes/fast.sfc",2,file,buffer,sizeof(buffer),path,sizeof(path)));
    assert(files[path].find("TC_ROMXFR1_TRACE_V2\ncore=2\nbaud=8000000\n")==0);
    assert(files[path].find("window=2\n")!=std::string::npos);
    assert(files[path].find("stream_boundary=DATA_ACK\nrequests=2057\nretries=2\nstream_crc=12345678\nEndAck_us=3000000\n")!=std::string::npos);
    std::puts("Trace export: post-measurement guard, exclusive new files, exact metadata, short write/sync/close/open errors passed");
}
