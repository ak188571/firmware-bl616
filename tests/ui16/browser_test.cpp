#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "ui16_browser.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct MockEntry { std::string name; unsigned char attr = 0; };
static std::vector<MockEntry> directory;
static std::string opened_path;
static int opens = 0, closes = 0, read_error_at = -1, last_core = 0;
static bool open_error = false, close_error = false;
extern "C" FRESULT f_opendir(DIR *dir, const char *path) {
    ++opens; opened_path = path;
    dir->position = 0;
    return open_error ? FR_NO_PATH : FR_OK;
}
extern "C" FRESULT f_readdir(DIR *dir, FILINFO *info) {
    if (static_cast<int>(dir->position) == read_error_at) return FR_DISK_ERR;
    if (dir->position == directory.size()) { info->fname[0] = 0; return FR_OK; }
    const auto &entry = directory[dir->position++];
    std::snprintf(info->fname,sizeof(info->fname),"%s",entry.name.c_str());
    info->fattrib = entry.attr;
    return FR_OK;
}
extern "C" FRESULT f_closedir(DIR *) { ++closes; return close_error ? FR_DISK_ERR : FR_OK; }

int critical_depth = 0;
static int sections = 0;
static std::vector<unsigned char> bytes;
void enter_critical() { assert(critical_depth==0); ++critical_depth; ++sections; }
void leave_critical() { assert(critical_depth==1); --critical_depth; }
void fpga_tx_byte(std::uint8_t value) { assert(critical_depth==1); bytes.push_back(value); }
void fpga_tx_header(int cmd,int len) {
    fpga_tx_byte(0xaa); fpga_tx_byte(len>>8); fpga_tx_byte(len); fpga_tx_byte(cmd);
}
static Ui16GameList load(int core,const char *,void *context) {
    last_core = core;
    return ui16_read_game_list(static_cast<const char*>(context),core);
}
static void reset_fs() {
    directory.clear(); opens=closes=0; read_error_at=-1; open_error=close_error=false;
}
static void save_frame(const char *name,const std::uint16_t (&cells)[CELL_COUNT]) {
    assert(bytes.size()==994 && critical_depth==0 && sections==14);
    std::size_t pos=0;
    for(int y=0;y<14;++y) {
        for(int b:{0xaa,0,68,15,y,0,32}) assert(bytes[pos++]==b);
        for(int x=0;x<32;++x) {
            assert(bytes[pos++]==(cells[y*32+x]&255));
            assert(bytes[pos++]==ui16_wire_high(cells[y*32+x]));
        }
    }
    std::ofstream file(std::string("build-ui16-host/")+name+".hex");
    char word[16];
    for(int i=0;i<512;++i) { std::snprintf(word,sizeof(word),"%03x\n",i<CELL_COUNT?cells[i]:0); file<<word; }
    bytes.clear(); sections=0;
}

