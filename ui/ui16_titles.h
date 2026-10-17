#pragma once
#include "ui16_game_list.h"
#include <cstddef>

inline constexpr std::size_t UI16_TSV_MAX_BYTES = 262144;
inline constexpr std::size_t UI16_TITLE_MAX_BYTES = 512;

class Ui16TitlesParser {
public:
    explicit Ui16TitlesParser(Ui16GameList &games);
    bool feed(const char *data, std::size_t length);
    bool finish();
private:
    bool line();
    Ui16GameList &games_;
    std::vector<std::string> pending_;
    std::vector<bool> seen_;
    std::string line_;
    std::size_t total_ = 0;
    bool first_ = true;
    bool failed_ = false;
};

void ui16_load_titles(const char *path, Ui16GameList &games);
