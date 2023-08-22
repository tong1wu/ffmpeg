/*
 * Direct3D 12 HW acceleration video encoder
 *
 * copyright (c) 2023 Tong Wu
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_D3D12VA_ENCODE_H
#define AVCODEC_D3D12VA_ENCODE_H

#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "avcodec.h"
#include "internal.h"
#include "hwconfig.h"
#include "hw_base_encode.h"

struct D3D12VAEncodeType;

extern const AVCodecHWConfigInternal *const ff_d3d12va_encode_hw_configs[];

#define MAX_PARAM_BUFFER_SIZE 4096
#define D3D12VA_VIDEO_ENC_ASYNC_DEPTH 8

enum
{
   ENC_FEATURE_NOT_SUPPORTED = 0,
   ENC_FEATURE_SUPPORTED = 1,
   ENC_FEATURE_REQUIRED = 2,
};

typedef struct D3D12VAEncodePicture {
    HWBaseEncodePicture base;

    int             header_size;

    AVD3D12VAFrame *input_surface;
    AVD3D12VAFrame *recon_surface;

    AVBufferRef    *output_buffer_ref;
    ID3D12Resource *output_buffer;

    AVBufferRef    *encoded_metadata_ref;
    ID3D12Resource *encoded_metadata;

    AVBufferRef    *resolved_metadata_ref;
    ID3D12Resource *resolved_metadata;

    D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA pic_ctl;

    int             fence_value;
} D3D12VAEncodePicture;

typedef struct D3D12VAEncodeProfile {
    //lavc profile value (FF_PROFILE_*).
    int       av_profile;
    //Supported bit depth.
    int       depth;
    //Number of components.
    int       nb_components;
    //Chroma subsampling in width dimension.
    int       log2_chroma_w;
    //Chroma subsampling in height dimension.
    int       log2_chroma_h;
    //D3D12 profile value.
    D3D12_VIDEO_ENCODER_PROFILE_DESC d3d12_profile;
} D3D12VAEncodeProfile;

typedef struct D3D12VAEncodeRCMode {
    // Mode from above enum (RC_MODE_*).
    int mode;
    // Name.
    const char *name;
    // D3D12 mode value.
    D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE d3d12_mode;
    // Uses bitrate parameters.
    int bitrate;
    // Supports maxrate distinct from bitrate.
    int maxrate;
    // Uses quality value.
    int quality;
    // Supports HRD/VBV parameters.
    int hrd;
} D3D12VAEncodeRCMode;

typedef struct D3D12VAEncodeContext {
    HWBaseEncodeContext base;

    //Codec-specific hooks.
    const struct D3D12VAEncodeType *codec;

    // bi not empty
    int bi_not_empty;

    //Chosen encoding profile details.
    const D3D12VAEncodeProfile *profile;

    //Chosen rate control mode details.
    const D3D12VAEncodeRCMode *rc_mode;

    AVD3D12VADeviceContext *hwctx;

    //device3 interface
    ID3D12Device3 *device3;

    ID3D12VideoDevice3 *video_device3;

    // Pool of (reusable) bitstream output buffers.
    AVBufferPool   *output_buffer_pool;

    // Pool of (reusable) encoded metadata buffers.
    AVBufferPool   *encoded_metadata_pool;

    // Pool of (reusable) resolved metadata buffers.
    AVBufferPool   *resolved_metadata_pool;

    //D3D12 video encoder
    AVBufferRef *encoder_ref;

    ID3D12VideoEncoder *encoder;

    //D3D12 video encoder heap
    ID3D12VideoEncoderHeap *encoder_heap;

    //A cached queue for reusing the D3D12 command allocators
    //@see https://learn.microsoft.com/en-us/windows/win32/direct3d12/recording-command-lists-and-bundles#id3d12commandallocator
    AVFifo *allocator_queue;

    //D3D12 command queue
    ID3D12CommandQueue *command_queue;

    //D3D12 video encode command list
    ID3D12VideoEncodeCommandList2 *command_list;

    //The sync context used to sync command queue
    AVD3D12VASyncContext sync_ctx;

    // The encoder does not support cropping information, so warn about
    // it the first time we encounter any nonzero crop fields.
    int             crop_warned;

    //D3D12 hardware structures
    D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC resolution;

    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codec_conf;

    D3D12_VIDEO_ENCODER_RATE_CONTROL rc;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS req;

    D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE gop;

    D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS res_limits;

    D3D12_VIDEO_ENCODER_LEVEL_SETTING level;
} D3D12VAEncodeContext;

typedef struct D3D12VAEncodeType {
    //List of supported profiles.
   const D3D12VAEncodeProfile *profiles;

    //Codec feature flags.
    int flags;

    // Default quality for this codec - used as quantiser or RC quality
    // factor depending on RC mode.
    int default_quality;

    // Query codec configuration and determine encode parameters like
    // block sizes for surface alignment and slices. If not set, assume
    // that all blocks are 16x16 and that surfaces should be aligned to match
    // this.
    int (*get_encoder_caps)(AVCodecContext *avctx);

    // Perform any extra codec-specific configuration.
    int (*configure)(AVCodecContext *avctx);

    // Set codec-specific level setting.
    int (*set_level)(AVCodecContext *avctx);

    // The size of any private data structure associated with each
    // picture (can be zero if not required).
    size_t picture_priv_data_size;

    // Fill the corresponding parameters.
    int (*init_sequence_params)(AVCodecContext *avctx);

    int (*init_picture_params)(AVCodecContext *avctx,
                               D3D12VAEncodePicture *pic);

    void (*free_picture_params)(D3D12VAEncodePicture *pic);

    // Write the packed header data to the provided buffer.
    int (*write_sequence_header)(AVCodecContext *avctx,
                                 char *data, size_t *data_len);

    // D3D12 codec name.
    D3D12_VIDEO_ENCODER_CODEC d3d12_codec;
} D3D12VAEncodeType;

int ff_d3d12va_encode_init(AVCodecContext *avctx);
int ff_d3d12va_encode_close(AVCodecContext *avctx);

#endif /* AVCODEC_D3D12VA_ENCODE_H */