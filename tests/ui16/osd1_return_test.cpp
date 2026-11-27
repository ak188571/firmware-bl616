#include "ui16_launch.h"
#include "ui16_system_osd.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <vector>

static int selected_policy(int id, const char *drv, Ui16SystemAction system_action, bool return_to_menu) {
    Ui16LaunchRequest request{};
    request.core_id = id;
    int short_wait_core = 0;
    (void)system_action; // Only the Console138K build inspects the NES action.
    const auto tc_transition_begin = [](int) {};
    do {
#include "osd1_return_selection.inc"
    } while (false);
    return short_wait_core;
}

static std::vector<Ui16LaunchError> results;
static std::vector<MonitorBootWait> attempts;
static bool waiting_for_input;
static constexpr unsigned UI16_BUTTON_CONFIRM = 0x0101;
static void ui16_wait_release() {}
static unsigned ui16_buttons() { waiting_for_input = true; return UI16_BUTTON_CONFIRM; }
static void delay(unsigned) {}
static void overlay_status(const char *, ...) {}
#define TC_DEBUG_PRINT(...) ((void)0)
const char *ui16_launch_error_text(Ui16LaunchError) { return "test error"; }
Ui16LaunchError Ui16Runtime::restore_monitor(MonitorBootWait wait) {
    assert(attempts.size() < results.size());
    attempts.push_back(wait);
    return results[attempts.size() - 1];
}
struct Ui16Browser {
    unsigned resumed = 0;
    void resume_input(unsigned buttons) { assert(!buttons); ++resumed; }
};
#include "osd1_restore_menu.inc"

static void check(int policy, std::initializer_list<Ui16LaunchError> outcomes,
                  std::initializer_list<MonitorBootWait> expected, bool manual) {
    Ui16Runtime runtime({});
    Ui16Browser browser;
    results = outcomes;
    attempts.clear();
    waiting_for_input = false;
    ui16_restore_menu(runtime, browser, policy);
    assert(attempts == std::vector<MonitorBootWait>(expected));
    assert(waiting_for_input == manual && browser.resumed == 1);
}

int main() {
    using W = MonitorBootWait;
    using A = Ui16SystemAction;
    using E = Ui16LaunchError;
    const int nes = selected_policy(1, "sd:", A::ReturnToList, true);
    const int snes = selected_policy(2, "sd:", A::ReturnToList, true);
    assert(snes == 2);
    assert(selected_policy(1, "usb:", A::ReturnToList, true) == 0);
    assert(selected_policy(1, "sd:", A::None, true) == 0);
    assert(selected_policy(1, "sd:", A::ReturnToList, false) == 0);
    assert(selected_policy(3, "sd:", A::ReturnToList, true) == 0);
#ifdef TANG_CONSOLE138K
    assert(nes == 1);
    check(nes, {E::None}, {W::Checked500ms}, false);
    check(nes, {E::MonitorProgramFailed, E::None}, {W::Checked500ms, W::Full}, false);
    check(nes, {E::MonitorTimeout, E::None}, {W::Checked500ms, W::Full}, false);
    check(nes, {E::MonitorTimeout, E::MonitorTimeout, E::None}, {W::Checked500ms, W::Full, W::Full}, true);
#else
    assert(nes == 0);
    check(nes, {E::None}, {W::Full}, false);
#endif
    check(0, {E::MonitorProgramFailed, E::None}, {W::Full, W::Full}, true);
    check(snes, {E::MonitorProgramFailed, E::None}, {W::Checked1s, W::Full}, false);
    check(snes, {E::MonitorTimeout, E::None}, {W::Checked1s, W::Full}, true);
    std::puts("PASS OSD1 return selection, single automatic retry and button-driven recovery");
}
