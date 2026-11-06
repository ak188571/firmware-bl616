#ifdef UI16_SYSTEM_MENU
#include "ui16_system_osd.h"
#include "menu_manager.h"

namespace {
struct SystemOsd:Menu {
    Ui16SystemAction &action;
    bool can_restart;
    SystemOsd(Ui16SystemAction &result,bool supported):action(result),can_restart(supported){}
    void render()override{
        overlay_clear();
        overlay_cursor(0,0);overlay_printf("Restart Game");
        overlay_cursor(0,1);overlay_printf("Return to Game List");
        if(!can_restart){overlay_cursor(0,3);overlay_printf("Restart needs updated core");}
    }
    std::vector<int> get_options()override{return {0,1};}
    int osd_width()const override{return can_restart?19:26;}
    bool on_choose(int index)override{
        if(index==0){
            if(!can_restart)return false;
            action=Ui16SystemAction::Restart;return true;
        }
        if(index==1){action=Ui16SystemAction::ReturnToList;return true;}
        return false;
    }
};
}
Menu *ui16_create_system_osd(Ui16SystemAction &action,bool can_restart){
    action=Ui16SystemAction::None;
    return new SystemOsd(action,can_restart);
}
#endif
