/*
 * TangCore firmware for BL616 MCU
 *
 * (c) 2025, nand2mario <nand2mario@outlook.com>
 *
 * This source code is licensed under the Apache 2.0 license found in the
 * LICENSE file in the root directory of this source tree.
 *
 */

#include <stdarg.h>
#include <string.h>
#include <vector>

extern "C" {
#include "board.h"
#include "bl616_glb.h"
#include "bflb_gpio.h"
#include "bflb_uart.h"
#include "bflb_clock.h"
#include "bl616_clock.h"

#include "usbh_core.h"
#include "ff.h"
#include "fatfs_diskio_register.h"
}

#include "file_chooser.h"
#include "programmer.h"
#include "usb_gamepad.h"
#include "utils.h"
#include "cores.h"
#include "overlay.h"
#include "init.h"
#include "menu_manager.h"

#ifdef UI16_SYSTEM_MENU
#include "ui16_system_menu.h"
#include "ui16_navigation.h"
#include "ui16_browser.h"
#include "ui16_titles.h"
#include "ui16_launch.h"
#include "ui16_osd_menu.h"
#ifdef UI16_SD_FONT
#include "ui16_font_cache.h"
#ifdef UI16_RELIABLE_FONT
#include "ui16_link.h"
#endif
#endif
#endif

// Uncomment this to enable UART console (use with caution. it may interfere with MCU-FPGA communication)
#define UART_CONSOLE

/////////////////////////////////////////////////////////////////////////////////
// Global state

int option_osd_key = OPTION_OSD_KEY_SELECT_RIGHT;
int16_t active_core = -1;           // firmware detected this core as active
bool core_running;                  // a ROM is loaded and running on the core
#ifdef UI16_SYSTEM_MENU
static volatile bool osd_config_ready=false,osd_config_capable=false;
#endif
struct core_info *core;

// UART
struct bflb_device_s *gpio_dev;
struct bflb_device_s *uart0_dev;
struct bflb_device_s *uart1_dev;

// USB and fatfs
struct usbh_msc *msc;
const char *drv = "sd:";

// Tasks and shared state
TaskHandle_t main_task_handle;
TaskHandle_t uart1_rx_task_handle;

#ifdef TANG_CONSOLE60K
const char *BOARD_NAME = "console60k";
#elif defined(TANG_CONSOLE138K)
const char *BOARD_NAME = "console138k";
#elif defined(TANG_MEGA60K)
const char *BOARD_NAME = "mega60k";
#elif defined(TANG_MEGA138K)
const char *BOARD_NAME = "mega138k";
#elif defined(TANG_PRIMER25K)
const char *BOARD_NAME = "primer25k";
#elif defined(TANG_NANO20K)
const char *BOARD_NAME = "nano20k";
#else
const char *BOARD_NAME = "unknown";
#endif

// Override system printf() to send to FPGA
int __attribute__((weak)) putchar(int ch) {
    fpga_tx_header(0x05, 2);
    fpga_tx_byte(ch);
    return ch;
}


/////////////////////////////////////////////////////////////////////////////////
// Core loading and other file system operations

// nand2mario: these USB data structures cannot be CACHED as they are written to by hardware
USB_NOCACHE_RAM_SECTION FATFS fs;
USB_NOCACHE_RAM_SECTION FIL fcore;
USB_NOCACHE_RAM_SECTION BYTE __attribute__((aligned(64))) fbuf[BLOCK_SIZE];

FRESULT res_sd;
FileChooser file_chooser;

// #define PAGESIZE 22
// #define TOPLINE 2
// #define PWD_SIZE 1024
// char pwd[PWD_SIZE];
// one page of file names to display
// char file_names[PAGESIZE][256];
// int file_dir[PAGESIZE];         // this file is a directory
// int file_sizes[PAGESIZE];
// int file_len;		            // number of files on this page

/////////////////////////////////////////////////////////////////////////////////
// Menu display and user interaction

