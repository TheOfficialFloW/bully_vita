#ifndef __CONFIG_H__
#define __CONFIG_H__

// #define DEBUG
// #define HAVE_RAZOR

#define LOAD_ADDRESS 0x98000000

#define MEMORY_SCELIBC_MB 4
#ifdef HAVE_RAZOR
#define MEMORY_NEWLIB_MB 256
#else
#define MEMORY_NEWLIB_MB 176
#endif
#define MEMORY_VITAGL_THRESHOLD_MB 8

#define DATA_PATH "ux0:data/Bully"
#define SO_PATH DATA_PATH "/" "libBully.so"
#define CONFIG_PATH DATA_PATH "/" "config.txt"
#define GLSL_PATH DATA_PATH "/" "glsl"
#define GXP_PATH DATA_PATH "/" "gxp"

#define SCREEN_W 960
#define SCREEN_H 544

#define TOUCH_X_MARGIN 100

#endif
