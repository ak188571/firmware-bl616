#pragma once
#include "ui16_crt_settings.h"
#include <string>
#include <cstddef>

std::uint32_t ui16_crt_crc(std::uint32_t state,const void *data,std::size_t size);
class Ui16CrtStore {
public:
    void prepare(int system,const char *path,void (*yield)()=nullptr);
    bool dirty(const Ui16CrtSettings &value)const;
    bool save(const Ui16CrtSettings &value);
    bool ready=false;
    Ui16CrtSettings initial,recommended;
    const char *recommended_source="GLOBAL";
    const char *notice="OK";
    bool user_override=false;
private:
    int system_=0;
    std::uint32_t crc_=0;
    std::uint64_t size_=0;
    std::string directory_,prefix_;
    Ui16CrtSettings persisted_;
};
