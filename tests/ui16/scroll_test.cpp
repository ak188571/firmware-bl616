#include "ui16_browser.h"
#include "ui16_titles.h"
#include "ui16_font_map.h"
#include "ui16_cell.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <fstream>

struct Entry {std::string name;unsigned char attr=0;};
static std::map<std::string,std::vector<Entry>> dirs;
static std::map<std::string,std::string> csvs;
static std::string active_dir,active_file;
static std::vector<std::string> loaded;
extern "C" FRESULT f_opendir(DIR *d,const char *path){active_dir=path;d->position=0;return dirs.count(path)?FR_OK:FR_NO_PATH;}
extern "C" FRESULT f_readdir(DIR *d,FILINFO *f){
    const auto &entries=dirs.at(active_dir);
    if(d->position==entries.size()){f->fname[0]=0;return FR_OK;}
    const auto &e=entries[d->position++];std::snprintf(f->fname,sizeof(f->fname),"%s",e.name.c_str());f->fattrib=e.attr;return FR_OK;
}
extern "C" FRESULT f_closedir(DIR *){return FR_OK;}
extern "C" FRESULT f_open(FIL *f,const char *path,unsigned char mode){
    assert(mode==FA_READ);active_file=path;f->position=0;
    assert(active_file.size()>=11&&active_file.substr(active_file.size()-11)=="/titles.csv");
    return csvs.count(path)?static_cast<FRESULT>(FR_OK):static_cast<FRESULT>(FR_NO_FILE);
}
extern "C" FRESULT f_read(FIL *f,void *buffer,UINT size,UINT *read){
    const auto &data=csvs.at(active_file);*read=static_cast<UINT>(std::min<std::size_t>(size,data.size()-f->position));
    std::memcpy(buffer,data.data()+f->position,*read);f->position+=*read;return FR_OK;
}
extern "C" FRESULT f_close(FIL *){return FR_OK;}
int critical_depth=0;static std::vector<unsigned char> wire;
void enter_critical(){assert(!critical_depth);++critical_depth;}
void leave_critical(){assert(critical_depth==1);--critical_depth;}
void fpga_tx_byte(std::uint8_t b){assert(critical_depth==1);wire.push_back(b);}
void fpga_tx_header(int c,int n){fpga_tx_byte(0xaa);fpga_tx_byte(n>>8);fpga_tx_byte(n);fpga_tx_byte(c);}
static Ui16GameList load(int core,const char *relative,void *){
    assert(core==1);const std::string path=std::string("sd:nes")+(*relative?"/":"")+relative;loaded.push_back(path);
    auto g=ui16_read_game_list(path.c_str(),core);ui16_load_titles((path+"/titles.csv").c_str(),g);return g;
}
static unsigned glyph(unsigned cp){for(auto e:ui16_font_map)if(e.codepoint==cp)return e.glyph;assert(false);return 0;}

