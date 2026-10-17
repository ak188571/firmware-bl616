#pragma once

extern int _overlay_on;
extern int overlay_on();
extern void overlay_cursor(int x, int y);
extern void dprint(const char *fmt, ...);
extern void overlay_printf(const char *fmt, ...);
extern void overlay_clear();
extern void overlay_status(const char *fmt, ...);
extern void overlay_message(const char *msg, int center);
extern void overlay(int state);     // turn overlay on/off

#ifdef UI16_SYSTEM_MENU
void overlay_osd_capture_begin();
void overlay_osd_capture_end();
void overlay_osd_window(unsigned x,unsigned y,unsigned width,unsigned height);
void overlay_osd_key(unsigned key);
void overlay_osd_neutral();
#endif
