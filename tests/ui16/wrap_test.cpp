#include "ui16_browser.h"
#include "ui16_titles.h"
#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>

extern "C" FRESULT f_open(FIL *,const char *,unsigned char){return FR_NO_FILE;}
extern "C" FRESULT f_read(FIL *,void *,UINT,UINT *){return FR_DISK_ERR;}
extern "C" FRESULT f_close(FIL *){return FR_OK;}
extern "C" FRESULT f_opendir(DIR *,const char *){return FR_NO_PATH;}
extern "C" FRESULT f_readdir(DIR *,FILINFO *){return FR_DISK_ERR;}
extern "C" FRESULT f_closedir(DIR *){return FR_OK;}
int critical_depth=0;
static std::vector<unsigned char> wire;
void enter_critical(){assert(critical_depth==0);++critical_depth;}
void leave_critical(){assert(critical_depth==1);--critical_depth;}
void fpga_tx_byte(std::uint8_t b){assert(critical_depth==1);wire.push_back(b);}
void fpga_tx_header(int c,int n){fpga_tx_byte(0xaa);fpga_tx_byte(n>>8);fpga_tx_byte(n);fpga_tx_byte(c);}
static unsigned glyph(unsigned cp){for(auto e:ui16_font_map)if(e.codepoint==cp)return e.glyph;assert(false);return 0;}
static bool parse(const std::string &raw,Ui16GameList &g){
    const auto csv="name\ttitle\nchild\t"+raw+"\n";Ui16TitlesParser parser(g);
    for(char c:csv)if(!parser.feed(&c,1))return false;
    return parser.finish();
}
static std::string hex(std::string_view s){std::ostringstream out;for(unsigned char c:s)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);return out.str();}
static Ui16GameList root_list;
static Ui16GameList load(int,const char *path,void *){
    if(!*path)return root_list;
    Ui16GameList g;g.status=Ui16ListStatus::Ready;g.names={"leaf.nes"};return g;
}
static void verify(const Ui16Browser &b){
    const auto &g=b.game_list();std::uint16_t cells[CELL_COUNT];
    ui16_build_game_menu("NES",g,b.selected_file(),cells,b.file_top());
    int row=3,selected_rows=0;
    for(std::size_t i=b.file_top();i<g.names.size();++i){
        const auto label=ui16_game_label(g,i);const auto layout=ui16_layout_text(label);
        if(row+layout.count>13)break;
        for(int line=0;line<layout.count;++line){
            const int y=row+line;
            assert(bool(cells[y*32+2]&4096)==(static_cast<int>(i)==b.selected_file()));
            if(line)assert((cells[y*32+2]&4095)==0); // indentation is a blank glyph
            if(static_cast<int>(i)==b.selected_file())++selected_rows;
        }
        row+=layout.count;
    }
    assert(selected_rows==ui16_layout_text(ui16_game_label(g,b.selected_file())).count);
    for(int y=row;y<13;++y)for(int x=0;x<32;++x)assert(cells[y*32+x]==0);
    assert(cells[13*32+2]==glyph('X'));
    wire.clear();ui16_send_game_menu("NES",g,b.selected_file(),b.file_top());assert(wire.size()==994);
    for(int y=0;y<14;++y)for(int x=0;x<32;++x){const auto offset=y*71+7+x*2;assert(wire[offset]==(cells[y*32+x]&255));assert(wire[offset+1]==ui16_wire_high(cells[y*32+x]));}
    if(b.selected_file()==70){
        std::ofstream f("build-ui16-host/wrap_cells.hex");char word[16];
        for(int i=0;i<512;++i){std::snprintf(word,sizeof(word),"%04x\n",i<CELL_COUNT?cells[i]:0);f<<word;}
    }
}
int main(int argc,char **argv){
    if(argc==3&&std::string(argv[1])=="--layout"){
        const std::string input=argv[2];std::string s;
        for(std::size_t i=0;i<input.size();i+=2)s+=static_cast<char>(std::stoul(input.substr(i,2),nullptr,16));
        const auto l=ui16_layout_text(s);
        std::cout<<l.valid<<'|'<<l.clipped<<'|'<<l.count<<'|'<<hex(l.lines[0])<<'|'<<hex(l.lines[1])<<'\n';return 0;
    }
    auto l=ui16_layout_text(std::string(30,'A'));assert(l.count==1&&!l.clipped);
    const auto ascii=std::string(31,'A');l=ui16_layout_text(ascii);assert(l.count==2&&l.lines[0].size()==30&&l.lines[1]=="A");
    l=ui16_layout_text(std::string(59,'A'));assert(l.count==2&&!l.clipped);
    l=ui16_layout_text(std::string(60,'A'));assert(l.clipped);
    const auto mixed=std::string(29,'A')+u8"日本";l=ui16_layout_text(mixed);assert(l.lines[0].size()==29&&l.lines[1]==u8"日本");
    std::string decoded;assert(ui16_decode_title(R"(日本\nテスト)",decoded)&&decoded==u8"日本\nテスト");
    assert(ui16_decode_title(R"(A\\nB)",decoded)&&decoded==R"(A\nB)");
    for(const auto &bad:std::vector<std::string>{R"(\nA)",R"(A\n)",R"(A\nB\nC)",R"(A\tB)","A\\",R"(A\n　)",std::string(31,'A')+R"(\nB)","A\\n"+std::string(30,'B')})assert(!ui16_decode_title(bad,decoded));
    Ui16GameList g;g.status=Ui16ListStatus::Ready;g.names={"child"};g.directories={true};
    assert(parse(R"(日本\nテスト)",g)&&g.titles[0]==u8"日本\nテスト");
    auto old=g.titles;assert(!parse("A\\n"+std::string(29,'B'),g)&&g.titles==old); // slash is part of the second line
    assert(parse("A\\n"+std::string(28,'B'),g));
    for(const auto &bad:std::vector<std::string>{R"(A\tB)",R"(\nA)",R"(A\nB\nC)"}){old=g.titles;assert(!parse(bad,g)&&g.titles==old);}
    // Exact glyph positions, indentation, selected attribute on both rows.
    g.titles={u8"日本\nテスト"};std::uint16_t cells[CELL_COUNT];ui16_build_game_menu("NES",g,0,cells);
    assert(cells[3*32+2]==(glyph(U'日')|4096));
    assert(cells[4*32+2]==4096&&cells[4*32+3]==(glyph(U'テ')|4096));
    assert(cells[4*32+9]==(glyph('/')|4096));
    // A two-row item at the lower boundary is not partially shown.
    g={};g.status=Ui16ListStatus::Ready;
    for(int i=0;i<9;++i)g.names.push_back("A");
    g.names.push_back(std::string(31,'B'));
    ui16_build_game_menu("NES",g,0,cells,0);for(int x=0;x<32;++x)assert(cells[12*32+x]==0);
    assert(ui16_game_scroll_top(g,9,0)==1);
    // 137 mixed-height entries, folder restore, reverse movement and changed order.
    root_list={};root_list.status=Ui16ListStatus::Ready;
    for(int i=0;i<137;++i){root_list.names.push_back(i==70?"child":"g"+std::to_string(i)+".nes");root_list.titles.push_back(i%3?u8"日本\\nテスト":std::string(40,'A'));root_list.directories.push_back(i==70);}
    // Convert the forced marker just as the CSV parser does.
    for(auto &title:root_list.titles)if(title.find('\\')!=std::string::npos){std::string d;assert(ui16_decode_title(title,d));title=d;}
    // Mix genuine one-row titles too.
    for(int i=0;i<137;i+=4)root_list.titles[i]="SHORT";
    const Ui16MenuEntry systems[]={{1,"NES"}};Ui16Browser b(systems,1);unsigned t=0;
    auto tap=[&](unsigned button){b.update(0,t+=10,load);return b.update(button,t+=10,load);};
    assert(tap(0x0100));
    for(int i=0;i<137;++i){assert(b.selected_file()==i);verify(b);if(i<136)assert(tap(UI16_BUTTON_DOWN));}
    assert(!tap(UI16_BUTTON_DOWN));
    for(int i=135;i>=70;--i){assert(tap(UI16_BUTTON_UP));verify(b);}
    const int saved=b.file_top();assert(tap(0x0001)&&b.relative_path()=="child");
    assert(tap(0x0002)&&b.selected_file()==70&&b.file_top()==saved);verify(b);
    assert(tap(0x0100));root_list.names.insert(root_list.names.begin(),"new.nes");root_list.titles.insert(root_list.titles.begin(),"NEW");root_list.directories.insert(root_list.directories.begin(),false);
    assert(tap(0x0200)&&b.selected_file()==71);verify(b);
    for(int i=70;i>=0;--i){assert(tap(UI16_BUTTON_UP));verify(b);}assert(b.file_top()==0&&!tap(UI16_BUTTON_UP));
    // System names use the same two-row rules and viewport calculation.
    Ui16MenuEntry many[17];for(auto &e:many)e={3,"ABCDEFGHIJKLMNOPQRSTUVWXYZ12345"};
    Ui16Browser system_browser(many,17,16);assert(system_browser.system_top()==12);
    ui16_build_system_menu(many,17,cells,16,system_browser.system_top());
    assert(cells[11*32+2]&4096);assert(cells[12*32+2]==4096&&cells[12*32+3]&4096);
    std::puts("Wrap tests passed: 30/29 widths, UTF-8 boundaries, forced escapes/errors, directory suffix, indentation, whole-item scrolling, 137 mixed entries, restore, system menu, UART");
}
