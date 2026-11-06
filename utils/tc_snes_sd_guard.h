#pragma once
// Scope is held under the application storage lease. The fault latch survives
// scope exit; power-on initialization is the only way to clear it.
bool tc_snes_sd_scope(bool active);
bool tc_snes_sd_faulted();
struct TcSnesSdScope {
    bool previous;
    explicit TcSnesSdScope(bool active):previous(tc_snes_sd_scope(active)){}
    ~TcSnesSdScope(){tc_snes_sd_scope(previous);}
    TcSnesSdScope(const TcSnesSdScope &)=delete;
    TcSnesSdScope &operator=(const TcSnesSdScope &)=delete;
};
