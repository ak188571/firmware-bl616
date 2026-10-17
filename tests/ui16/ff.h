#pragma once
// Minimal host-test FatFs declarations, not used by the firmware build.
#include <stddef.h>
typedef int FRESULT;
enum { FR_OK = 0, FR_DISK_ERR = 1, FR_NO_PATH = 5 };
enum { FR_NO_FILE = 4, FA_READ = 1 };
typedef unsigned int UINT;
typedef struct { size_t position; } FIL;
FRESULT f_open(FIL *file, const char *path, unsigned char mode);
FRESULT f_read(FIL *file, void *buffer, UINT length, UINT *read);
FRESULT f_close(FIL *file);
enum { AM_HID = 0x02, AM_SYS = 0x04, AM_DIR = 0x10 };
typedef struct { size_t position; } DIR;
typedef struct { unsigned char fattrib; char fname[256]; } FILINFO;
FRESULT f_opendir(DIR *dir, const char *path);
FRESULT f_readdir(DIR *dir, FILINFO *info);
FRESULT f_closedir(DIR *dir);
