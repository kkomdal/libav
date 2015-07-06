/*
 * Resolume DXV decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "texturedsp.h"
#include "thread.h"

typedef struct DXVContext {
    TextureDSPContext texdsp;
    GetByteContext gbc;

    int compressed;
    int paletted;

    uint8_t *tex_data;  // Compressed texture
    int tex_rat;        // Compression ratio
    int tex_step;       // Distance between blocks
    int64_t tex_size;   // Texture size

    /* Pointer to the selected decompression function */
    int (*tex_funct)(uint8_t *dst, ptrdiff_t stride, const uint8_t *block);
} DXVContext;


#define LZF_LITERAL_MAX (1 << 5)
#define LZF_LONG_BACKREF 7

static int ff_lzf_uncompress(GetByteContext *gb, uint8_t **buf, int64_t *size)
{
    int ret     = 0;
    uint8_t *p  = *buf;
    int64_t len = 0;

    while (bytestream2_get_bytes_left(gb) > 2) {
        uint8_t s = bytestream2_get_byte(gb);

        if (s < LZF_LITERAL_MAX) {
            if (s > *size - len) {
                *size += *size / 2;
                ret = av_reallocp(buf, *size);
                if (ret < 0)
                    return ret;
            }

            bytestream2_get_buffer(gb, p, s);
            p   += s;
            len += s;
        } else {
            int l   = 2 + (s >> 5);
            int off = ((s & 0x1f) << 8) + 1;

            if (l == LZF_LONG_BACKREF)
                l += bytestream2_get_byte(gb);

            off += bytestream2_get_byte(gb);

            if (off > p - *buf)
                return AVERROR_INVALIDDATA;

            if (l > *size - len) {
                *size += *size / 2;
                ret = av_reallocp(buf, *size);
                if (ret < 0)
                    return ret;
            }

            av_memcpy_backptr(p, off, l);

            p   += l;
            len += l;
        }
    }

    *size = len;

    return 0;
}

static int decompress_texture_thread(AVCodecContext *avctx, void *arg,
                                     int block_nb, int thread_nb)
{
    DXVContext *ctx = avctx->priv_data;
    AVFrame *frame = arg;
    int x = (TEXTURE_BLOCK_W * block_nb) % (avctx->coded_width);
    int y = TEXTURE_BLOCK_H * (TEXTURE_BLOCK_W * block_nb / (avctx->coded_width));
    uint8_t *p = frame->data[0] + x * 4 + y * frame->linesize[0];
    const uint8_t *d = ctx->tex_data + block_nb * ctx->tex_step;

    ctx->tex_funct(p, frame->linesize[0], d);
    return 0;
}

static void decompress_texture(AVCodecContext *avctx, AVFrame *frame)
{
    DXVContext *ctx = avctx->priv_data;
    int x, y;
    uint8_t *src = ctx->tex_data;

    for (y = 0; y < avctx->coded_height; y += 4) {
        for (x = 0; x < avctx->coded_width; x += 4) {
            uint8_t *p = frame->data[0] + x * 4 + y * frame->linesize[0];
            int step = ctx->tex_funct(p, frame->linesize[0], src);
            src += step;
        }
    }
}

/* This scheme addresses already decoded elements depending on 2-bit status:
 *   0 -> copy new element
 *   1 -> copy one element from position -x
 *   2 -> copy one element from position -(get_byte() + 2) * x
 *   3 -> copy one element from position -(get_16le() + 0x102) * x
 * x is 2 for dxt1 and 4 for dxt5. */
#define CHECKPOINT(x)                                                         \
    do {                                                                      \
        if (state == 0) {                                                     \
            value = bytestream2_get_le32(gbc);                                \
            state = 16;                                                       \
        }                                                                     \
        op = value & 0x3;                                                     \
        value >>= 2;                                                          \
        state--;                                                              \
        switch (op) {                                                         \
        case 1: /* copy one element from position -x */                       \
            idx = x;                                                          \
            break;                                                            \
        case 2: /* copy one element from position -(get_byte() + 2) * x */    \
            idx = (bytestream2_get_byte(gbc) + 2) * x;                        \
            break;                                                            \
        case 3: /* copy one element from position -(get_16le() + 0x102) * x */\
            idx = (bytestream2_get_le16(gbc) + 0x102) * x;                    \
            break;                                                            \
        }                                                                     \
    } while(0)

static int dxv_decompress_dxt1(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, op;
    int idx, prev, state = 0;
    int pos = 2;

    /* Copy the first two elements */
    AV_WL32(ctx->tex_data, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 4, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos < ctx->tex_size / 4) {
        CHECKPOINT(2);

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

static int dxv_decompress_dxt5(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, op;
    int idx, prev, state = 0;
    int pos = 4;
    int run = 0;
    int probe, check;

    /* Copy the first four elements */
    AV_WL32(ctx->tex_data +  0, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  4, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  8, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 12, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos < ctx->tex_size / 4) {
        if (run) {
            run--;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            if (state == 0) {
                value = bytestream2_get_le32(gbc);
                state = 16;
            }
            op = value & 0x3;
            value >>= 2;
            state--;

            switch (op) {
            case 0:
                /* Long copy */
                check = bytestream2_get_byte(gbc) + 1;
                if (check == 256) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        check += probe;
                    } while (probe == 0xFFFF);
                }
                while (check && pos < ctx->tex_size / 4) {
                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    check--;
                }

                /* Restart (or exit) the loop */
                continue;
                break;
            case 1:
                /* Load new run value */
                run = bytestream2_get_byte(gbc);
                if (run == 255) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        run += probe;
                    } while (probe == 0xFFFF);
                }

                /* Copy two dwords from previous data */
                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 2:
                /* Copy two dwords from a previous index */
                idx = 8 + bytestream2_get_le16(gbc);
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 3:
                /* Copy two dwords from input */
                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            }
        }

        CHECKPOINT(4);

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(4);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(4);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

