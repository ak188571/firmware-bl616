#include "ui16_titles.h"
#include "ui16_system_menu.h"
#include "ui16_cell.h"
#include "ui16_font_map.h"
#include "utils.h"
extern "C" {
#include "ff.h"
}
#include <cassert>
#include <cstring>
#include <fstream>
#include <cstdio>

static std::string file_data;
static int open_result=FR_OK,read_result=FR_OK,close_result=FR_OK,closed=0;
extern "C" FRESULT f_open(FIL *f,const char *,unsigned char mode){assert(mode==FA_READ);f->position=0;return open_result;}
extern "C" FRESULT f_read(FIL *f,void *buf,UINT len,UINT *n){
    if(read_result!=FR_OK)return read_result;
    *n=static_cast<UINT>(std::min<std::size_t>(len,file_data.size()-f->position));
    std::memcpy(buf,file_data.data()+f->position,*n);f->position+=*n;return FR_OK;
}
extern "C" FRESULT f_close(FIL *){++closed;return close_result;}
int critical_depth=0;static std::vector<unsigned char> bytes;
void enter_critical(){assert(!critical_depth);++critical_depth;}
void leave_critical(){assert(critical_depth==1);--critical_depth;}
void fpga_tx_byte(std::uint8_t b){assert(critical_depth==1);bytes.push_back(b);}
void fpga_tx_header(int cmd,int len){fpga_tx_byte(0xaa);fpga_tx_byte(len>>8);fpga_tx_byte(len);fpga_tx_byte(cmd);}
static bool parse(const std::string &text,Ui16GameList &g){
    Ui16TitlesParser p(g);
    for(char c:text)if(!p.feed(&c,1))return false; // Split even BOM and multi-byte characters.
    return p.finish();
}
static unsigned glyph(unsigned cp){for(auto e:ui16_font_map)if(e.codepoint==cp)return e.glyph;assert(false);return 0;}
int main(){
    Ui16GameList g;g.status=Ui16ListStatus::Ready;g.names={"GAME001.nes","game002.nes","game003.nes"};
    assert(parse(u8"\xef\xbb\xbf" "name\ttitle\r\ngame001.NES\t日本「テスト」、ABC!\r\ngame002.nes\t　 \r\n",g));
    assert(g.titles[0]==u8"日本「テスト」、ABC!"&&g.titles[1].empty()&&g.titles[2].empty());
    Ui16GameList ordered;ordered.status=Ui16ListStatus::Ready;
    ordered.names={"zeta.nes","Folder","alpha.nes","Beta.nes"};
    ordered.directories={false,true,false,false};
    assert(parse("name\ttitle\nBeta.nes\tB\nFolder\tF\nmissing.nes\tX\n",ordered));
    assert((ordered.names==std::vector<std::string>{"Beta.nes","Folder","alpha.nes","zeta.nes"}));
    assert((ordered.titles==std::vector<std::string>{"B","F","",""}));
    assert((ordered.directories==std::vector<bool>{false,true,false,false}));
    Ui16GameList extended;extended.status=Ui16ListStatus::Ready;
    extended.names={"zeta.nes","alpha.nes","Folder"};extended.directories={false,false,true};
    assert(parse("name\ttitle\tcrt_v1\nalpha.nes\t日本\t1:2:3\nFolder\tF\t\nzeta.nes\t\t0:0:0\n",extended));
    assert(extended.names[0]=="alpha.nes"&&extended.crt_recommendations[0].present);
    assert((extended.crt_recommendations[0].settings==Ui16CrtSettings{true,2,3}));
    assert(!extended.crt_recommendations[1].present&&extended.crt_recommendations[2].present);
    for(const auto &bad:std::vector<std::string>{
        "name\ttitle\tcrt_v2\n", "name\ttitle\tcrt_v1\na.nes\tA\t1:4:0\n",
        "name\ttitle\tcrt_v1\na.nes\tA\t2:0:0\n", "name\ttitle\tcrt_v1\na.nes\tA\t1:0\n",
        "name\ttitle\tcrt_v1\na.nes\tA\n", "name\ttitle\tcrt_v1\nFolder\tF\t1:1:1\n"}){
        const auto old_names=extended.names;assert(!parse(bad,extended)&&extended.names==old_names);
        assert(extended.crt_recommendations[0].settings.blur==2);
    }
    // A blank display title still participates in CSV ordering.
    Ui16GameList blank;blank.status=Ui16ListStatus::Ready;
    blank.names={"zeta.nes","alpha.nes","Beta.nes"};
    assert(parse("name\ttitle\nBeta.nes\t\n",blank));
    assert((blank.names==std::vector<std::string>{"Beta.nes","alpha.nes","zeta.nes"}));
    const auto ordered_names=ordered.names,ordered_titles=ordered.titles;
    const auto ordered_directories=ordered.directories;
    assert(!parse("name\ttitle\nFolder\tF\nmissing.nes\tX\nMISSING.NES\tY\n",ordered));
    assert(ordered.names==ordered_names&&ordered.titles==ordered_titles&&ordered.directories==ordered_directories);
    for(const auto &bad:std::vector<std::string>{"", "wrong\ttitle\n", "name\ttitle\n\tx\n", "name\ttitle\n../bad.nes\tx\n", "name\ttitle\nA.nes\tx\ty\n", "name\ttitle\nGAME001.nes\tx\ngame001.NES\ty\n",std::string("name\ttitle\nA.nes\t")+"\xc0\x80",std::string("name\ttitle\nA.nes\t")+std::string(513,'A')}){
        auto copy=g;const auto before=copy.titles;assert(!parse(bad,copy)&&copy.titles==before);
    }
    assert(parse("name\ttitle",g)); // Header-only file, no final newline.
    open_result=FR_NO_FILE;closed=0;ui16_load_titles("sd:nes/titles.csv",g);assert(!g.titles_error&&g.titles.empty()&&closed==0);
    open_result=FR_DISK_ERR;ui16_load_titles("x",g);assert(g.titles_error);
    open_result=FR_OK;file_data="name\ttitle\ngame001.nes\tA\n";read_result=FR_DISK_ERR;ui16_load_titles("x",g);assert(g.titles_error&&g.titles.empty());
    read_result=FR_OK;close_result=FR_DISK_ERR;ui16_load_titles("x",g);assert(g.titles_error&&g.titles.empty());
    close_result=FR_OK;file_data="name\ttitle\ngame001.nes\tA\nbad\n";ui16_load_titles("x",g);assert(g.titles_error&&g.titles.empty());
    file_data="name\ttitle\n"+std::string(UI16_TSV_MAX_BYTES,'\n');ui16_load_titles("x",g);assert(g.titles_error);
    file_data=u8"name\ttitle\ngame001.nes\t日本「ヲ」、abc!\ngame002.nes\t\n";ui16_load_titles("x",g);assert(!g.titles_error);
    assert(g.names[0]=="GAME001.nes"&&g.titles[0]==u8"日本「ヲ」、abc!");
    std::uint16_t cells[CELL_COUNT];ui16_build_game_menu("NES",g,0,cells);
    assert((cells[10*32+2]&UI16_GLYPH_MASK)==glyph(U'日'));
    assert((cells[10*32+8]&UI16_GLYPH_MASK)==glyph(U'ヲ')&&glyph(U'ヲ')>255);
    assert(cells[10*32+8]&UI16_SELECTED);
    assert(cells[11*32+8]&UI16_VERTICAL_HALF);
    ui16_send_game_menu("NES",g,0);assert(bytes.size()==1988);
    std::size_t pos=0;
    for(int y=0;y<MENU_ROWS;++y){for(int b:{0xaa,0,68,15,y,0,32})assert(bytes[pos++]==b);
        for(int x=0;x<32;++x){const auto c=cells[y*32+x];assert(bytes[pos++]==(c&255));const auto hi=bytes[pos++];
            assert((hi&ATTR_MASK)==(ui16_wire_high(c)&ATTR_MASK));
            assert(((((hi&0x3c)>>2)<<8)|(c&255))==(c&4095));}}
    std::ofstream hex("build-ui16-host/titles_cells.hex");char word[16];for(int i=0;i<1024;++i){std::snprintf(word,sizeof(word),"%04x\n",i<CELL_COUNT?cells[i]:0);hex<<word;}
    std::ofstream packet("build-ui16-host/titles_frame.bin",std::ios::binary);packet.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    g.titles[0]=u8"😀";ui16_build_game_menu("NES",g,0,cells);assert((cells[10*32+2]&4095)==glyph(U'□'));
    std::puts("Stage 4 C++ tests passed: streaming TSV, BOM/CRLF/UTF-8, blank fallback, duplicate/error atomicity, original names, >255 glyphs, wire bytes, replacement glyph");
}
