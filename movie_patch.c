/* movie_patch.c -- Video playback for movies
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "so_util.h"

#include "shaders/movie_f.h"
#include "shaders/movie_v.h"

#define FB_ALIGNMENT 0x40000

enum {
  PLAYER_INACTIVE,
  PLAYER_ACTIVE,
  PLAYER_STOP,
};

int (* OS_FileOpen)(int area, void **handle, char const *file, int access);
int (* OS_FileRead)(void *handle, void *data, int size);
int (* OS_FileSetPosition)(void *handle, int pos);
int (* OS_FileSize)(void *handle);
int (* OS_FileClose)(void *handle);

SceAvPlayerHandle movie_player;

int player_state = PLAYER_INACTIVE;

GLuint movie_frame[2];
uint8_t movie_frame_idx = 0;
SceGxmTexture *movie_tex[2];
GLuint movie_fs;
GLuint movie_vs;
GLuint movie_prog;

SceUID audio_thid;
int audio_new;
int audio_port;
int audio_len;
int audio_freq;
int audio_mode;

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

void *file_handle = NULL;

int open_file_cb(void *p, const char *file) {
  return OS_FileOpen(0, &file_handle, file, 0) == 0 ? 0 : -1;
}

int close_file_cb(void *p) {
  return OS_FileClose(file_handle) == 0 ? 0 : -1;
}

int read_file_cb(void *p, uint8_t *buf, uint64_t off, uint32_t len) {
  if (OS_FileSetPosition(file_handle, (int)off) != 0)
    return -1;
  return OS_FileRead(file_handle, buf, len) == 0 ? len : -1;
}

uint64_t size_file_cb(void *p) {
  return (uint64_t)OS_FileSize(file_handle);
}

void *mem_alloc(void *p, uint32_t align, uint32_t size) {
  return memalign(align, size);
}

void mem_free(void *p, void *ptr) {
  free(ptr);
}

void *gpu_alloc(void *p, uint32_t align, uint32_t size) {
  if (align < FB_ALIGNMENT) {
    align = FB_ALIGNMENT;
  }
  size = ALIGN_MEM(size, align);
  size = ALIGN_MEM(size, 1024 * 1024);
  SceUID memblock = sceKernelAllocMemBlock("Video Memblock", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);

  void *res;
  sceKernelGetMemBlockBase(memblock, &res);
  sceGxmMapMemory(res, size, (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE));

  return res;
}

void gpu_free(void *p, void *ptr) {
  glFinish();
  SceUID memblock = sceKernelFindMemBlockByAddr(ptr, 0);
  sceGxmUnmapMemory(ptr);
  sceKernelFreeMemBlock(memblock);
}

void movie_audio_init(void) {
  audio_port = -1;
  for (int i = 0; i < 8; i++) {
    if (sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN) >= 0) {
      audio_port = i;
      break;
    }
  }

  if (audio_port == -1) {
    audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    audio_new = 1;
  } else {
    audio_len = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
    audio_freq = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
    audio_mode = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);
    audio_new = 0;
  }
}

void movie_audio_shutdown(void) {
  if (audio_new) {
    sceAudioOutReleasePort(audio_port);
  } else {
    sceAudioOutSetConfig(audio_port, audio_len, audio_freq, audio_mode);
  }
}

int movie_audio_thread(SceSize args, void *argp) {
  SceAvPlayerFrameInfo frame;
  memset(&frame, 0, sizeof(SceAvPlayerFrameInfo));

  while (player_state == PLAYER_ACTIVE && sceAvPlayerIsActive(movie_player)) {
    if (sceAvPlayerGetAudioData(movie_player, &frame)) {
      sceAudioOutSetConfig(audio_port, 1024, frame.details.audio.sampleRate, frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
      sceAudioOutOutput(audio_port, frame.pData);
    } else {
      sceKernelDelayThread(1000);
    }
  }

  return sceKernelExitDeleteThread(0);
}

void movie_draw_frame(void) {
  if (player_state == PLAYER_ACTIVE) {
    if (sceAvPlayerIsActive(movie_player)) {
      SceAvPlayerFrameInfo frame;
      if (sceAvPlayerGetVideoData(movie_player, &frame)) {
        movie_frame_idx = (movie_frame_idx + 1) % 2;
        sceGxmTextureInitLinear(
          movie_tex[movie_frame_idx],
          frame.pData,
          SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
          frame.details.video.width,
          frame.details.video.height, 0);

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
    } else {
      player_state = PLAYER_STOP;
    }
  }

  if (player_state == PLAYER_STOP) {
    // TODO: clear screen
    sceAvPlayerStop(movie_player);
    sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
    sceAvPlayerClose(movie_player);
    movie_audio_shutdown();
    player_state = PLAYER_INACTIVE;
  }
}

void movie_setup_player(void) {
  sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);

  glGenTextures(2, movie_frame);
  for (int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, movie_frame[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_W, SCREEN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
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
  movie_audio_init();

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

  audio_thid = sceKernelCreateThread("movie_audio_thread", movie_audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
  sceKernelStartThread(audio_thid, 0, NULL);

  player_state = PLAYER_ACTIVE;

  return 0;
}

void OS_MovieStop(void) {
  player_state = PLAYER_STOP;
}

int OS_MovieIsPlaying(int *loops) {
  return player_state == PLAYER_ACTIVE && sceAvPlayerIsActive(movie_player);
}

void OS_MovieSetSkippable(void) {
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
