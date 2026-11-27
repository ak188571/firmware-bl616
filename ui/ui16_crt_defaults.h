#pragma once
#include "ui16_crt_settings.h"

enum class Ui16CrtDefaultsState { Missing, Ready, Bad, Io, Unavailable };
enum class Ui16CrtDefaultsSource { Builtin, Global, System };
struct Ui16CrtDefaults {
    Ui16CrtDefaultsState state=Ui16CrtDefaultsState::Missing;
    Ui16CrtDefaultsSource source=Ui16CrtDefaultsSource::Builtin;
    Ui16CrtRecommendation recommendation;
    bool named_items=false; // crt_v2 permits CSV to override only its three fields.
};
Ui16CrtDefaults ui16_parse_crt_defaults(std::string_view text,unsigned system);
inline const char *ui16_crt_defaults_state_name(Ui16CrtDefaultsState state){
    switch(state){
    case Ui16CrtDefaultsState::Missing:return "MISSING";
    case Ui16CrtDefaultsState::Ready:return "OK";
    case Ui16CrtDefaultsState::Bad:return "BAD FILE";
    case Ui16CrtDefaultsState::Io:return "READ ERROR";
    default:return "UNAVAILABLE";
    }
}
