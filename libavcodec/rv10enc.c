/*
 * RV10 encoder
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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

/**
 * @file
 * RV10 encoder
 */

#include "mpegvideo.h"
#include "put_bits.h"

void ff_rv10_encode_picture_header(MpegEncContext *s, int picture_number)
{
    int full_frame= 0;

    avpriv_align_put_bits(&s->pb);

    put_bits(&s->pb, 1, 1);     /* marker */

    put_bits(&s->pb, 1, (s->pict_type == AV_PICTURE_TYPE_P));

    put_bits(&s->pb, 1, 0);     /* not PB frame */

    put_bits(&s->pb, 5, s->qscale);

    if (s->pict_type == AV_PICTURE_TYPE_I) {
        /* specific MPEG like DC coding not used */
    }
    /* if multiple packets per frame are sent, the position at which
       to display the macroblocks is coded here */
    if(!full_frame){
        put_bits(&s->pb, 6, 0); /* mb_x */
        put_bits(&s->pb, 6, 0); /* mb_y */
        put_bits(&s->pb, 12, s->mb_width * s->mb_height);
    }

    put_bits(&s->pb, 3, 0);     /* ignored */
}

FF_MPV_GENERIC_CLASS(rv10)

static const enum PixelFormat tmp__0[] = { PIX_FMT_YUV420P, PIX_FMT_NONE };
AVCodec ff_rv10_encoder = {
    "rv10",
    NULL_IF_CONFIG_SMALL("RealVideo 1.0"),
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_RV10,
    0, 0, tmp__0,
    0, 0, 0, 0, &rv10_class,
    0, sizeof(MpegEncContext),
    0, 0, 0, 0, 0, ff_MPV_encode_init,
    0, ff_MPV_encode_picture,
    0, ff_MPV_encode_end,
};
