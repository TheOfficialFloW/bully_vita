/* openal_patch.c -- stb_vorbis redirection
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "main.h"
#include "so_util.h"

void patch_stb_vorbis(void) {
  hook_thumb(so_find_addr("stb_vorbis_close"), (uintptr_t)stb_vorbis_close);
  hook_thumb(so_find_addr("stb_vorbis_decode_filename"), (uintptr_t)stb_vorbis_decode_filename);
  hook_thumb(so_find_addr("stb_vorbis_decode_frame_pushdata"), (uintptr_t)stb_vorbis_decode_frame_pushdata);
  hook_thumb(so_find_addr("stb_vorbis_decode_memory"), (uintptr_t)stb_vorbis_decode_memory);
  hook_thumb(so_find_addr("stb_vorbis_flush_pushdata"), (uintptr_t)stb_vorbis_flush_pushdata);
  hook_thumb(so_find_addr("stb_vorbis_get_error"), (uintptr_t)stb_vorbis_get_error);
  hook_thumb(so_find_addr("stb_vorbis_get_file_offset"), (uintptr_t)stb_vorbis_get_file_offset);
  hook_thumb(so_find_addr("stb_vorbis_get_frame_float"), (uintptr_t)stb_vorbis_get_frame_float);
  hook_thumb(so_find_addr("stb_vorbis_get_frame_short"), (uintptr_t)stb_vorbis_get_frame_short);
  hook_thumb(so_find_addr("stb_vorbis_get_frame_short_interleaved"), (uintptr_t)stb_vorbis_get_frame_short_interleaved);
  hook_thumb(so_find_addr("stb_vorbis_get_info"), (uintptr_t)stb_vorbis_get_info);
  hook_thumb(so_find_addr("stb_vorbis_get_sample_offset"), (uintptr_t)stb_vorbis_get_sample_offset);
  hook_thumb(so_find_addr("stb_vorbis_get_samples_float"), (uintptr_t)stb_vorbis_get_samples_float);
  hook_thumb(so_find_addr("stb_vorbis_get_samples_float_interleaved"), (uintptr_t)stb_vorbis_get_samples_float_interleaved);
  hook_thumb(so_find_addr("stb_vorbis_get_samples_short"), (uintptr_t)stb_vorbis_get_samples_short);
  hook_thumb(so_find_addr("stb_vorbis_get_samples_short_interleaved"), (uintptr_t)stb_vorbis_get_samples_short_interleaved);
  hook_thumb(so_find_addr("stb_vorbis_open_file"), (uintptr_t)stb_vorbis_open_file);
  hook_thumb(so_find_addr("stb_vorbis_open_file_section"), (uintptr_t)stb_vorbis_open_file_section);
  hook_thumb(so_find_addr("stb_vorbis_open_filename"), (uintptr_t)stb_vorbis_open_filename);
  hook_thumb(so_find_addr("stb_vorbis_open_memory"), (uintptr_t)stb_vorbis_open_memory);
  hook_thumb(so_find_addr("stb_vorbis_open_pushdata"), (uintptr_t)stb_vorbis_open_pushdata);
  hook_thumb(so_find_addr("stb_vorbis_seek"), (uintptr_t)stb_vorbis_seek);
  hook_thumb(so_find_addr("stb_vorbis_seek_frame"), (uintptr_t)stb_vorbis_seek_frame);
  hook_thumb(so_find_addr("stb_vorbis_seek_start"), (uintptr_t)stb_vorbis_seek_start);
  hook_thumb(so_find_addr("stb_vorbis_stream_length_in_samples"), (uintptr_t)stb_vorbis_stream_length_in_samples);
  hook_thumb(so_find_addr("stb_vorbis_stream_length_in_seconds"), (uintptr_t)stb_vorbis_stream_length_in_seconds);
}
