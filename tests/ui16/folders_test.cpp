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
static Ui16GameList endless(int,const char *,void *context){
    Ui16GameList g;g.status=Ui16ListStatus::Ready;g.names={context?*static_cast<std::string*>(context):"d"};g.directories={true};return g;
}
static unsigned glyph(unsigned cp){for(auto e:ui16_font_map)if(e.codepoint==cp)return e.glyph;assert(false);return 0;}
int main(){
    dirs["sd:nes"]={{"action",AM_DIR},{"puzzle",AM_DIR},{"root.nes"},{"empty",AM_DIR},{"missing",AM_DIR},{"bad",AM_DIR},{".",AM_DIR},{"..",AM_DIR},{"hidden",AM_DIR|AM_HID}};
    dirs["sd:nes/action"]={{"nested",AM_DIR},{"A.NES"}};
    dirs["sd:nes/action/nested"]={{"deep.nes"}};dirs["sd:nes/puzzle"]={{"P.NES"}};dirs["sd:nes/empty"]={};dirs["sd:nes/bad"]={{"bad.nes"}};
    csvs["sd:nes/titles.csv"]=u8"name\ttitle\naction\tアクション\npuzzle\tパズル\nroot.nes\tROOT\n";
    csvs["sd:nes/action/titles.csv"]=u8"name\ttitle\nnested\t日本\nA.NES\tABC\n";
    csvs["sd:nes/action/nested/titles.csv"]=u8"name\ttitle\ndeep.nes\tカナ\n";
    csvs["sd:nes/bad/titles.csv"]="name,title\nbad.nes,bad\n";
    const Ui16MenuEntry entries[]={{1,"NES"},{-1,"CORES"}};Ui16Browser b(entries,2);std::uint32_t t=0;
    const auto press=[&](unsigned button){b.update(0,t+=10,load);return b.update(button,t+=10,load);};
    assert(press(UI16_BUTTON_CONFIRM)&&b.in_games()&&b.relative_path().empty());
    assert(b.game_list().names.size()==6&&b.game_list().directories[0]);
    assert(b.game_list().titles[0]==u8"アクション");
    std::uint16_t cells[CELL_COUNT];ui16_build_game_menu(b.heading(),b.game_list(),0,cells);
    assert((cells[3*32+12]&4095)==glyph('/')); // Japanese folder label plus slash.
    ui16_send_game_menu(b.heading(),b.game_list(),0);assert(wire.size()==994);
    std::ofstream hex("build-ui16-host/folders_cells.hex");char word[16];for(int i=0;i<512;++i){std::snprintf(word,sizeof(word),"%04x\n",i<CELL_COUNT?cells[i]:0);hex<<word;}
    std::ofstream packet("build-ui16-host/folders_frame.bin",std::ios::binary);packet.write(reinterpret_cast<const char*>(wire.data()),wire.size());
    assert(press(UI16_BUTTON_CONFIRM)&&b.relative_path()=="action"&&std::string(b.heading())==u8"アクション");
    const auto calls=loaded.size();assert(!b.update(UI16_BUTTON_CONFIRM,t+=500,load)&&loaded.size()==calls);
    assert(press(UI16_BUTTON_CONFIRM)&&b.relative_path()=="action/nested"&&std::string(b.heading())==u8"日本");
    assert(b.game_list().titles[0]==u8"カナ");
    dirs["sd:nes/action"].insert(dirs["sd:nes/action"].begin(),{"new.nes"});
    assert(press(UI16_BUTTON_BACK)&&b.relative_path()=="action"&&b.selected_file()==1); // restore by name, not old index
    assert(press(UI16_BUTTON_BACK)&&b.relative_path().empty()&&b.selected_file()==0);
    assert(press(UI16_BUTTON_DOWN)&&b.selected_file()==1);
    assert(press(UI16_BUTTON_CONFIRM)&&b.relative_path()=="puzzle");
    assert(press(UI16_BUTTON_BACK)&&b.selected_file()==1);
    press(UI16_BUTTON_DOWN);const auto before=loaded.size();assert(!press(UI16_BUTTON_CONFIRM)&&loaded.size()==before); // ROM is not launched.
    press(UI16_BUTTON_DOWN);assert(press(UI16_BUTTON_CONFIRM)&&b.game_list().status==Ui16ListStatus::Empty);
    assert(press(UI16_BUTTON_BACK)&&b.selected_file()==3);
    press(UI16_BUTTON_DOWN);assert(press(UI16_BUTTON_CONFIRM)&&b.game_list().status==Ui16ListStatus::DirectoryError);
    assert(press(UI16_BUTTON_BACK)&&b.selected_file()==4);
    press(UI16_BUTTON_DOWN);assert(press(UI16_BUTTON_CONFIRM)&&b.game_list().titles_error&&b.game_list().titles.empty());
    assert(press(UI16_BUTTON_BACK)&&b.selected_file()==5);
    assert(press(UI16_BUTTON_BACK)&&!b.in_games()&&b.selected_system()==0);
    // Depth and path bounds, with no traversal outside a core's root.
    Ui16Browser limited(entries,2);t=0;
    auto tap=[&](unsigned button,void *ctx=nullptr){limited.update(0,t+=10,endless,ctx);return limited.update(button,t+=10,endless,ctx);};
    tap(UI16_BUTTON_CONFIRM);
    for(unsigned i=0;i<UI16_MAX_FOLDER_DEPTH;++i)assert(tap(UI16_BUTTON_CONFIRM));
    const auto path=limited.relative_path();assert(tap(UI16_BUTTON_CONFIRM)&&limited.relative_path()==path&&limited.game_list().navigation_error);
    for(unsigned i=0;i<=UI16_MAX_FOLDER_DEPTH;++i)assert(tap(UI16_BUTTON_BACK));
    assert(!limited.in_games());
    std::string long_name(255,'x');tap(UI16_BUTTON_CONFIRM,&long_name);tap(UI16_BUTTON_CONFIRM,&long_name);tap(UI16_BUTTON_CONFIRM,&long_name);
    assert(limited.relative_path().size()==511);assert(tap(UI16_BUTTON_CONFIRM,&long_name)&&limited.game_list().navigation_error&&limited.relative_path().size()==511);
    assert(!ui16_valid_component("..")&&!ui16_valid_component("a/b")&&!ui16_valid_component("bad\\path"));
    std::puts("Folder tests passed: actual FatFs/CSV adapters, translated folders, nested Back/restore, held buttons, empty/error pages, ROM no-op, depth/path bounds");
}
