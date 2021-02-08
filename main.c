/* main.c -- Bully .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>

#include <malloc.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include <math.h>
#include <math_neon.h>

#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "main.h"
#include "config.h"
#include "so_util.h"
#include "jni_patch.h"
#include "openal_patch.h"
#include "opengl_patch.h"

int _newlib_heap_size_user = MEMORY_MB * 1024 * 1024;

SceTouchPanelInfo panelInfoFront, panelInfoBack;

int debugPrintf(char *text, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, text);
  vsprintf(string, text, list);
  va_end(list);

  SceUID fd = sceIoOpen("ux0:data/bully_log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
  if (fd >= 0) {
    sceIoWrite(fd, string, strlen(string));
    sceIoClose(fd);
  }

  return 0;
}

void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, fmt);
  vsprintf(string, fmt, list);
  va_end(list);

  debugPrintf("[LOG] %s %s: %s\n", cond, tag, string);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  va_list list;
  static char string[0x1000];

  va_start(list, fmt);
  vsprintf(string, fmt, list);
  va_end(list);

  debugPrintf("[LOG] %s: %s\n", tag, string);

  return 0;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
  static char string[0x1000];
  vsprintf(string, fmt, ap);
  debugPrintf("[LOG] %s: %s\n", tag, string);
  return 0;
}

int __android_log_write(int prio, const char *tag, const char *text) {
  debugPrintf("[LOG] %s: %s\n", tag, text);
  return 0;
}

int ret0(void) {
  return 0;
}

int ret1(void) {
  return 1;
}

int mkdir(const char *pathname, mode_t mode) {
  if (sceIoMkdir(pathname, mode) < 0)
    return -1;
  return 0;
}

int OS_ScreenGetHeight(void) {
  return SCREEN_H;
}

int OS_ScreenGetWidth(void) {
  return SCREEN_W;
}

int ProcessEvents(void) {
  return 0; // 1 is exit!
}

// only used for NVEventAppMain
int pthread_create_fake(int r0, int r1, int r2, void *arg) {
  int (* func)() = *(void **)(arg + 4);
  return func();
}

int pthread_mutex_init_fake(SceKernelLwMutexWork **work) {
  *work = (SceKernelLwMutexWork *)memalign(8, sizeof(SceKernelLwMutexWork));
  if (sceKernelCreateLwMutex(*work, "mutex", SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, NULL) < 0)
    return -1;
  return 0;
}

int pthread_mutex_destroy_fake(SceKernelLwMutexWork **work) {
  if (sceKernelDeleteLwMutex(*work) < 0)
    return -1;
  free(*work);
  return 0;
}

int pthread_mutex_lock_fake(SceKernelLwMutexWork **work) {
  if (!*work)
    pthread_mutex_init_fake(work);
  if (sceKernelLockLwMutex(*work, 1, NULL) < 0)
    return -1;
  return 0;
}

int pthread_mutex_unlock_fake(SceKernelLwMutexWork **work) {
  if (sceKernelUnlockLwMutex(*work, 1) < 0)
    return -1;
  return 0;
}

int sem_init_fake(int *uid) {
  *uid = sceKernelCreateSema("sema", 0, 0, 0x7fffffff, NULL);
  if (*uid < 0)
    return -1;
  return 0;
}

int sem_post_fake(int *uid) {
  if (sceKernelSignalSema(*uid, 1) < 0)
    return -1;
  return 0;
}

int sem_wait_fake(int *uid) {
  if (sceKernelWaitSema(*uid, 1, NULL) < 0)
    return -1;
  return 0;
}

int sem_trywait_fake(int *uid) {
  SceUInt timeout = 0;
  if (sceKernelWaitSema(*uid, 1, &timeout) < 0)
    return -1;
  return 0;
}

int sem_destroy_fake(int *uid) {
  if (sceKernelDeleteSema(*uid) < 0)
    return -1;
  return 0;
}

int thread_stub(SceSize args, uintptr_t *argp) {
  int (* func)(void *arg) = (void *)argp[0];
  void *arg = (void *)argp[1];
  char *out = (char *)argp[2];
  out[0x41] = 1; // running
  func(arg);
  return sceKernelExitDeleteThread(0);
}

void *OS_ThreadLaunch(int (* func)(), void *arg, int r2, char *name, int r4, int priority) {
  int vita_priority;

  switch (priority) {
    case 0:
      vita_priority = 0x40;
      break;
    case 1:
      vita_priority = 0x10000100 - 31;
      break;
    case 2:
      vita_priority = 0x10000100 - 15;
      break;
    case 3:
      vita_priority = 0x10000100;
      break;
    default:
      vita_priority = 0x10000100;
      break;
  }

  debugPrintf("Launching thread %s\n", name);

  SceUID thid = sceKernelCreateThread(name, (SceKernelThreadEntry)thread_stub, vita_priority, 128 * 1024, 0, 0, NULL);
  if (thid >= 0) {
    char *out = malloc(0x48);
    *(int *)(out + 0x24) = thid;

    uintptr_t args[3];
    args[0] = (uintptr_t)func;
    args[1] = (uintptr_t)arg;
    args[2] = (uintptr_t)out;
    sceKernelStartThread(thid, sizeof(args), args);

    return out;
  }

  return NULL;
}

void OS_ThreadWait(void *thread) {
  if (thread)
    sceKernelWaitThreadEnd(*(int *)(thread + 0x24), NULL, NULL);
}

extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;

void patch_game(void) {
  hook_thumb(so_find_addr("__cxa_guard_acquire"), (uintptr_t)&__cxa_guard_acquire);
  hook_thumb(so_find_addr("__cxa_guard_release"), (uintptr_t)&__cxa_guard_release);

  hook_thumb(so_find_addr("_Z24NVThreadGetCurrentJNIEnvv"), (uintptr_t)NVThreadGetCurrentJNIEnv);

  // do not use pthread
  hook_thumb(so_find_addr("_Z15OS_ThreadLaunchPFjPvES_jPKci16OSThreadPriority"), (uintptr_t)OS_ThreadLaunch);
  hook_thumb(so_find_addr("_Z13OS_ThreadWaitPv"), (uintptr_t)OS_ThreadWait);

  hook_thumb(so_find_addr("_Z17OS_ScreenGetWidthv"), (uintptr_t)OS_ScreenGetWidth);
  hook_thumb(so_find_addr("_Z18OS_ScreenGetHeightv"), (uintptr_t)OS_ScreenGetHeight);

  // TODO: set deviceChip, definedDevice
  hook_thumb(so_find_addr("_Z20AND_SystemInitializev"), (uintptr_t)ret0);

  // TODO: implement touch here
  hook_thumb(so_find_addr("_Z13ProcessEventsb"), (uintptr_t)ProcessEvents);
}

extern void *__cxa_atexit;
extern void *__cxa_finalize;

static const short _C_tolower_[] = {
  -1,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
  0x40, 'a',  'b',  'c',  'd',  'e',  'f',  'g',
  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
  'x',  'y',  'z',  0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
  0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
  0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
  0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
  0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
  0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
  0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
  0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

const short *_tolower_tab_ = _C_tolower_;

static char *__ctype_ = (char *)&_ctype_;

static FILE __sF_fake[0x100][3];

int __isfinitef(float d) {
  return isfinite(d);
}

FILE *fopen_hook(const char *filename, const char *mode) {
  FILE *file = fopen(filename, mode);
  debugPrintf("fopen %s: %p\n", filename, file);
  return file;
}

void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * data) {
  if (level == 0)
    glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
}

void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * data) {
  if (level == 0)
    glCompressedTexImage2D(target, level, format, width, height, border, imageSize, data);
}

static DynLibFunction dynlib_functions[] = {
  { "__android_log_assert", (uintptr_t)&__android_log_assert },
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
  { "__android_log_write", (uintptr_t)&__android_log_write },

  { "__cxa_atexit", (uintptr_t)&__cxa_atexit },
  { "__cxa_finalize", (uintptr_t)&__cxa_finalize },
  { "__errno", (uintptr_t)&__errno },
  // { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
  // { "__gnu_Unwind_Find_exidx", (uintptr_t)&__gnu_Unwind_Find_exidx },
  // { "__google_potentially_blocking_region_begin", (uintptr_t)&__google_potentially_blocking_region_begin },
  // { "__google_potentially_blocking_region_begin", (uintptr_t)&__google_potentially_blocking_region_begin },
  // { "__google_potentially_blocking_region_end", (uintptr_t)&__google_potentially_blocking_region_end },
  // { "__google_potentially_blocking_region_end", (uintptr_t)&__google_potentially_blocking_region_end },
  { "__isfinitef", (uintptr_t)&__isfinitef },
  { "__sF", (uintptr_t)&__sF_fake },
  { "_ctype_", (uintptr_t)&__ctype_ },
  { "_tolower_tab_", (uintptr_t)&_tolower_tab_ },

  { "AAsset_close", (uintptr_t)&ret0 },
  { "AAsset_getLength", (uintptr_t)&ret0 },
  { "AAsset_getRemainingLength", (uintptr_t)&ret0 },
  { "AAsset_read", (uintptr_t)&ret0 },
  { "AAsset_seek", (uintptr_t)&ret0 },
  { "AAssetManager_fromJava", (uintptr_t)&ret0 },
  { "AAssetManager_open", (uintptr_t)&ret0 },

  { "abort", (uintptr_t)&abort },
  { "exit", (uintptr_t)&exit },

  // { "accept", (uintptr_t)&accept },
  // { "bind", (uintptr_t)&bind },
  // { "connect", (uintptr_t)&connect },
  // { "gethostbyaddr", (uintptr_t)&gethostbyaddr },
  // { "gethostbyname", (uintptr_t)&gethostbyname },
  // { "getsockname", (uintptr_t)&getsockname },
  // { "getsockopt", (uintptr_t)&getsockopt },
  // { "inet_aton", (uintptr_t)&inet_aton },
  // { "inet_ntoa", (uintptr_t)&inet_ntoa },
  // { "listen", (uintptr_t)&listen },
  // { "recvmsg", (uintptr_t)&recvmsg },
  // { "sendmsg", (uintptr_t)&sendmsg },
  // { "setsockopt", (uintptr_t)&setsockopt },
  // { "shutdown", (uintptr_t)&shutdown },
  // { "socket", (uintptr_t)&socket },

  { "acosf", (uintptr_t)&acosf },
  { "asinf", (uintptr_t)&asinf },
  { "atan", (uintptr_t)&atan },
  { "atan2", (uintptr_t)&atan2 },
  { "atan2f", (uintptr_t)&atan2f },
  { "atanf", (uintptr_t)&atanf },
  { "cbrt", (uintptr_t)&cbrt },
  { "ceilf", (uintptr_t)&ceilf },
  { "cos", (uintptr_t)&cos },
  { "cosf", (uintptr_t)&cosf },
  { "exp", (uintptr_t)&exp },
  { "floor", (uintptr_t)&floor },
  { "floorf", (uintptr_t)&floorf },
  { "fmod", (uintptr_t)&fmod },
  { "fmodf", (uintptr_t)&fmodf },
  { "ldexp", (uintptr_t)&ldexp },
  { "log", (uintptr_t)&log },
  { "log10f", (uintptr_t)&log10f },
  { "logf", (uintptr_t)&logf },
  { "pow", (uintptr_t)&pow },
  { "powf", (uintptr_t)&powf },
  { "sin", (uintptr_t)&sin },
  { "sinf", (uintptr_t)&sinf },
  { "sqrtf", (uintptr_t)&sqrtf },
  { "tan", (uintptr_t)&tan },
  { "tanf", (uintptr_t)&tanf },

  { "calloc", (uintptr_t)&calloc },
  { "free", (uintptr_t)&free },
  { "malloc", (uintptr_t)&malloc },
  { "memalign", (uintptr_t)&memalign },
  { "realloc", (uintptr_t)&realloc },

  { "atoi", (uintptr_t)&atoi },

  // { "clock_gettime", (uintptr_t)&clock_gettime },
  { "gettimeofday", (uintptr_t)&gettimeofday },
  { "localtime_r", (uintptr_t)&localtime_r },
  { "time", (uintptr_t)&time },

  // { "close", (uintptr_t)&close },
  // { "closedir", (uintptr_t)&closedir },
  // { "ioctl", (uintptr_t)&ioctl },
  // { "lseek", (uintptr_t)&lseek },
  { "mkdir", (uintptr_t)&mkdir },
  // { "open", (uintptr_t)&open },
  // { "opendir", (uintptr_t)&opendir },
  // { "read", (uintptr_t)&read },
  // { "readdir", (uintptr_t)&readdir },
  { "stat", (uintptr_t)&stat },
  // { "write", (uintptr_t)&write },

  { "eglGetCurrentContext", (uintptr_t)&ret0 },
  // { "eglGetDisplay", (uintptr_t)&eglGetDisplay },
  // { "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
  // { "eglQueryString", (uintptr_t)&eglQueryString },

  { "fclose", (uintptr_t)&fclose },
  { "fdopen", (uintptr_t)&fdopen },
  { "fflush", (uintptr_t)&fflush },
  { "fgetc", (uintptr_t)&fgetc },
  { "fgets", (uintptr_t)&fgets },

  { "fopen", (uintptr_t)&fopen },
  { "fprintf", (uintptr_t)&fprintf },
  { "fputc", (uintptr_t)&fputc },
  { "fputs", (uintptr_t)&fputs },
  { "fread", (uintptr_t)&fread },
  { "fseek", (uintptr_t)&fseek },
  { "ftell", (uintptr_t)&ftell },
  { "fwrite", (uintptr_t)&fwrite },

  { "getc", (uintptr_t)&getc },
  { "ungetc", (uintptr_t)&ungetc },

  { "getenv", (uintptr_t)&getenv },
  // { "gettid", (uintptr_t)&gettid },

  { "glActiveTexture", (uintptr_t)&glActiveTexture },
  { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
  { "glBindBuffer", (uintptr_t)&glBindBuffer },
  { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindRenderbuffer", (uintptr_t)&ret0 },
  { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBlendFuncSeparate", (uintptr_t)&glBlendFuncSeparate },
  { "glBufferData", (uintptr_t)&glBufferData },
  { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor },
  { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil },
  { "glColorMask", (uintptr_t)&glColorMask },
  { "glCompileShader", (uintptr_t)&glCompileShader },
  { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2DHook },
  { "glCompressedTexSubImage2D", (uintptr_t)&ret0 }, // TODO
  { "glCreateProgram", (uintptr_t)&glCreateProgram },
  { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace },
  { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
  { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&ret0 },
  { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures },
  { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask },
  { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
  { "glDrawElements", (uintptr_t)&glDrawElements },
  { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFinish", (uintptr_t)&glFinish },
  { "glFramebufferRenderbuffer", (uintptr_t)&ret0 },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
  { "glFrontFace", (uintptr_t)&glFrontFace },
  { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
  { "glGenRenderbuffers", (uintptr_t)&ret0 },
  { "glGenTextures", (uintptr_t)&glGenTextures },
  { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetBooleanv", (uintptr_t)&glGetBooleanv },
  { "glGetError", (uintptr_t)&glGetError },
  { "glGetIntegerv", (uintptr_t)&glGetIntegerv },
  { "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
  { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv },
  { "glGetString", (uintptr_t)&glGetString },
  { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glLineWidth", (uintptr_t)&glLineWidth },
  { "glLinkProgram", (uintptr_t)&glLinkProgram },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset },
  { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glRenderbufferStorage", (uintptr_t)&ret0 },
  { "glScissor", (uintptr_t)&glScissor },
  { "glShaderSource", (uintptr_t)&glShaderSource },
  { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilMask", (uintptr_t)&glStencilMask },
  { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glTexImage2D", (uintptr_t)&glTexImage2DHook },
  { "glTexParameterf", (uintptr_t)&glTexParameterf },
  { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D },
  { "glUniform1i", (uintptr_t)&glUniform1i },
  { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
  { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
  { "glViewport", (uintptr_t)&glViewport },

  { "longjmp", (uintptr_t)&longjmp },
  { "setjmp", (uintptr_t)&setjmp },

  { "lrand48", (uintptr_t)&lrand48 },
  { "srand48", (uintptr_t)&srand48 },

  { "memchr", (uintptr_t)&memchr },
  { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy },
  { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },

  // { "nanosleep", (uintptr_t)&nanosleep },
  { "usleep", (uintptr_t)&usleep },

  { "printf", (uintptr_t)&printf },

  { "pthread_attr_destroy", (uintptr_t)&ret0 },
  // { "pthread_attr_getschedparam", (uintptr_t)&pthread_attr_getschedparam },
  // { "pthread_attr_getstacksize", (uintptr_t)&pthread_attr_getstacksize },
  // { "pthread_attr_init", (uintptr_t)&pthread_attr_init },
  // { "pthread_attr_setschedparam", (uintptr_t)&pthread_attr_setschedparam },
  // { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize },
  // { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast },
  // { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy },
  { "pthread_cond_init", (uintptr_t)&ret0 },
  // { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal },
  // { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait },
  // { "pthread_cond_timeout_np", (uintptr_t)&pthread_cond_timeout_np },
  // { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait },
  { "pthread_create", (uintptr_t)&pthread_create_fake },
  { "pthread_getspecific", (uintptr_t)&ret0 },
  // { "pthread_join", (uintptr_t)&pthread_join },
  { "pthread_key_create", (uintptr_t)&ret0 },
  // { "pthread_key_delete", (uintptr_t)&pthread_key_delete },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },
  { "pthread_mutexattr_destroy", (uintptr_t)&ret0 },
  { "pthread_mutexattr_init", (uintptr_t)&ret0 },
  { "pthread_mutexattr_settype", (uintptr_t)&ret0 },
  // { "pthread_once", (uintptr_t)&pthread_once },
  // { "pthread_self", (uintptr_t)&pthread_self },
  // { "pthread_setname_np", (uintptr_t)&pthread_setname_np },
  // { "pthread_setschedparam", (uintptr_t)&pthread_setschedparam },
  { "pthread_setspecific", (uintptr_t)&ret0 },

  // { "sem_destroy", (uintptr_t)&sem_destroy_fake },
  // { "sem_getvalue", (uintptr_t)&sem_getvalue },
  // { "sem_init", (uintptr_t)&sem_init_fake },
  // { "sem_post", (uintptr_t)&sem_post_fake },
  // { "sem_trywait", (uintptr_t)&sem_trywait_fake },
  // { "sem_wait", (uintptr_t)&sem_wait_fake },

  { "putchar", (uintptr_t)&putchar },
  { "puts", (uintptr_t)&puts },

  { "bsearch", (uintptr_t)&bsearch },
  { "qsort", (uintptr_t)&qsort },

  // { "raise", (uintptr_t)&raise },
  // { "select", (uintptr_t)&select },
  { "sigaction", (uintptr_t)&ret0 },

  // { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max },
  // { "sched_get_priority_min", (uintptr_t)&sched_get_priority_min },
  // { "sched_yield", (uintptr_t)&sched_yield },

  // { "setlocale", (uintptr_t)&setlocale },

  { "snprintf", (uintptr_t)&snprintf },
  { "sprintf", (uintptr_t)&sprintf },

  { "sscanf", (uintptr_t)&sscanf },

  { "strcasecmp", (uintptr_t)&strcasecmp },
  { "strcat", (uintptr_t)&strcat },
  { "strchr", (uintptr_t)&strchr },
  { "strcmp", (uintptr_t)&strcmp },
  { "strcoll", (uintptr_t)&strcoll },
  { "strcpy", (uintptr_t)&strcpy },
  { "strcspn", (uintptr_t)&strcspn },
  { "strerror", (uintptr_t)&strerror },
  { "strftime", (uintptr_t)&strftime },
  { "strlen", (uintptr_t)&strlen },
  { "strncasecmp", (uintptr_t)&strncasecmp },
  { "strncat", (uintptr_t)&strncat },
  { "strncmp", (uintptr_t)&strncmp },
  { "strncpy", (uintptr_t)&strncpy },
  { "strpbrk", (uintptr_t)&strpbrk },
  { "strrchr", (uintptr_t)&strrchr },
  { "strspn", (uintptr_t)&strspn },
  { "strstr", (uintptr_t)&strstr },
  { "strtod", (uintptr_t)&strtod },
  { "strtok", (uintptr_t)&strtok },
  { "strtol", (uintptr_t)&strtol },
  { "strtoul", (uintptr_t)&strtoul },
  { "strxfrm", (uintptr_t)&strxfrm },

  // { "syscall", (uintptr_t)&syscall },
  // { "sysconf", (uintptr_t)&sysconf },

  // { "touchsensesdk_addResource", (uintptr_t)&touchsensesdk_addResource },
  // { "touchsensesdk_create", (uintptr_t)&touchsensesdk_create },
  // { "touchsensesdk_play", (uintptr_t)&touchsensesdk_play },
  // { "touchsensesdk_stop", (uintptr_t)&touchsensesdk_stop },

  { "vprintf", (uintptr_t)&vprintf },
  { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf },

  { "btowc", (uintptr_t)&btowc },
  { "iswctype", (uintptr_t)&iswctype },
  { "mbrtowc", (uintptr_t)&mbrtowc },
  { "towlower", (uintptr_t)&towlower },
  { "towupper", (uintptr_t)&towupper },

  { "wcrtomb", (uintptr_t)&wcrtomb },
  { "wcscoll", (uintptr_t)&wcscoll },
  { "wcsftime", (uintptr_t)&wcsftime },
  { "wcslen", (uintptr_t)&wcslen },
  { "wcsxfrm", (uintptr_t)&wcsxfrm },
  { "wctob", (uintptr_t)&wctob },
  { "wctype", (uintptr_t)&wctype },
  { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcmp", (uintptr_t)&wmemcmp },
  { "wmemcpy", (uintptr_t)&wmemcpy },
  { "wmemmove", (uintptr_t)&wmemmove },
  { "wmemset", (uintptr_t)&wmemset },
};

int main(int argc, char *argv[]) {
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panelInfoFront);
  sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &panelInfoBack);

  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  so_load(SO_PATH);
  so_relocate();
  so_resolve(dynlib_functions, sizeof(dynlib_functions) / sizeof(DynLibFunction), 1);

  patch_openal();
  patch_opengl();
  patch_game();
  so_flush_caches();

  so_execute_init_array();
  so_free_temp();

  vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
  vglSetParamBufferSize(2 * 1024 * 1024);
  vglInitExtended(0, SCREEN_W, SCREEN_H, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
  vglUseVram(GL_TRUE);

  jni_load();

  return 0;
}