// Menus for "NES", "SNES" ... entries
// dir: initial dir including the drive name (e.g. "sd:nes", "usb:cores")
// return 0: user chose a ROM (*choice), 1: no choice made, -1: error
// file chosen: pwd / file_name[*choice]
static int menu_loadrom(const char *dir) {
    string fname;
    file_chooser.rootdir = dir;
    file_chooser.curdir = dir;
    file_chooser.msg_return = "<< Return to main menu";
    bool r = file_chooser.choose_file(fname);
    if (!r) {
        overlay_status("No file chosen");
        return 1;
    }

    // now proceed to load the core and ROM
    joy1_state = 0; joy2_state = 0; // clear joypad states

    // load core if in cores/ dir
    if (fname.find(string(drv) + "cores") == 0) {
        overlay_status("Core: %s", fname.c_str());
        fpga_program(fname.c_str());
        _overlay_on = 1;                // turn on overlay after core is loaded
        return 0;       // return to main menu
    }

    // find core info entry
    core_info *core = NULL;
    string path = fname.substr(fname.find(":")+1);
    for (int i = 0; i < core_info_list.size(); i++) {
        core_info *c = &core_info_list[i];
        if (path.find(c->rom_dir) == 0) {
            overlay_status("ROM for: %s", c->display_name);
            core = c;
            break;
        }
    }
    if (core == NULL) {
        overlay_status("Core not found: %s", path.c_str());
        return -1;
    }

    // user chose a ROM file
    active_core = get_core_id();

    // load core if needed
    if (core != NULL) {
        if (active_core != core->id) {      // active core is not what we need
            string fname_core;
            if (find_core_for_board(fname_core, core->core_file)) {
                // load core
                fpga_program(fname_core.c_str());
                _overlay_on = 1;

                // allow 2 seconds for core to start
                uint64_t start = bflb_mtimer_get_time_ms();
                while (bflb_mtimer_get_time_ms() - start < 2000) {
                    send_blank_packet();
                    active_core = get_core_id();
                    if (active_core == core->id)
                        break;
                }
            }
        }

        // Attemp to load ROM
        if (active_core == core->id) {
            overlay_status("Loading ROM: %s\n", fname.c_str());
            core->load_rom(fname.c_str());
            return 1;
        } else {
            overlay_status("Core failed to load\n");
            delay(1000);
            return -1;
        }
    }
    return -1;
}

static void menu_options(void) {
    // to be implemented
}

