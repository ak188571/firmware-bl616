#pragma once
#include <string>

struct Ui16LaunchRequest {
    int core_id=0;
    std::string relative_path;
    std::string filename; // Original filename, never the translated display title.
};

struct Ui16LaunchTarget {
    int core_id=0;
    std::string rom_dir;
    std::string core_file;
};

enum class Ui16LaunchError { None, InvalidSelection, MissingRom, MissingCore, MissingMonitor,
    ProgramFailed, CoreTimeout, RomFailed, OsdUnsupported, MonitorProgramFailed, MonitorTimeout };
const char *ui16_launch_error_text(Ui16LaunchError error);

// Hardware adapters perform bounded waits and reuse the existing ROM loader.
struct Ui16LaunchOps {
    void *context=nullptr;
    bool (*file_ready)(const char *path,void *)=nullptr;
    bool (*find_core)(const char *name,std::string &path,void *)=nullptr;
    bool (*program)(const char *path,void *)=nullptr;
    bool (*wait_core)(int expected,void *)=nullptr;
    bool (*load_rom)(int core_id,const char *path,void *)=nullptr;
    void (*display_owner)(bool ui16,void *)=nullptr;
    bool (*osd_ready)(void *)=nullptr;
};

class Ui16Runtime {
public:
    explicit Ui16Runtime(Ui16LaunchOps ops):ops_(ops){}
    Ui16LaunchError launch(const char *drive,const Ui16LaunchTarget &target,const Ui16LaunchRequest &request);
    Ui16LaunchError restore_monitor();
    bool monitor_ready() const {return monitor_ready_;}
private:
    Ui16LaunchOps ops_;
    bool monitor_ready_=true;
};
