#pragma once
#include "ui16_rom_id.h"
#include "ui16_crt_settings.h"

enum class Ui16CrtReadState { NoId, Missing, User, Reset, Recovered, Bad, Io, Future, Conflict };
struct Ui16CrtRestore {
    Ui16CrtReadState state=Ui16CrtReadState::NoId;
    Ui16CrtRecommendation override_value;
};
inline const char *ui16_crt_read_name(Ui16CrtReadState state){
    switch(state){
    case Ui16CrtReadState::Missing:return "MISSING";
    case Ui16CrtReadState::User:return "USER";
    case Ui16CrtReadState::Reset:return "RESET";
    case Ui16CrtReadState::Recovered:return "RECOVERED";
    case Ui16CrtReadState::Bad:return "BAD SAVE";
    case Ui16CrtReadState::Io:return "READ ERROR";
    case Ui16CrtReadState::Future:return "NEW SCHEMA";
    case Ui16CrtReadState::Conflict:return "CONFLICT";
    default:return "NO ROM ID";
    }
}
