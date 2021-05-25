/* movie.c -- Video playback for movies
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/avplayer.h>
#include <psp2/audioout.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <taihen.h>
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

#include "so_util.h"
#include "movie.h"

#include "shaders/movie_f.h"
#include "shaders/movie_v.h"

#define FB_ALIGNMENT 0x40000

enum {
  PLAYER_STOPPED,
  PLAYER_READY,
  PLAYER_ACTIVE
};

int (* OS_FileOpen)(int area, void **handle, char const *file, int access);
int (* OS_FileRead)(void *handle, void *data, int size);
int (* OS_FileSetPosition)(void *handle, int pos);
int (* OS_FileSize)(void *handle);
int (* OS_FileClose)(void *handle);

GLuint movie_frame[2];
uint8_t movie_frame_idx = 0;
SceGxmTexture *movie_tex[2];
int player_state = PLAYER_STOPPED;
GLuint movie_fs;
GLuint movie_vs;
GLuint movie_prog;
int audio_port;
SceAvPlayerHandle movie_player;
void *handle = NULL;

float movie_pos[8] = {
  -1.0f, 1.0f,
  -1.0f, -1.0f,
   1.0f, 1.0f,
   1.0f, -1.0f
};

float movie_texcoord[8] = {
  0.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 0.0f,
  1.0f, 1.0f
};

int open_file_cb(void *jumpback, const char *argFilename) {
  return (OS_FileOpen(0, &handle, argFilename, 0) == 0) ? 0 : -1;
}

int close_file_cb(void *jumpback) {
  return (OS_FileClose(handle) == 0) ? 0 : -1;
}

int read_file_cb(void *jumpback, uint8_t *argBuffer, uint64_t argPosition, uint32_t argLength) {
  int res = OS_FileSetPosition(handle, (int)argPosition);
  // debugPrintf("OS_FileSetPosition %llx: %d\n", argPosition, res);
  if (res != 0)
    return -1;
  res = OS_FileRead(handle, argBuffer, argLength);
  // debugPrintf("OS_FileRead %x: %d\n", argLength, res);
  return (res == 0) ? argLength : -1;
}

uint64_t size_file_cb(void *jumpback) {
  return (uint64_t)OS_FileSize(handle);
}

int audio_thread(SceSize args, void *ThisObject) {
  SceAvPlayerFrameInfo audioFrame;
  memset(&audioFrame, 0, sizeof(SceAvPlayerFrameInfo));
  
  for (;;) {
    while (sceAvPlayerIsActive(movie_player)) {
      if (sceAvPlayerGetAudioData(movie_player, &audioFrame)) {
        sceAudioOutSetConfig(audio_port, -1, /*audioFrame.details.audio.sampleRate*/-1, audioFrame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
        sceAudioOutOutput(audio_port, audioFrame.pData);
      } else {
        sceKernelDelayThread(1000);
      }
    }
  }

  return sceKernelExitDeleteThread(0);
}

int video_thread(SceSize args, void *ThisObject) {
  SceAvPlayerFrameInfo videoFrame;
  memset(&videoFrame, 0, sizeof(SceAvPlayerFrameInfo));
  
  for (;;) {
    while (sceAvPlayerIsActive(movie_player)) {
      if (sceAvPlayerGetVideoData(movie_player, &videoFrame)) {
        uint8_t next_movie_frame_idx = (movie_frame_idx + 1) % 2;
        sceGxmTextureInitLinear(
          movie_tex[next_movie_frame_idx],
          videoFrame.pData,
          SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
          videoFrame.details.video.width,
          videoFrame.details.video.height, 0);
        movie_frame_idx = next_movie_frame_idx;
        player_state = PLAYER_ACTIVE;
      }
    }
    sceKernelDelayThread(1000);
  }

  return sceKernelExitDeleteThread(0);
}

void *mem_alloc(void *p, uint32_t alignment, uint32_t size) {
  return memalign(alignment, size);
}

void mem_free(void *p, void *ptr) {
  free(ptr);
}

