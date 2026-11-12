#pragma once
#include <string>
#include "ui16_crt_settings.h"
#include "tc_launch_trace.h"

struct Ui16LaunchRequest {
    int core_id=0;
    std::string relative_path;
    std::string filename; // Original filename, never the translated display title.
    Ui16CrtRecommendation crt_recommendation{}; // Snapshot for this confirmed ROM.
};

struct Ui16LaunchTarget {
    int core_id=0;
    std::string rom_dir;
    std::string core_file;
};

enum class Ui16LaunchError { None, InvalidSelection, MissingRom, MissingCore, MissingMonitor,
    ProgramFailed, CoreTimeout, RomFailed, OsdUnsupported, MonitorProgramFailed, MonitorTimeout,
    StartingDisplayFailed };
const char *ui16_launch_error_text(Ui16LaunchError error);

// Hardware adapters perform bounded waits and reuse the existing ROM loader.
struct Ui16LaunchOps {
    void *context=nullptr;
    bool (*file_ready)(const char *path,void *)=nullptr;
    bool (*find_core)(const char *name,std::string &path,void *)=nullptr;
    bool (*program)(const char *path,bool try_short_boot_wait,void *)=nullptr;
    bool (*wait_core)(int expected,void *)=nullptr;
    bool (*load_rom)(int core_id,const char *path,void *)=nullptr;
    void (*display_owner)(bool ui16,void *)=nullptr;
    bool (*osd_ready)(void *)=nullptr;
    // Finish the monitor's launch notice before handing the FPGA to the game.
    bool (*starting)(int,void *)=nullptr;
};

class Ui16Runtime {
public:
    explicit Ui16Runtime(Ui16LaunchOps ops):ops_(ops){}
    Ui16LaunchError launch(const char *drive,const Ui16LaunchTarget &target,const Ui16LaunchRequest &request);
    Ui16LaunchError restore_monitor(bool try_short_boot_wait=false);
    bool monitor_ready() const {return monitor_ready_;}
private:
    Ui16LaunchOps ops_;
    bool monitor_ready_=true;
};
