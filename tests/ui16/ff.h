#pragma once
// Minimal host-test FatFs declarations, not used by the firmware build.
#include <stddef.h>
typedef int FRESULT;
enum { FR_OK = 0, FR_DISK_ERR = 1, FR_NO_PATH = 5 };
enum { AM_HID = 0x02, AM_SYS = 0x04, AM_DIR = 0x10 };
typedef struct { size_t position; } DIR;
typedef struct { unsigned char fattrib; char fname[256]; } FILINFO;
FRESULT f_opendir(DIR *dir, const char *path);
FRESULT f_readdir(DIR *dir, FILINFO *info);
FRESULT f_closedir(DIR *dir);
