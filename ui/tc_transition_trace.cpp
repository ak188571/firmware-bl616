#include "tc_transition_trace.h"
#ifdef TC_LAUNCH_TRACE
TcTransitionRecord tc_transition_records[2];
TcTransitionRecord *tc_transition_active=nullptr;
void tc_transition_begin(unsigned phase){
    if(phase>1 || tc_transition_active)return;
    if(phase==0){tc_transition_records[0]={};tc_transition_records[1]={};}
    auto &record=tc_transition_records[phase];
    record={};record.started=true;record.origin_us=tc_launch_now_us();
    tc_transition_active=&record;
}
void tc_transition_finish(){
    if(!tc_transition_active)return;
    tc_transition_active->wall_us=tc_launch_now_us()-tc_transition_active->origin_us;
    tc_transition_active->finished=true;
    tc_transition_active=nullptr;
}
#endif