static int dxv_decompress_lzf(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;

    int ret = ff_lzf_uncompress(gbc, &ctx->tex_data, &ctx->tex_size);
    if (ret == 0) {
        av_log(avctx, AV_LOG_ERROR, "lzf error %s\n.", strerror(errno));
        return -1;
    }

    return 0;
}

static int dxv_decode(AVCodecContext *avctx, void *data,
                      int *got_frame, AVPacket *avpkt)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    AVFrame *frame = data;
    int (*decompress_tex)(AVCodecContext *avctx);
    uint32_t tag;
    int blocks, channels;
    int size = 0, old_type = 0;
    int ret;

    bytestream2_init(gbc, avpkt->data, avpkt->size);

    tag = bytestream2_get_le32(gbc);
    switch (tag) {
    case MKBETAG('D', 'X', 'T', '1'):
        decompress_tex = dxv_decompress_dxt1;
        ctx->tex_funct = ctx->texdsp.dxt1_block;
        ctx->tex_rat   = 8;
        ctx->tex_step  = 8;
        av_log(avctx, AV_LOG_DEBUG, "DXTR1 compression and DXT1 texture ");
        break;
    case MKBETAG('D', 'X', 'T', '5'):
        decompress_tex = dxv_decompress_dxt5;
        ctx->tex_funct = ctx->texdsp.dxt5_block;
        ctx->tex_rat   = 4;
        ctx->tex_step  = 16;
        av_log(avctx, AV_LOG_DEBUG, "DXTR5 compression and DXT5 texture ");
        break;
    case MKBETAG('Y', 'C', 'G', '6'):
    case MKBETAG('Y', 'G', '1', '0'):
        avpriv_report_missing_feature(avctx, "Tag 0x%08X", tag);
        return AVERROR_PATCHWELCOME;
    default:
        size = tag & 0x00FFFFFF;
        old_type = tag >> 24;
        channels = old_type & 0x0F;
        if (old_type & 0x40) {
            av_log(avctx, AV_LOG_DEBUG, "LZF compression and DXT5 texture ");
            ctx->tex_funct = ctx->texdsp.dxt5_block;
            ctx->tex_step  = 16;
        } else if (old_type & 0x20) {
            av_log(avctx, AV_LOG_DEBUG, "LZF compression and DXT1 texture ");
            ctx->tex_funct = ctx->texdsp.dxt1_block;
            ctx->tex_step  = 8;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported header (0x%08X)\n.", tag);
            return AVERROR_INVALIDDATA;
        }
        decompress_tex = dxv_decompress_lzf;
        ctx->tex_rat = 1;
        break;
    }

    /* Old header is 4 bytes, newer header is 12 bytes. */
    if (!old_type) {
        channels = bytestream2_get_byte(gbc);
        bytestream2_skip(gbc, 3); // unknown
        size = bytestream2_get_le32(gbc);
    }
    av_log(avctx, AV_LOG_DEBUG, "(%d channels)\n", channels);

    if (size != bytestream2_get_bytes_left(gbc)) {
        av_log(avctx, AV_LOG_ERROR, "Incomplete or invalid file (%u > %u)\n.",
               size, bytestream2_get_bytes_left(gbc));
        return AVERROR_INVALIDDATA;
    }

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    ctx->tex_size = avctx->coded_width * avctx->coded_height * 4 / ctx->tex_rat;
    ret = av_reallocp(&ctx->tex_data, ctx->tex_size);
    if (ret < 0)
        return ret;

    ret = decompress_tex(avctx);
    if (ret < 0)
        return ret;

#if 0
    /* Use the decompress function on the texture, one block per thread. */
    blocks = avctx->coded_width * avctx->coded_height / (TEXTURE_BLOCK_W * TEXTURE_BLOCK_H);
    avctx->execute2(avctx, decompress_texture_thread, frame, NULL, blocks);
#else
    decompress_texture(avctx, frame);
#endif

    /* Frame is ready to be output. */
    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static int dxv_init(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Codec requires 16x16 alignment. */
    avctx->coded_width  = FFALIGN(avctx->width,  16);
    avctx->coded_height = FFALIGN(avctx->height, 16);

    ff_texturedsp_init(&ctx->texdsp);
    avctx->pix_fmt = AV_PIX_FMT_RGBA;

    return 0;
}

static int dxv_close(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;

    av_freep(&ctx->tex_data);

    return 0;
}

AVCodec ff_dxv_decoder = {
    .name           = "dxv",
    .long_name      = NULL_IF_CONFIG_SMALL("Resolume DXV"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_DXV,
    .init           = dxv_init,
    .decode         = dxv_decode,
    .close          = dxv_close,
    .priv_data_size = sizeof(DXVContext),
    .capabilities   = CODEC_CAP_DR1,// | CODEC_CAP_SLICE_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP
};
