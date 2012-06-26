/*
 * RAW video demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "avformat.h"
#include "rawdec.h"

static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret= av_get_packet(s->pb, pkt, packet_size);
    pkt->pts=
    pkt->dts= pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}

#define OFFSET(x) offsetof(FFRawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption rawvideo_options[] = {
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size), AV_OPT_TYPE_STRING, {0, NULL}, 0, 0, DEC },
    { "pixel_format", "", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {0, "yuv420p"}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {0, "25"}, 0, 0, DEC },
    { NULL },
};

static const AVClass rawvideo_demuxer_class = {
    "rawvideo demuxer",
    av_default_item_name,
    rawvideo_options,
    LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_rawvideo_demuxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    AVFMT_GENERIC_INDEX,
    "yuv,cif,qcif,rgb",
    0, &rawvideo_demuxer_class,
    0, CODEC_ID_RAWVIDEO,
    sizeof(FFRawVideoDemuxerContext),
    0, ff_raw_read_header,
    rawvideo_read_packet,
};