int main() {
    unsigned gid_x=0;for(auto e:ui16_font_map)if(e.codepoint=='X')gid_x=e.glyph;
    const struct { int id; const char *name; } good[] = {
        {1,"ABC.NES"},{2,"ABC.sfc"},{2,"ABC.SMC"},{3,"ABC.gba"},
        {4,"ABC.bin"},{4,"ABC.MD"},{5,"ABC.sms"},{6,"ABC.img"}
    };
    for(const auto &sample:good) assert(ui16_is_rom_file(sample.id,sample.name));
    for(const auto &sample:good) assert(!ui16_is_rom_file(999,sample.name));
    assert(!ui16_is_rom_file(1,"ABC.sfc") && !ui16_is_rom_file(1,"ABC.nes.bak"));
    assert(!ui16_is_rom_file(1,".nes") && !ui16_is_rom_file(1,nullptr));
    assert(!ui16_is_rom_file(1,"ABC") && !ui16_is_rom_file(1,""));

    reset_fs();
    const std::string long_name=std::string(200,'A')+".nes";
    directory={{"ABC.nes"},{"DEF.NES"},{"readme.txt"},{"wrong.sfc"},
               {"SUB.nes",AM_DIR},{"hidden.nes",AM_HID},{"system.nes",AM_SYS},{long_name}};
    auto games=ui16_read_game_list("sd:nes",1);
    assert(games.status==Ui16ListStatus::Ready && games.names.size()==4 && !games.more);
    assert(games.names[0]=="ABC.nes" && games.names[1]=="DEF.NES" && games.names[3]==long_name);
    assert(opened_path=="sd:nes" && opens==1 && closes==1);
    reset_fs(); for(int i=0;i<UI16_GAME_CAPACITY;++i) directory.push_back({"ABC.nes"});
    games=ui16_read_game_list("sd:nes",1);
    assert(games.names.size()==UI16_GAME_CAPACITY && !games.more);
    directory.push_back({"DEF.nes"});
    games=ui16_read_game_list("usb:nes",1);
    assert(games.names.size()==UI16_GAME_CAPACITY && games.more && opened_path=="usb:nes");
    reset_fs(); games=ui16_read_game_list("sd:nes",1);
    assert(games.status==Ui16ListStatus::Empty && games.names.empty() && closes==1);
    reset_fs(); open_error=true; games=ui16_read_game_list("sd:missing",1);
    assert(games.status==Ui16ListStatus::DirectoryError && closes==0);
    reset_fs(); directory={{"ABC.nes"}}; read_error_at=1;
    games=ui16_read_game_list("sd:nes",1);
    assert(games.status==Ui16ListStatus::ReadError && games.names.empty() && closes==1);
    reset_fs(); directory={{"ABC.nes"}}; close_error=true;
    games=ui16_read_game_list("sd:nes",1);
    assert(games.status==Ui16ListStatus::ReadError && games.names.empty());

    constexpr auto b=UI16_BUTTON_CONFIRM,a=UI16_BUTTON_BACK,down=UI16_BUTTON_DOWN;
    const Ui16MenuEntry entries[]={{1,"NES"},{2,"SNES"},{-1,"CORES"},{-2,"OPTIONS"}};
    reset_fs(); directory={{"ABC.sfc"},{"DEF.SMC"}};
    Ui16Browser browser(entries,4);
    assert(browser.update(down,0,load) && browser.selected_system()==1);
    assert(!browser.update(0,10,load));
    assert(browser.update(b,20,load,(void*)"sd:snes") && browser.in_games());
    assert(last_core==2 && browser.selected_system()==1 && browser.selected_file()==0);
    assert(!browser.update(b,500,load,(void*)"sd:snes") && opens==1); // held B does not reload
    browser.update(0,510,load);
    assert(browser.update(down,520,load) && browser.selected_file()==1);
    assert(browser.update(a,530,load) && !browser.in_games() && browser.selected_system()==1);
    assert(!browser.update(a,540,load)); // held A does not affect the system screen
    assert(!browser.update(a|b,550,load)); // simultaneous A+B does not enter
    browser.update(0,560,load);
    assert(browser.update(b,570,load,(void*)"sd:snes") && browser.selected_file()==0 && opens==2);
    browser.update(0,580,load);
    assert(!browser.update(b,590,load) && opens==2); // game B never loads/reopens ROMs
    browser.update(0,600,load);
    browser.update(a,610,load);
    browser.update(0,620,load);
    browser.update(down,630,load); // CORES
    browser.update(0,640,load);
    assert(!browser.update(b,650,load) && !browser.in_games() && opens==2);
    browser.update(0,660,load); browser.update(down,670,load); // OPTIONS
    browser.update(0,680,load);
    assert(!browser.update(b,690,load) && !browser.in_games() && opens==2);
    // Empty and error screens always accept Back.
    for(bool error:{false,true}) {
        reset_fs(); open_error=error;
        Ui16Browser no_games(entries,4);
        assert(no_games.update(b,0,load,(void*)"sd:nes"));
        assert(no_games.in_games() && no_games.selected_file()==-1);
        assert(no_games.game_list().status==(error?Ui16ListStatus::DirectoryError:Ui16ListStatus::Empty));
        assert(!no_games.update(down,10,load));
        assert(no_games.update(a,20,load) && !no_games.in_games());
    }
    Ui16Browser empty(nullptr,0);
    assert(!empty.update(b,0,load) && empty.active_system()==nullptr);

    // Layout/transport: only one file row selected, stale system rows cleared.
    games={}; games.names={"ABC.nes","DEF.NES"}; games.status=Ui16ListStatus::Ready;
    std::uint16_t cells[CELL_COUNT];
    ui16_build_game_menu("NES",games,1,cells);
    assert(cells[3*32+2]==62 && cells[4*32+2]==(55|4096));
    for(int i=0;i<CELL_COUNT;++i) {
        if(i/32!=4) assert((cells[i]&4096)==0);
        if(i/32>=5 && i/32<13) assert(cells[i]==0);
    }
    ui16_send_game_menu("NES",games,1); save_frame("game_list",cells);
    games.more=true;
    ui16_build_game_menu("SNES",games,0,cells);
    assert(cells[13*32+12]!=0); // MORE FILES hint
    ui16_send_game_menu("SNES",games,0); save_frame("game_more",cells);
    for(auto status:{Ui16ListStatus::Empty,Ui16ListStatus::DirectoryError,Ui16ListStatus::ReadError}) {
        games={};games.status=status;
        ui16_build_game_menu("NES",games,-1,cells);
        for(auto cell:cells) assert((cell&4096)==0);
        assert(cells[3*32+2]!=0 && cells[13*32+2]==gid_x); // message plus A BACK
        ui16_send_game_menu("NES",games,-1);
        save_frame(status==Ui16ListStatus::Empty?"game_empty":status==Ui16ListStatus::DirectoryError?"game_directory_error":"game_read_error",cells);
    }
    std::puts("Stage 3 tests passed: ROM filters, bounded FatFs/LFN, errors/close, screen transitions, button edges, selection restore, empty/error Back, layouts and UART frames");
}
