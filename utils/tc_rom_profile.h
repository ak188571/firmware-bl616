#pragma once
#include <cstddef>
#include <cstdint>

// Optional, transport-independent observation. Never affects framing, credits,
// retries, CRC values or sink completion. The observer owns its clock/accounting.
namespace TcRom {
enum class Activity : unsigned {
    Cpu, SourceCopy, FrameCrc, StreamCrc, ReplyCrc, SdRead,
    TxSetup, TxWait, TxDrain, AckProcess, AckWait, Count
};
constexpr unsigned ActivityCount=static_cast<unsigned>(Activity::Count);
struct Profile {
    void *context=nullptr;
    void (*enter)(void *,Activity,std::size_t)=nullptr;
    void (*leave)(void *,Activity)=nullptr;
};
class ProfileScope {
public:
    ProfileScope(Profile *p,Activity a,std::size_t units=0,bool begin=true):p_(p),a_(a),units_(units){if(begin)start();}
    ~ProfileScope(){stop();}
    void start(){if(!on_&&p_&&p_->enter&&p_->leave){p_->enter(p_->context,a_,units_);on_=true;}}
    void stop(){if(on_){p_->leave(p_->context,a_);on_=false;}}
    ProfileScope(const ProfileScope &)=delete;
    ProfileScope &operator=(const ProfileScope &)=delete;
private:
    Profile *p_;Activity a_;std::size_t units_;bool on_=false;
};
}
