/* fios.c -- use FIOS2 for optimized I/O
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "config.h"
#include "fios.h"
#include "so_util.h"

#define MAX_PATH_LENGTH 128
#define RAMCACHEBLOCKSIZE (32 * 1024)
#define RAMCACHEBLOCKNUM 1024

static int64_t g_OpStorage[SCE_FIOS_OP_STORAGE_SIZE(64, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_ChunkStorage[SCE_FIOS_CHUNK_STORAGE_SIZE(1024) / sizeof(int64_t) + 1];
static int64_t g_FHStorage[SCE_FIOS_FH_STORAGE_SIZE(32, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];
static int64_t g_DHStorage[SCE_FIOS_DH_STORAGE_SIZE(32, MAX_PATH_LENGTH) / sizeof(int64_t) + 1];

static SceFiosRamCacheContext g_MainRamCacheContext = SCE_FIOS_RAM_CACHE_CONTEXT_INITIALIZER;
static char *g_MainRamCacheWorkBuffer;

static SceFiosRamCacheContext g_PatchRamCacheContext = SCE_FIOS_RAM_CACHE_CONTEXT_INITIALIZER;
static char *g_PatchRamCacheWorkBuffer;

int fios_init(void) {
  int res;

  SceFiosParams params = SCE_FIOS_PARAMS_INITIALIZER;
  params.opStorage.pPtr = g_OpStorage;
  params.opStorage.length = sizeof(g_OpStorage);
  params.chunkStorage.pPtr = g_ChunkStorage;
  params.chunkStorage.length = sizeof(g_ChunkStorage);
  params.fhStorage.pPtr = g_FHStorage;
  params.fhStorage.length = sizeof(g_FHStorage);
  params.dhStorage.pPtr = g_DHStorage;
  params.dhStorage.length = sizeof(g_DHStorage);
  params.pathMax = MAX_PATH_LENGTH;

  params.threadAffinity[SCE_FIOS_IO_THREAD] = 0x40000;
  params.threadAffinity[SCE_FIOS_CALLBACK_THREAD] = 0;
  params.threadAffinity[SCE_FIOS_DECOMPRESSOR_THREAD] = 0;

  params.threadPriority[SCE_FIOS_IO_THREAD] = 64;
  params.threadPriority[SCE_FIOS_CALLBACK_THREAD] = 191;
  params.threadPriority[SCE_FIOS_DECOMPRESSOR_THREAD] = 191;

  res = sceFiosInitialize(&params);
  if (res < 0)
    return res;

  g_MainRamCacheWorkBuffer = memalign(8, RAMCACHEBLOCKNUM * RAMCACHEBLOCKSIZE);
  if (!g_MainRamCacheWorkBuffer)
    return -1;

  g_MainRamCacheContext.pPath = DATA_PATH "/Android/main.obb";
  g_MainRamCacheContext.pWorkBuffer = g_MainRamCacheWorkBuffer;
  g_MainRamCacheContext.workBufferSize = RAMCACHEBLOCKNUM * RAMCACHEBLOCKSIZE;
  g_MainRamCacheContext.blockSize = RAMCACHEBLOCKSIZE;
  res = sceFiosIOFilterAdd(0, sceFiosIOFilterCache, &g_MainRamCacheContext);
  if (res < 0)
    return res;

  g_PatchRamCacheWorkBuffer = memalign(8, RAMCACHEBLOCKNUM * RAMCACHEBLOCKSIZE);
  if (!g_PatchRamCacheWorkBuffer)
    return -1;

  g_PatchRamCacheContext.pPath = DATA_PATH "/Android/patch.obb";
  g_PatchRamCacheContext.pWorkBuffer = g_PatchRamCacheWorkBuffer;
  g_PatchRamCacheContext.workBufferSize = RAMCACHEBLOCKNUM * RAMCACHEBLOCKSIZE;
  g_PatchRamCacheContext.blockSize = RAMCACHEBLOCKSIZE;
  res = sceFiosIOFilterAdd(1, sceFiosIOFilterCache, &g_PatchRamCacheContext);
  if (res < 0)
    return res;

  return 0;
}

void fios_terminate(void) {
  sceFiosTerminate();
  free(g_PatchRamCacheWorkBuffer);
  free(g_MainRamCacheWorkBuffer);
}