// keep sending HID state to core until OSD is turned on
static void send_hid_to_core(void) {
    uint16_t hid1_old = 0, hid2_old = 0;
    bool first = true;
    dprint("Start sending HID to core...");
    while (1) {
        uint16_t joy1=0, joy2=0, hid1=0, hid2=0;
        get_joypad_states(&joy1, &joy2, &hid1, &hid2);
#ifdef UI16_SYSTEM_MENU
        // Consume the OSD chord before sending game HID, and release any latched state.
        if(joy1==OSD_KEY_CODE||joy2==OSD_KEY_CODE||hid1==OSD_KEY_CODE||hid2==OSD_KEY_CODE||overlay_on()){
            overlay_osd_window(0,0,0,0);
            overlay(1);break;
        }
        if(first||hid1!=hid1_old||hid2!=hid2_old){
            fpga_tx_header(0x09,5);
            fpga_tx_byte(hid1>>8);fpga_tx_byte(hid1&0xff);
            fpga_tx_byte(hid2>>8);fpga_tx_byte(hid2&0xff);
            hid1_old=hid1;hid2_old=hid2;first=false;
        }
#else
        if (first || hid1 != hid1_old || hid2 != hid2_old) {    // send HID if changed
            fpga_tx_header(0x09, 5);
            fpga_tx_byte(hid1 >> 8);
            fpga_tx_byte(hid1 & 0xff);
            fpga_tx_byte(hid2 >> 8);
            fpga_tx_byte(hid2 & 0xff);
            hid1_old = hid1;
            hid2_old = hid2;
            first = false;
        }
        if (joy1 == OSD_KEY_CODE || joy2 == OSD_KEY_CODE || hid1 == OSD_KEY_CODE || hid2 == OSD_KEY_CODE) {
            break;
        }
        if (overlay_on())      // turned off by keyboard
            break;
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    dprint("Stopped sending HID to core.");
}

// // (R L X A RT LT DN UP START SELECT Y B)
// Return: 1 button B pressed, 4: button A pressed, 2: next page, 3: previous page
// active is the entry chosen
int joy_choice(int start_line, int len, int *active, int overlay_key_code) {
    if (*active < 0 || *active >= len)
        *active = 0;
    uint16_t joy1=0, joy2=0, hid1=0, hid2=0;
    int last = *active;

    get_joypad_states(&joy1, &joy2, &hid1, &hid2);
    joy1 |= hid1;
    joy2 |= hid2;

    if ((joy1 == overlay_key_code) || (joy2 == overlay_key_code)) {
        overlay_status("OSD: %s", overlay_on() ? "ON" : "OFF");
        overlay(!overlay_on());    // toggle OSD
        delay(300);
    }

    if (!overlay_on()) {           // keep sending HID state to core when OSD is off
        send_hid_to_core();
        return 0;
    }

    if ((joy1 & 0x10) || (joy2 & 0x10)) {
        if (*active > 0) (*active)--;
    }
    if ((joy1 & 0x20) || (joy2 & 0x20)) {
        if (*active < len-1) (*active)++;
    }
    if ((joy1 & 0x40) || (joy2 & 0x40))
        return 3;      // previous page
    if ((joy1 & 0x80) || (joy2 & 0x80))
        return 2;      // next page
    if ((joy1 & 0x100) || (joy2 & 0x100))
        return 4;      // button A pressed
    if ((joy1 & 0x1) || (joy2 & 0x1))
        return 1;      // button B pressed

    overlay_cursor(0, start_line + (*active));
    overlay_printf(">");

    // overlay_cursor(0, 27);
    // overlay_printf(" j1=%04x j2=%04x h1=%04x h2=%04x", joy1, joy2, hid1, hid2);
    if (last != *active) {
        overlay_cursor(0, start_line + last);
        overlay_printf(" ");
        delay(100);     // button debounce
    }
    return 0;
}

#define MAIN_TASK_STACK_SIZE  2048
#define MAIN_TASK_PRIORITY    3
#define UART1_RX_TASK_STACK_SIZE  512
#define UART1_RX_TASK_PRIORITY    3

// UART1 RX IRQ drains the 32-byte hardware FIFO into a task-owned parser queue.
// The OSD config reply is 85 bytes on the wire and cannot wait one RTOS tick.
static volatile uint8_t uart1_rx_ring[1024];
static volatile unsigned uart1_rx_write=0,uart1_rx_read=0;
static volatile bool uart1_rx_overflow=false;
static void uart1_rx_isr(int,void *)
{
    const uint32_t status=bflb_uart_get_intstatus(uart1_dev);
    if(status&UART_INTSTS_RTO)bflb_uart_int_clear(uart1_dev,UART_INTCLR_RTO);
    while(bflb_uart_rxavailable(uart1_dev)){
        const uint8_t byte=bflb_uart_getchar(uart1_dev);
        const unsigned next=(uart1_rx_write+1)&1023;
        if(next==uart1_rx_read)uart1_rx_overflow=true;
        else {uart1_rx_ring[uart1_rx_write]=byte;uart1_rx_write=next;}
    }
}
// 1: byte, 0: empty, -1: overflow (discard incomplete data and resynchronise).
static int uart1_rx_take(uint8_t *byte)
{
    int result=0;
    taskENTER_CRITICAL();
    if(uart1_rx_overflow){uart1_rx_read=uart1_rx_write;uart1_rx_overflow=false;result=-1;}
    else if(uart1_rx_read!=uart1_rx_write){
        *byte=uart1_rx_ring[uart1_rx_read];uart1_rx_read=(uart1_rx_read+1)&1023;result=1;
    }
    taskEXIT_CRITICAL();
    return result;
}
static void uart1_rx_start()
{
    bflb_irq_attach(uart1_dev->irq_num,uart1_rx_isr,nullptr);
    bflb_uart_rxint_mask(uart1_dev,false);
    bflb_irq_enable(uart1_dev->irq_num);
}

// Receive joypad updates and other UART responses from the FPGA
static void uart1_rx_task(void *pvParameters)
{
    uint8_t buffer[5];
#ifdef UI16_RELIABLE_FONT
    uint8_t link_ack[14];
#endif
    uint16_t pos = 0;
    uint8_t type = 0;
    uint16_t len = 0;

#ifdef UI16_SYSTEM_MENU
    char osd_tag[7]={};
    unsigned osd_tag_count=0;
    bool osd_tag_found=false;
#endif
    while (1) {
        uint8_t ch=0;
        int received;
        while ((received=uart1_rx_take(&ch))!=0) {
            if(received<0){pos=0;continue;}

            if (pos == 0) {          // expecting 0xAA
                if (ch == 0xAA)
                    pos++;
            } else if (pos == 1) {   // len msb
                len = (uint16_t)ch << 8;
                pos++;
            } else if (pos == 2) {   // len lsb
                len += ch;
                pos++;
            } else if (pos == 3) {   // command type
                type = ch;
                pos++;
#ifdef UI16_RELIABLE_FONT
                if(type==0x16 && len!=15)pos=0;
#endif

            ////// pos >= 4 //////
            }
#ifdef UI16_RELIABLE_FONT
            else if(type==0x16) {
                link_ack[pos-4]=ch;
                if(pos==17){ui16_link_accept_ack(link_ack,sizeof link_ack);pos=0;}
                else ++pos;
            }
#endif
            else if (type == 1) {     // response to command 1 (get core ID)
                if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                    core_id = ch;
                    xSemaphoreGive(state_mutex);
                }
                pos = 0;
            } else if (type == 2) {                 // config string / OSD feature marker
#ifdef UI16_SYSTEM_MENU
                if(pos==4){osd_tag_count=0;osd_tag_found=false;}
                for(unsigned i=0;i<6;++i)osd_tag[i]=osd_tag[i+1];
                osd_tag[6]=static_cast<char>(ch);
                if(++osd_tag_count>=7&&memcmp(osd_tag,"OSDWIN1",7)==0)osd_tag_found=true;
                if(pos==len+2){
                    taskENTER_CRITICAL();osd_config_capable=osd_tag_found;osd_config_ready=true;taskEXIT_CRITICAL();
                }
#endif
                if(pos==len+2)pos=0;else ++pos;
            } else if (type == 3) {                 // periodic joypad state
                buffer[pos-4] = ch;
                // Complete packet received
                if (pos == 7) {
                    // Combine bytes into 16-bit values
                    uint16_t joy1 = (buffer[0] << 8) | buffer[1];
                    uint16_t joy2 = (buffer[2] << 8) | buffer[3];

                    // Update global state with mutex protection
                    if (xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
                        joy1_state = joy1;
                        joy2_state = joy2;
                        xSemaphoreGive(state_mutex);
                    }
                    pos = 0; // Reset for next packet
                } else
                    pos++;
            } else if (type == 4) {              // floppy write
                if (pos < 6)
                    buffer[pos-4] = ch;
                else
                    fbuf[pos-6] = ch;
                if (pos == 6+511) {
                    uint16_t drive = buffer[0] >> 7;
                    uint16_t sector = (buffer[0] & 0x7f) << 8 | buffer[1];
                    if (floppy[drive]) {
                        UINT br;
                        f_lseek(&f_floppy[drive], sector * 512);
                        if (f_write(&f_floppy[drive], fbuf, 512, &br) != FR_OK) {
                            overlay_status("Failed to write floppy");
                        }
                    }
                    pos = 0;   // reset for next packet
                } else
                    pos++;
            } else if (type == 5) {              // floppy read
                buffer[pos-4] = ch;
                if (pos == 5) {
                    uint16_t drive = buffer[0] >> 7;
                    uint16_t sector = (buffer[0] & 0x7f) << 8 | buffer[1];
                    if (floppy[drive]) {
                        UINT br;
                        f_lseek(&f_floppy[drive], sector * 512);
                        if (f_read(&f_floppy[drive], fbuf, 512, &br) == FR_OK) {
                            fpga_tx_header(0x0a, br+1);
                            for (UINT i = 0; i < br; i++) {
                                fpga_tx_byte(fbuf[i]);
                            }
                        } else {
                            overlay_status("Failed to read floppy");
                        }
                    }
                    pos = 0;   // reset for next packet
                } else
                    pos++;

            } else {
                pos = 0; // Reset if we get out of sync
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

#ifdef UI16_SYSTEM_MENU
static bool ui16_hw_osd_ready(void *)
{
    taskENTER_CRITICAL();osd_config_ready=false;osd_config_capable=false;taskEXIT_CRITICAL();
    fpga_tx_header(2,1);
    const auto start=bflb_mtimer_get_time_ms();
    while(!osd_config_ready&&bflb_mtimer_get_time_ms()-start<400)delay(10);
    if(!osd_config_ready||!osd_config_capable)return false;
    overlay_osd_key(OSD_KEY_CODE);
    return true;
}

// UI16 launch adapters BEGIN (host-tested with hardware substitutes)
static bool ui16_hw_file_ready(const char *path,void *)
{
    FILINFO info{};
    return f_stat(path,&info)==FR_OK&&!(info.fattrib&AM_DIR)&&info.fsize>0;
}
static bool ui16_hw_find_core(const char *name,std::string &path,void *)
{
    return find_core_for_board(path,name)&&ui16_hw_file_ready(path.c_str(),nullptr);
}
static bool ui16_hw_program(const char *path,void *)
{
    core_running=false;active_core=-1;
    extern bool gba_bios_loaded,gba_missing_bios_warned;
    gba_bios_loaded=false;gba_missing_bios_warned=false;
    const bool ok=fpga_program(path);
    // Native FPGA pads report changes; discard the previous FPGA image's latch.
    taskENTER_CRITICAL();joy1_state=0;joy2_state=0;taskEXIT_CRITICAL();
    _overlay_on=1;
    return ok;
}
static bool ui16_hw_wait_core(int expected,void *)
{
    const auto start=bflb_mtimer_get_time_ms();
    do {
        send_blank_packet();active_core=get_core_id();
        if(active_core==expected)return true;
        delay(10);
    } while(bflb_mtimer_get_time_ms()-start<2000);
    return false;
}
static bool ui16_hw_load_rom(int id,const char *path,void *)
{
    auto *target=find_core_by_id(id);
    core_running=false;
    if(!target||!target->load_rom)return false;
    const int result=target->load_rom(path);
    if(result!=0||!core_running){core_running=false;return false;}
    overlay(0);return true;
}
static void ui16_hw_display_owner(bool ui16,void *)
{
    fpga_use_ui16_display(ui16);overlay(1);
}
static bool ui16_hw_starting(void *)
{
    ui16_send_starting_game();
    if (bflb_uart_wait_tx_done(uart1_dev)!=0) return false;
    // Allow the completed frame to remain visible before FPGA reconfiguration.
    delay(500);
    return true;
}
// UI16 launch adapters END

static std::uint16_t ui16_buttons()
{
    std::uint16_t j1=0,j2=0,h1=0,h2=0;
    get_joypad_states(&j1,&j2,&h1,&h2);return j1|j2|h1|h2;
}
static void ui16_wait_release()
{
    while(ui16_buttons())delay(10);
}
static void ui16_restore_menu(Ui16Runtime &runtime,Ui16Browser &browser)
{
    for(;;){
        const auto error=runtime.restore_monitor();
        if(error==Ui16LaunchError::None)break;
        overlay_status("%s; A/B retry",ui16_launch_error_text(error));
        dprint("UI16 restore: %s\n",ui16_launch_error_text(error));
        ui16_wait_release();
        while(!(ui16_buttons()&UI16_BUTTON_CONFIRM))delay(10);
        ui16_wait_release();
    }
    ui16_wait_release();browser.resume_input(0);
}

static Ui16GameList ui16_load_for_core(int core_id, const char *relative_path, void *)
{
    const auto *info = find_core_by_id(core_id);
    if (!info) {
        Ui16GameList result;
        result.status = Ui16ListStatus::DirectoryError;
        return result;
    }
    std::string path = std::string(drv) + info->rom_dir;
    if(relative_path && *relative_path)path += std::string("/") + relative_path;
    auto games = ui16_read_game_list(path.c_str(), core_id);
    ui16_load_titles((path + "/titles.csv").c_str(), games);
    return games;
}
#endif

// Display main menu and call other menu functions
static void main_task(void *pvParameters)
{
    uint32_t last_redraw_time = 0;
    // volatile uint32_t *reg_gpio0 = (volatile uint32_t *)0x200008c4;     // bl616 reference 4.8.5
    // volatile uint32_t *reg_gpio1 = (volatile uint32_t *)0x200008c8;
    // volatile uint32_t *reg_gpio2 = (volatile uint32_t *)0x200008cc;
    // volatile uint32_t *reg_gpio3 = (volatile uint32_t *)0x200008d0;

    // wait for drive to be ready
    uint64_t start = bflb_mtimer_get_time_ms();
    FRESULT res;
    overlay_status("Mounting sd card...", drv);
    while ((res = f_mount(&fs, "sd:", 1)) != FR_OK && bflb_mtimer_get_time_ms() - start < 500)
        delay(100);

    if (res == FR_OK) {
        overlay_status("SD card mounted in %d ms", bflb_mtimer_get_time_ms() - start);
    } else  {
        overlay_status("SD not found. Mounting USB...");
        drv = "usb:";
        start = bflb_mtimer_get_time_ms();
        while ((res = f_mount(&fs, "usb:", 1)) != FR_OK && bflb_mtimer_get_time_ms() - start < 2000)
            delay(100);
        if (res != FR_OK) {
            overlay_status("Failed to mount USB drive");
        } else {
            overlay_status("USB drive mounted in %d ms", bflb_mtimer_get_time_ms() - start);
        }
    }

    // load monitor core at startup
    string fname;
    if (find_core_for_board(fname, "monitor.bin")) {
        const bool monitor_loaded = fpga_program(fname.c_str());
#ifdef UI16_SYSTEM_MENU
        if (!monitor_loaded || !ui16_hw_wait_core(0,nullptr)) {
            overlay_status("UI16: monitor load failed");
            for (;;) delay(1000);
        }
#ifdef UI16_SD_FONT
        // Main UI16 is independent of game OSD; if the pack is absent or corrupt
        // the fixed 319-glyph font remains available.
        const auto font_file = std::string(drv) + "/ui16/font16.ui16";
        ui16_font_open(font_file.c_str());
#endif
        // Keep the configured order; include existing Cores/Options entries.
        std::vector<Ui16MenuEntry> entries;
        entries.reserve(main_menu_config.size());
        std::size_t count = 0;
        for (const auto id : main_menu_config) {
            if (id == 0) break;
            if (id > 0) {
                const auto *info = find_core_by_id(id);
                if (info) entries.push_back({id, info->display_name});
            } else if (id == -1) {
                entries.push_back({id, "CORES"});
            } else if (id == -2) {
                entries.push_back({id, "OPTIONS"});
            }
        }
        delay(1000);
        count=entries.size();
        int initial = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (entries[i].id > 0) { initial = static_cast<int>(i); break; }
        }
        Ui16Browser browser(entries.data(), count, initial);
        Ui16Runtime runtime({nullptr,ui16_hw_file_ready,ui16_hw_find_core,ui16_hw_program,
            ui16_hw_wait_core,ui16_hw_load_rom,ui16_hw_display_owner,ui16_hw_osd_ready,ui16_hw_starting});
        fpga_use_ui16_display(true);
        const auto redraw = [&]() {
            if (browser.in_games()) {
                ui16_send_game_menu(browser.heading(),
                                    browser.game_list(), browser.selected_file(), browser.file_top());
            } else {
                ui16_send_system_menu(entries.data(), count, browser.selected_system(), browser.system_top());
            }
        };
        redraw();
        // A/B launch a ROM; the browser retains its folder, selection and viewport.
        for (;;) {
            uint16_t joy1 = 0, joy2 = 0, hid1 = 0, hid2 = 0;
            get_joypad_states(&joy1, &joy2, &hid1, &hid2);
            const uint16_t buttons = joy1 | joy2 | hid1 | hid2;
            if (browser.update(buttons, static_cast<uint32_t>(bflb_mtimer_get_time_ms()), ui16_load_for_core)) {
                redraw();
            }
            Ui16LaunchRequest request;
            if(browser.take_launch_request(request)){
                auto *target=find_core_by_id(request.core_id);
                const Ui16LaunchTarget launch_target=target?
                    Ui16LaunchTarget{target->id,target->rom_dir,target->core_file}:Ui16LaunchTarget{};
                const auto error=runtime.launch(drv,launch_target,request);
                if(error==Ui16LaunchError::None){
                    // Preserve normal game input; the UI button assignment is not a game remap.
                    ui16_wait_release();
                    for(;;){
                        overlay(0);send_hid_to_core();
                        ui16_wait_release();overlay_osd_window(0,0,0,0);overlay(1);
                        menu_clear();
                        const std::string root=std::string(drv)+target->rom_dir;
                        std::unique_ptr<Menu> popup(target->create_menu?target->create_menu(root.c_str()):create_default_menu(root.c_str()));
                        Menu *menu=popup.get();push_menu(std::move(popup));
                        menu->do_redraw();ui16_osd_menu_input_loop();
                        const bool return_to_menu=overlay_on();
                        menu_clear();
                        if(return_to_menu)break; // Main Menu selected; reload monitor below.
                        ui16_wait_release(); // OSD or X/Y closes the popup and resumes the game.
                    }
                    menu_clear();
                }
                ui16_restore_menu(runtime,browser);
#ifdef UI16_SD_FONT
                // FPGA reprogramming restores the initial embedded font ROM.
                // Never trust a cached dynamic slot after returning from a game.
                ui16_font_monitor_reset();
#endif
                browser.set_launch_error(error==Ui16LaunchError::None?nullptr:ui16_launch_error_text(error));
                redraw();
            }
            delay(10);
        }
#else
        (void)monitor_loaded;
#endif
        // UI16 fixed-frame test BEGIN
        #ifdef UI16_UART_SELFTEST
            extern void ui16_send_selftest();
            delay(1000);
            for (;;) {
                ui16_send_selftest();
                delay(1000);
            }
        #endif
        // UI16 fixed-frame test END
    } else {
        overlay_status("No monitor.bin found for board.");
    }

    int line_start;
    int menu_cnt = main_menu_config.size();
    line_start = 13 - (menu_cnt+2+2) / 2;       // 2 lines for version, 2 lines for "TangCore"

    while (1) {
        bool redraw = true;
        int choice = 0;
        for (;;) {
            uint32_t now = bflb_mtimer_get_time_ms();
            if (active_core == -1) {
                // send_blank_packet();
                active_core = get_core_id();            // 200ms timeout
                overlay_status("core_id=%d", active_core);
                if (active_core >= 0) redraw = true;    // redraw immediately if core is detected
            }
            // if (core < 0) continue;         // do not draw or process input if core is not ready
            if (now - last_redraw_time > 5000)
                redraw = true;
            if (redraw) {
                active_core = get_core_id();            // allow jtag to change core underneath us
                overlay(overlay_on());                  // set correct overlay state
                overlay_clear();

                int line = line_start;
                overlay_cursor(0, line++);
                //              01234567890123456789012345678901
                overlay_printf("       -== TangCore ==-");
                line++;

                // display all menu items
                for (int i = 0; i < menu_cnt; i++) {
                    overlay_cursor(2, line++);
                    if (main_menu_config[i] > 0) {
                        for (int j = 0; core_info_list[j].id != 0; j++) {
                            if (core_info_list[j].id == main_menu_config[i]) {
                                overlay_printf("%s", core_info_list[j].display_name);
                                break;
                            }
                        }
                    } else if (main_menu_config[i] == -1) {
                        overlay_printf("Cores");
                    } else if (main_menu_config[i] == -2) {
                        overlay_printf("Options");
                    }
                }

                line++;
                overlay_cursor(2, line++);
                overlay_printf("Version: ");
                overlay_printf(__DATE__);
                last_redraw_time = now;
                redraw = false;

                // print some debug stats to UART
                // uint16_t joy1=0, joy2=0;
                // get_joypad_states(&joy1, &joy2);
                // overlay_status("core=%d, j1=%04x, j2=%04x", active_core, joy1, joy2);
                // overlay_status("Mtimer frequency: %d MHz", bflb_mtimer_get_freq() / 1000000);
                // overlay_status("CPU frequency: %d MHz", bflb_clk_get_system_clock(BL_SYSTEM_CLOCK_MCU_CLK) / 1000000);
                // overlay_status("GPIO0-3 status: %08x %08x %08x %08x", *reg_gpio0, *reg_gpio1, *reg_gpio2, *reg_gpio3);
            }

            bool before_overlay = overlay_on();
            int r = joy_choice(line_start+2, menu_cnt, &choice, OSD_KEY_CODE);
            if (r == 1) break;

            if (!before_overlay && overlay_on() && active_core > 0) {
                // overlay is turned back on, now display the pop-up menu
                DEBUG("Displaying pop-up menu\n");
                menu_clear();
                // Menu *menu = core->create_menu(core->rom_dir)
                core_info *core = find_core_by_id(active_core);
                if (core != NULL) {
                    DEBUG("Found core_info. Displaying menu\n");
                    Menu *menu;
                    if (active_core == 6) {
                        menu = create_pcxt_menu(std::string(drv).append(core->rom_dir).c_str());
                    } else {
                        menu = create_default_menu(std::string(drv).append(core->rom_dir).c_str());
                    }
                    std::unique_ptr<Menu> menu_ptr(menu);
                    push_menu(std::move(menu_ptr));
                    menu->do_redraw();
                    menu_input_loop();
                    redraw = true;
                }
            }

            delay(20);
        }

        if (main_menu_config[choice] > 0) {
            // Load rom or core from USB drive
            struct core_info *core = NULL;
            for (int i = 0; core_info_list[i].id != 0; i++) {
                if (core_info_list[i].id == main_menu_config[choice]) {
                    core = &core_info_list[i];
                    break;
                }
            }
            if (core) {
                std::string dir = std::string(drv).append(core->rom_dir);
                menu_loadrom(dir.c_str());
            }
        } else if (main_menu_config[choice] == -1) {
            // load cores manually
            std::string dir = std::string(drv).append("cores");
            menu_loadrom(dir.c_str());
        } else if (main_menu_config[choice] == -2) {
            // Options
            menu_options();
        }

        delay(300);
    }
}

static void print_system_info(void) {
    // this is viewable with scripts/liveuart.py
    overlay_status("TangCore %s", __DATE__);
    overlay_status("TangBoard: %s", BOARD_NAME);
    overlay_status("System clock: %u MHz", bflb_clk_get_system_clock(BL_SYSTEM_CLOCK_MCU_CLK) / 1000000);
    // UART registers
    // Clock comes from XCLK/160M/BCLK and goes through a divider and becomes UART_CLK
    overlay_status("GLB_UART_CFG0: %08x", BL_RD_WORD(0x20000150));
    overlay_status("GLB_UART_CFG1: %08x", BL_RD_WORD(0x20000154));
    overlay_status("GLB_UART_CFG2: %08x", BL_RD_WORD(0x20000158));
    overlay_status("UART0 clock: %u", Clock_Peripheral_Clock_Get(BL_PERIPHERAL_CLOCK_UART0));
    overlay_status("UART1 clock: %u", Clock_Peripheral_Clock_Get(BL_PERIPHERAL_CLOCK_UART1));

    // 10.3.5: baudrate = UART_clk / (uart_prd + 1)
    // This causes memory exception.
    // overlay_status("UART_BIT_PRD: %08x", BL_RD_WORD(0x40010008));
    //              01234567890123456789012345678901
    // overlay_status("                                ");
}

// Initialize things, then start main_task and uart1_rx_task to do actual work
#ifdef UI16_RELIABLE_FONT
static std::uint32_t ui16_link_now_ms() { return static_cast<std::uint32_t>(bflb_mtimer_get_time_ms()); }
static void ui16_link_wait_ms() { vTaskDelay(1); }
#endif
int main(void)
{
    /* Board init */
    board_init();
    init_core_list();

    // Initialize GPIO and UART
    init_gpio_and_uart();

    print_system_info();

    // Create mutex for joypad states
    state_mutex = xSemaphoreCreateMutex();

    overlay_status("Initializing SDH...");
    fatfs_sdh_driver_register();        // calls SDH_Init()
    // f_mount(&fs_sd, "sd:", 0);          // registers SDMMC drive

    // Initializing USB host...
    overlay_status("Initializing USB host...");
    usbh_initialize();
    fatfs_usbh_driver_register();
    usb_gamepad_init();

    overlay_status("Creating tasks...");
#ifdef UI16_RELIABLE_FONT
    ui16_link_set_platform(ui16_link_now_ms,ui16_link_wait_ms);
#endif
    // Create the tasks
    xTaskCreate(main_task, "main_task", MAIN_TASK_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &main_task_handle);
    xTaskCreate(uart1_rx_task, "uart1_rx_task", UART1_RX_TASK_STACK_SIZE, NULL, UART1_RX_TASK_PRIORITY, &uart1_rx_task_handle);

    uart1_rx_start();
    vTaskStartScheduler();

    while (1) {
    }
}
