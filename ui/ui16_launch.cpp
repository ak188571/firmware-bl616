#ifdef UI16_SYSTEM_MENU
#include "ui16_launch.h"
#include "ui16_game_list.h"

namespace {
bool valid_path(const std::string &path,bool empty_ok)
{
    if(path.empty())return empty_ok;
    if(path.size()>511)return false;
    std::size_t pos=0;int depth=0;
    while(pos<path.size()){
        if(++depth>16)return false;
        const auto end=path.find('/',pos);
        if(!ui16_valid_component(path.substr(pos,end==std::string::npos?end:end-pos)))return false;
        if(end==std::string::npos)return true;
        pos=end+1;
    }
    return false;
}
}
const char *ui16_launch_error_text(Ui16LaunchError error)
{
    switch(error){
    case Ui16LaunchError::None:return "";
    case Ui16LaunchError::InvalidSelection:return "INVALID ROM";
    case Ui16LaunchError::MissingRom:return "ROM NOT FOUND";
    case Ui16LaunchError::MissingCore:return "CORE NOT FOUND";
    case Ui16LaunchError::MissingMonitor:return "MONITOR NOT FOUND";
    case Ui16LaunchError::ProgramFailed:return "CORE WRITE ERROR";
    case Ui16LaunchError::CoreTimeout:return "CORE ID TIMEOUT";
    case Ui16LaunchError::OsdUnsupported:return "REBUILD GAME CORE";
    case Ui16LaunchError::RomFailed:return "ROM LOAD ERROR";
    case Ui16LaunchError::MonitorProgramFailed:return "MONITOR WRITE ERROR";
    case Ui16LaunchError::MonitorTimeout:return "MONITOR ID TIMEOUT";
    }
    return "LAUNCH ERROR";
}
Ui16LaunchError Ui16Runtime::launch(const char *drive,const Ui16LaunchTarget &target,const Ui16LaunchRequest &request)
{
    if(!monitor_ready_||!drive||(std::string(drive)!="sd:"&&std::string(drive)!="usb:")||
       target.core_id!=request.core_id||target.core_id<=0||!valid_path(target.rom_dir,false)||
       !valid_path(request.relative_path,true)||!ui16_valid_component(request.filename)||
       !ui16_is_rom_file(target.core_id,request.filename.c_str())||!ui16_valid_component(target.core_file)||
       !ops_.file_ready||!ops_.find_core||!ops_.program||!ops_.wait_core||!ops_.load_rom||!ops_.display_owner)
        return Ui16LaunchError::InvalidSelection;
    const std::string path=std::string(drive)+target.rom_dir+
        (request.relative_path.empty()?"":"/"+request.relative_path)+"/"+request.filename;
    if(!ops_.file_ready(path.c_str(),ops_.context))return Ui16LaunchError::MissingRom;
    std::string core,monitor;
    if(!ops_.find_core(target.core_file.c_str(),core,ops_.context))return Ui16LaunchError::MissingCore;
    // Verify the return route before replacing the monitor FPGA image.
    if(!ops_.find_core("monitor.bin",monitor,ops_.context))return Ui16LaunchError::MissingMonitor;
    monitor_ready_=false;
    ops_.display_owner(false,ops_.context);
    if(!ops_.program(core.c_str(),ops_.context))return Ui16LaunchError::ProgramFailed;
    if(!ops_.wait_core(target.core_id,ops_.context))return Ui16LaunchError::CoreTimeout;
    if(ops_.osd_ready&&!ops_.osd_ready(ops_.context))return Ui16LaunchError::OsdUnsupported;
    if(!ops_.load_rom(target.core_id,path.c_str(),ops_.context))return Ui16LaunchError::RomFailed;
    return Ui16LaunchError::None;
}
Ui16LaunchError Ui16Runtime::restore_monitor()
{
    if(monitor_ready_)return Ui16LaunchError::None;
    std::string path;
    if(!ops_.find_core("monitor.bin",path,ops_.context))return Ui16LaunchError::MissingMonitor;
    ops_.display_owner(false,ops_.context);
    if(!ops_.program(path.c_str(),ops_.context))return Ui16LaunchError::MonitorProgramFailed;
    if(!ops_.wait_core(0,ops_.context))return Ui16LaunchError::MonitorTimeout;
    ops_.display_owner(true,ops_.context);
    monitor_ready_=true;
    return Ui16LaunchError::None;
}
#endif