void *gpu_alloc(void *p, uint32_t alignment, uint32_t size) {
  if (alignment < FB_ALIGNMENT) {
    alignment = FB_ALIGNMENT;
  }
  size = ALIGN_MEM(size, alignment);
  size = ALIGN_MEM(size, 1024 * 1024);
  SceUID memblock = sceKernelAllocMemBlock("Video Memblock", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
  
  void *res;
  sceKernelGetMemBlockBase(memblock, &res);
  sceGxmMapMemory(res, size, (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE));
  
  return res;
}

void gpu_free(void *p, void *ptr) {
  SceUID memblock = sceKernelFindMemBlockByAddr(ptr, 0);
  sceKernelFreeMemBlock(memblock);
}

void movie_draw_frame(void) {
  if (player_state == PLAYER_ACTIVE) {
    glUseProgram(movie_prog);
    glBindTexture(GL_TEXTURE_2D, movie_frame[movie_frame_idx]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, &movie_pos[0]);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, &movie_texcoord[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    vglSwapBuffers(GL_FALSE);
  }
}

void movie_setup_player(void) {
  glGenTextures(2, movie_frame);
  for (int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, movie_frame[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    movie_tex[i] = vglGetGxmTexture(GL_TEXTURE_2D);
    vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
  }
  
  movie_vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderBinary(1, &movie_vs, 0, movie_v, size_movie_v);
  
  movie_fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderBinary(1, &movie_fs, 0, movie_f, size_movie_f);
  
  movie_prog = glCreateProgram();
  glAttachShader(movie_prog, movie_vs);
  glAttachShader(movie_prog, movie_fs);
  glBindAttribLocation(movie_prog, 0, "inPos");
  glBindAttribLocation(movie_prog, 1, "inTex");
  glLinkProgram(movie_prog);
}

int OS_MoviePlay(const char *file, int a2, int a3, float a4) {
  if (player_state == PLAYER_STOPPED) {
    sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);

    audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    sceAudioOutSetConfig(audio_port, -1, -1, (SceAudioOutMode)-1);
  
    // Setting audio channel volume
    int vol_stereo[] = {32767, 32767};
    sceAudioOutSetVolume(audio_port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol_stereo);

    SceAvPlayerInitData playerInit;
    memset(&playerInit, 0, sizeof(SceAvPlayerInitData));

    playerInit.memoryReplacement.allocate = mem_alloc;
    playerInit.memoryReplacement.deallocate = mem_free;
    playerInit.memoryReplacement.allocateTexture = gpu_alloc;
    playerInit.memoryReplacement.deallocateTexture = gpu_free;

    playerInit.fileReplacement.objectPointer = NULL;
    playerInit.fileReplacement.open = open_file_cb;
    playerInit.fileReplacement.close = close_file_cb;
    playerInit.fileReplacement.readOffset = read_file_cb;
    playerInit.fileReplacement.size = size_file_cb;
  
    playerInit.basePriority = 0xA0;
    playerInit.numOutputVideoFrameBuffers = 2;
    playerInit.autoStart = GL_TRUE;

    movie_player = sceAvPlayerInit(&playerInit);
    sceAvPlayerAddSource(movie_player, file);

    SceUID audioThreadId = sceKernelCreateThread("movie audio thread", audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
    sceKernelStartThread(audioThreadId, 0, NULL);

    SceUID videoThreadId = sceKernelCreateThread("movie video thread", video_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
    sceKernelStartThread(videoThreadId, 0, NULL);
  
    player_state = PLAYER_READY;
  } else {
    sceAvPlayerAddSource(movie_player, file);
  }
  
  return 0;
}

void OS_MovieSetSkippable(void) {

}

void OS_MovieStop(void) {
  sceAvPlayerStop(movie_player);
  player_state = PLAYER_READY;
}

int OS_MovieIsPlaying(int *loops) {
  return player_state && sceAvPlayerIsActive(movie_player);
}

void patch_movie(void) {
  OS_FileOpen = (void *)so_find_addr("_Z11OS_FileOpen14OSFileDataAreaPPvPKc16OSFileAccessType");
  OS_FileRead = (void *)so_find_addr("_Z11OS_FileReadPvS_i");
  OS_FileSetPosition = (void *)so_find_addr("_Z18OS_FileSetPositionPvi");
  OS_FileSize = (void *)so_find_addr("_Z11OS_FileSizePv");
  OS_FileClose = (void *)so_find_addr("_Z12OS_FileClosePv");
  
  hook_thumb(so_find_addr("_Z12OS_MoviePlayPKcbbf"), (uintptr_t)OS_MoviePlay);
  hook_thumb(so_find_addr("_Z20OS_MovieSetSkippableb"), (uintptr_t)OS_MovieSetSkippable);
  hook_thumb(so_find_addr("_Z12OS_MovieStopv"), (uintptr_t)OS_MovieStop);
  hook_thumb(so_find_addr("_Z17OS_MovieIsPlayingPi"), (uintptr_t)OS_MovieIsPlaying);
}
