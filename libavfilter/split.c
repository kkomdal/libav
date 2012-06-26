/*
 * Copyright (c) 2007 Bobby Bingham
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
 * audio and video splitter
 */

#include "avfilter.h"
#include "audio.h"
#include "internal.h"
#include "video.h"

static int split_init(AVFilterContext *ctx, const char *args)
{
    int i, nb_outputs = 2;

    if (args) {
        nb_outputs = strtol(args, NULL, 0);
        if (nb_outputs <= 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid number of outputs specified: %d.\n",
                   nb_outputs);
            return AVERROR(EINVAL);
        }
    }

    for (i = 0; i < nb_outputs; i++) {
        char name[32];
        AVFilterPad pad = { 0 };

        snprintf(name, sizeof(name), "output%d", i);
        pad.type = ctx->filter->inputs[0].type;
        pad.name = av_strdup(name);

        ff_insert_outpad(ctx, i, &pad);
    }

    return 0;
}

static void split_uninit(AVFilterContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = inlink->dst;
    int i;

    for (i = 0; i < ctx->nb_outputs; i++)
        ff_start_frame(ctx->outputs[i],
                       avfilter_ref_buffer(picref, ~AV_PERM_WRITE));
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext *ctx = inlink->dst;
    int i;

    for (i = 0; i < ctx->nb_outputs; i++)
        ff_draw_slice(ctx->outputs[i], y, h, slice_dir);
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    int i;

    for (i = 0; i < ctx->nb_outputs; i++)
        ff_end_frame(ctx->outputs[i]);

    avfilter_unref_buffer(inlink->cur_buf);
}

static AVFilterPad tmp__0[] = {{ "default",
                                    AVMEDIA_TYPE_VIDEO,
                                    0, 0, start_frame,
                                    ff_null_get_video_buffer,
                                    0, end_frame,
                                    draw_slice, },
                                  { NULL}};
static AVFilterPad tmp__1[] = {{ NULL}};
AVFilter avfilter_vf_split = {
    "split",
    NULL_IF_CONFIG_SMALL("Pass on the input to two outputs."),

    tmp__0,
    tmp__1,

    split_init,
    split_uninit,
};

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *samplesref)
{
    AVFilterContext *ctx = inlink->dst;
    int i, ret = 0;

    for (i = 0; i < ctx->nb_outputs; i++) {
        ret = ff_filter_samples(inlink->dst->outputs[i],
                                avfilter_ref_buffer(samplesref, ~AV_PERM_WRITE));
        if (ret < 0)
            break;
    }
    avfilter_unref_buffer(samplesref);
    return ret;
}

static const AVFilterPad tmp__2[] = {{ "default",
                                        AVMEDIA_TYPE_AUDIO,
                                        0, 0, 0, 0, ff_null_get_audio_buffer,
                                        0, 0, filter_samples },
                                      { NULL }};
static const AVFilterPad tmp__3[] = {{ NULL }};
AVFilter avfilter_af_asplit = {
    "asplit",
    NULL_IF_CONFIG_SMALL("Pass on the audio input to N audio outputs."),

    tmp__2,
    tmp__3,

    split_init,
    split_uninit,
};