static void check_frame(const Ui16Browser &b) {
    std::uint16_t cells[CELL_COUNT];
    ui16_build_game_menu(b.heading(),b.game_list(),b.selected_file(),cells,b.file_top());
    int highlighted=0;
    for(int row=0;row<MENU_ROWS;++row) {
        bool select=false;
        for(int x=0;x<32;++x)select|=(cells[row*32+x]&4096)!=0;
        if(select){++highlighted;assert(row==UI16_MENU_FIRST_ROW+b.selected_file()-b.file_top());}
    }
    assert(highlighted==1);
    if(b.selected_file()==9 || b.selected_file()==10 || b.selected_file()==136) {
        const auto path="build-ui16-host/scroll_"+std::to_string(b.selected_file());
        std::ofstream hex(path+".hex");char word[16];
        for(int i=0;i<512;++i){std::snprintf(word,sizeof(word),"%04x\n",i<CELL_COUNT?cells[i]:0);hex<<word;}
    }
    for(int row=0;row<UI16_MENU_CAPACITY;++row) {
        const int index=b.file_top()+row;
        if(index>=static_cast<int>(b.game_list().names.size()))break;
        const auto &g=b.game_list();
        Ui16GameList one;one.status=Ui16ListStatus::Ready;
        one.names={g.names[index]};one.directories={g.directories[index]};
        if(index<static_cast<int>(g.titles.size()))one.titles={g.titles[index]};
        std::uint16_t expected[CELL_COUNT];
        ui16_build_game_menu("",one,index==b.selected_file()?0:-1,expected);
        for(int x=0;x<32;++x)assert(cells[(3+row)*32+x]==expected[3*32+x]);
    }
    assert(cells[13*32+2]==glyph('X') && cells[13*32+3]==glyph('/'));
    assert(cells[13*32+6]==glyph(0x623b) && cells[13*32+8]==glyph(0x308b));
    wire.clear();ui16_send_game_menu(b.heading(),b.game_list(),b.selected_file(),b.file_top());
    assert(wire.size()==994 && critical_depth==0);
    for(int y=0;y<14;++y)for(int x=0;x<32;++x) {
        const auto offset=y*71+7+2*x;
        assert(wire[offset]==(cells[y*32+x]&255));
        assert(wire[offset+1]==ui16_wire_high(cells[y*32+x]));
    }
}
int main() {
    static_assert(UI16_BUTTON_CONFIRM==0x0101 && UI16_BUTTON_BACK==0x0202);
    const Ui16MenuEntry systems[]={{1,"NES"}};
    dirs["sd:nes"]={};
    std::string csv="name\ttitle\n";
    for(int i=0;i<137;++i) {
        const std::string name=i==70?"child":"game"+std::to_string(i)+".nes";
        dirs["sd:nes"].push_back({name,static_cast<unsigned char>(i==70?AM_DIR:0)});
        csv+=name+"\t"+u8"日本"+std::to_string(i)+"\n";
    }
    csvs["sd:nes/titles.csv"]=csv;
    dirs["sd:nes/child"]={{"nested.nes"}};
    Ui16Browser b(systems,1);std::uint32_t t=0;
    auto tap=[&](unsigned button){b.update(0,t+=10,load);return b.update(button,t+=10,load);};
    assert(tap(0x0100)); // A enters; Y no longer confirms.
    assert(b.game_list().names.size()==137 && !b.game_list().more);
    for(int i=0;i<137;++i) {
        assert(b.selected_file()==i && b.file_top()==std::max(0,i-9));
        check_frame(b);
        if(i<136)assert(tap(UI16_BUTTON_DOWN));
    }
    assert(!tap(UI16_BUTTON_DOWN)&&b.selected_file()==136);
    for(int i=135;i>=70;--i)assert(tap(UI16_BUTTON_UP));
    const int top=b.file_top();assert(top==70);
    assert(tap(0x0001)&&b.relative_path()=="child"&&b.file_top()==0);
    assert(!b.update(0x0101,t+=500,load)); // adding A while B is held cannot enter another level
    assert(tap(0x0002)&&b.relative_path().empty()&&b.selected_file()==70&&b.file_top()==top); // Y returns
    check_frame(b);
    // Restore by name after filesystem order changes; adjust viewport only as needed.
    assert(tap(0x0100));
    dirs["sd:nes"].insert(dirs["sd:nes"].begin(),{"inserted.nes"});
    assert(tap(0x0200)&&b.selected_file()==71&&b.file_top()==70); // X returns
    check_frame(b);
    for(int i=70;i>=0;--i){assert(tap(UI16_BUTTON_UP));check_frame(b);}
    assert(b.file_top()==0&&!tap(UI16_BUTTON_UP));
    // Conflicting actions and held buttons never cross a hierarchy.
    assert(!tap(0x0102));
    assert(!b.update(0x0100,t+=10,load));
    assert(tap(0x0202)&&!b.in_games());
    assert(!b.update(0x0202,t+=1000,load));
    assert(tap(0x0101)&&b.in_games()); // A+B together are one confirm action
    // Every entry is selectable, including indices beyond a screenful.
    Ui16MenuEntry many[23];for(auto &e:many)e={1,"NES"};
    Ui16Browser m(many,23);t=0;
    for(int i=0;i<22;++i){m.update(0,t+=10,load);assert(m.update(UI16_BUTTON_DOWN,t+=10,load));}
    assert(m.selected_system()==22&&m.system_top()==13);
    std::uint16_t cells[CELL_COUNT];ui16_build_system_menu(many,23,cells,22,m.system_top());
    assert(cells[12*32+2]&4096);
    m.update(0,t+=10,load);assert(m.update(0x0001,t+=10,load));
    m.update(0,t+=10,load);assert(m.update(0x0200,t+=10,load));
    assert(!m.in_games()&&m.selected_system()==22&&m.system_top()==13);
    // Timer repeat moves one row per update, including viewport boundaries.
    Ui16Navigation nav(137,8);
    assert(nav.update(UI16_BUTTON_DOWN,0)&&nav.selected()==9);
    assert(!nav.update(UI16_BUTTON_DOWN,399));
    assert(nav.update(UI16_BUTTON_DOWN,400)&&nav.selected()==10);
    assert(nav.update(UI16_BUTTON_DOWN,500)&&nav.selected()==11);
    assert(ui16_scroll_top(11,137,0)==2);
    assert(ui16_scroll_top(0,0,999)==0&&ui16_scroll_top(-1,4,999)==0);
    // Exact limit is allowed; additional entries are explicitly signalled.
    dirs["sd:nes"].clear();
    for(int i=0;i<UI16_GAME_CAPACITY;++i)dirs["sd:nes"].push_back({"g"+std::to_string(i)+".nes"});
    auto g=ui16_read_game_list("sd:nes",1);assert(g.names.size()==256&&!g.more);
    dirs["sd:nes"].push_back({"overflow.nes"});g=ui16_read_game_list("sd:nes",1);
    assert(g.names.size()==256&&g.more);
    Ui16Navigation full(256);
    for(int i=1;i<256;++i){full.update(0,i*20);assert(full.update(UI16_BUTTON_DOWN,i*20+1));}
    assert(full.selected()==255&&!full.update(UI16_BUTTON_DOWN,100000));
    std::puts("Stage 5 tests passed: 137-entry bidirectional scrolling, translated rows, UART frames, viewport restore/reorder, A/B/X/Y edges, 23 systems, bounds/repeat, 256-entry limit");
}
