#ifdef UI16_SYSTEM_MENU
#include "ui16_browser.h"
#include <utility>

Ui16Browser::Ui16Browser(const Ui16MenuEntry *entries, std::size_t count, int initial)
    : entries_(entries), count_(entries ? count : 0), systems_(count_, initial)
{
    system_top_=ui16_system_scroll_top(entries_,count_,systems_.selected(),0);
}

const Ui16MenuEntry *Ui16Browser::active_system() const
{
    const int selected = systems_.selected();
    if (selected < 0 || static_cast<std::size_t>(selected) >= count_) return nullptr;
    return entries_ + selected;
}

const char *Ui16Browser::heading() const
{
    return levels_.empty() ? "SYSTEMS" : levels_.back().heading.c_str();
}

void Ui16Browser::load(Ui16GameLoader loader, void *context, int selected)
{
    const auto *entry=active_system();
    games_={}; // Release the previous directory before allocating the next list.
    games_=loader && entry ? loader(entry->id,relative_path_.c_str(),context) : Ui16GameList{};
    if(!loader || !entry)games_.status=Ui16ListStatus::DirectoryError;
    files_=Ui16Navigation(games_.names.size(),selected);
    file_top_=ui16_game_scroll_top(games_,files_.selected(), file_top_);
}

bool Ui16Browser::take_launch_request(Ui16LaunchRequest &request)
{
    if(!pending_launch_.core_id)return false;
    request=std::move(pending_launch_);pending_launch_={};return true;
}
void Ui16Browser::resume_input(std::uint16_t buttons)
{
    previous_buttons_=buttons;
    systems_.update(0,0);files_.update(0,0);pending_launch_={};
}

bool Ui16Browser::update(std::uint16_t buttons, std::uint32_t now_ms,
                         Ui16GameLoader loader, void *context)
{
    // An action is rearmed only when both action groups have been released.
    const auto actions=UI16_BUTTON_CONFIRM|UI16_BUTTON_BACK;
    const auto pressed = static_cast<std::uint16_t>((previous_buttons_ & actions) ? 0 : buttons & actions);
    previous_buttons_ = buttons;
    const bool both = (buttons & UI16_BUTTON_CONFIRM) && (buttons & UI16_BUTTON_BACK);
    if (!both && in_games_ && (pressed & UI16_BUTTON_BACK)) {
        pending_launch_={};
        if(levels_.size()>1) {
            const auto child=levels_.back().component;
            levels_.pop_back();
            const auto slash=relative_path_.find_last_of('/');
            relative_path_=slash==std::string::npos ? "" : relative_path_.substr(0,slash);
            file_top_=levels_.back().top;
            load(loader,context,levels_.back().selected);
            // Restore the folder by its real name even if enumeration order changed.
            for(std::size_t i=0;i<games_.names.size();++i)if(games_.names[i]==child) {
                files_=Ui16Navigation(games_.names.size(),static_cast<int>(i));break;
            }
            file_top_=ui16_game_scroll_top(games_,files_.selected(),file_top_);
        } else {
            in_games_ = false;
            levels_.clear();relative_path_.clear();
            systems_.update(0, now_ms);
        }
        return true;
    }
    if (!both && !in_games_ && (pressed & UI16_BUTTON_CONFIRM)) {
        const auto *entry = active_system();
        if (entry && entry->id > 0) {
            relative_path_.clear();
            levels_={{"",ui16_system_label(*entry),0,0}};
            file_top_=0;
            load(loader,context);
            in_games_ = true;
            return true;
        }
    }
    if(!both && in_games_ && (pressed & UI16_BUTTON_CONFIRM)) {
        const int selected=files_.selected();
        if(selected>=0 && static_cast<std::size_t>(selected)<games_.directories.size() && games_.directories[selected]) {
            const auto component=games_.names[selected];
            const auto path=relative_path_.empty()?component:relative_path_+"/"+component;
            if(!ui16_valid_component(component)||levels_.size()-1>=UI16_MAX_FOLDER_DEPTH||path.size()>UI16_MAX_RELATIVE_PATH) {
                games_.navigation_error=true;
                return true;
            }
            const auto label=static_cast<std::size_t>(selected)<games_.titles.size()&&!games_.titles[selected].empty() ? games_.titles[selected] : component;
            levels_.back().selected=selected;
            levels_.back().top=file_top_;
            levels_.push_back({component,label,0,0});
            file_top_=0;
            relative_path_=path;
            load(loader,context);
            return true;
        }
        if(selected>=0&&static_cast<std::size_t>(selected)<games_.names.size()&&
           (static_cast<std::size_t>(selected)>=games_.directories.size()||!games_.directories[selected])&&
           games_.status==Ui16ListStatus::Ready){
            const auto *entry=active_system();
            if(entry&&ui16_valid_component(games_.names[selected])&&ui16_is_rom_file(entry->id,games_.names[selected].c_str())){
                games_.launch_error=nullptr;
                pending_launch_={entry->id,relative_path_,games_.names[selected]};
                return false; // Confirmation wins over a simultaneous direction.
            }
        }
    }
    const bool changed=in_games_ ? files_.update(buttons, now_ms) : systems_.update(buttons, now_ms);
    if(changed && in_games_) {
        games_.navigation_error=false;
        games_.launch_error=nullptr;
        file_top_=ui16_game_scroll_top(games_,files_.selected(),file_top_);
    } else if(changed) {
        system_top_=ui16_system_scroll_top(entries_,count_,systems_.selected(),system_top_);
    }
    return changed;
}
#endif
