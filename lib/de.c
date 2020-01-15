#include "de.h"

#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>

#include <stdio.h>
#include <stdlib.h>

#define INBUF_SIZE 4096

static DeFrame *
de_frame_new (AVFrame *av_frame, uint8_t *data, int width, int height)
{
    DeFrame *frame;

    av_frame->width = width;
    av_frame->height = height;

    frame = malloc (1 * sizeof(DeFrame));
    frame->frame = av_frame;
    frame->width = width;
    frame->height = height;
    frame->data = malloc (width * height * sizeof (uint8_t));
    memcpy (frame->data, data, width * height);

    return frame;
}

static void
de_frame_free (DeFrame *frame)
{
    av_frame_free (&frame->frame);
    free (frame->data);
    free (frame);
}

static DeContext *
de_context_new (void)
{
    DeContext *context;

    context = malloc (1 * sizeof(DeContext));
    context->got_last = 0;
    context->frame_count = 0;

    return context;
}

void
de_context_free (DeContext *context)
{
    avcodec_free_context (&context->encoder_ctx);
    avformat_close_input (&context->fmt_ctx);
    free (context);
}

static int
find_video_stream (AVFormatContext *fmt_ctx) {
    int i;
    int stream_id;

    for(i = 0; i < fmt_ctx->nb_streams; i++) {
        if(fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream_id = i;
            break;
        }
    }

    return stream_id;
}

DeContext *
de_context_create (const char *infile)
{
    AVFormatContext *fmt_ctx = NULL;
    int stream_id = -1;
    AVCodec *decoder = NULL;
    AVDictionary *options = NULL;
    int fail = 0;
    DeContext *context;

    /* register all formats and codecs */
    av_register_all ();

    context = de_context_new ();

    /* Open input file, and allocate format context */
    if (avformat_open_input (&fmt_ctx, infile, NULL, NULL) < 0) {
        fprintf (stderr, "Could not open source file %s\n", infile);
        fail = 1;
        goto out;
    }
    printf("[LOG] Input file opened\n");

    /* Retrieve stream nformation */
    if (avformat_find_stream_info (fmt_ctx, NULL) < 0) {
        fprintf (stderr, "Could not find stream information\n");
        fail = 1;
        goto out;
    }
    printf("[LOG] Got stream info\n");

    av_dump_format (fmt_ctx, 0, infile, 0);

    /* Find video stream */
    context->stream_id = find_video_stream (fmt_ctx);
    if (context->stream_id == -1) {
        printf("Could not find stream\n");
        fail = 1;
        goto out;
    }
    printf("[LOG] Got video stream, id = %d\n", stream_id);

    context->decoder_ctx = fmt_ctx->streams[context->stream_id]->codec;

    /* Find decoder for stream */
    decoder = avcodec_find_decoder (context->decoder_ctx->codec_id);
    if (!decoder) {
        fprintf(stderr, "Failed to find codec\n");
        fail = 1;
        goto out;
    }
    printf("[LOG] Found codec\n");

    if (avcodec_open2(context->decoder_ctx, decoder, &options) < 0) {
        fprintf(stderr, "Failed to open codec\n");
        fail = 1;
        goto out;
    }
    printf("[LOG] Opened codec\n");

    context->sws_ctx = sws_getContext(context->decoder_ctx->width,
                                      context->decoder_ctx->height,
                                      context->decoder_ctx->pix_fmt,
                                      context->decoder_ctx->width,
                                      context->decoder_ctx->height,
                                      AV_PIX_FMT_RGB8,
                                      SWS_BICUBIC,
                                      NULL,
                                      NULL,
                                      NULL);

out:
    if (fail) {
        avformat_close_input(&fmt_ctx);
        return NULL;
    }

    context->fmt_ctx = fmt_ctx;

    return context;
}

static DeFrame *
de_context_decode_frame (DeContext *context,
                         AVFrame *yuv_frame,
                         AVFrame *rgb_frame,
                         AVPacket *pkt,
                         int *got_frame)
{
    int ret;
    DeFrame *frame;

    ret = avcodec_decode_video2 (context->decoder_ctx,
                                 yuv_frame,
                                 got_frame,
                                 pkt);
    if (ret < 0) {
        fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
        return NULL;
    }

    if (*got_frame) {
        printf("[LOG] Got frame %d\n", context->frame_count);
        /* Convert to RGB */
        sws_scale (context->sws_ctx,
                   (uint8_t const * const *)yuv_frame->data,
                   yuv_frame->linesize,
                   0,
                   context->decoder_ctx->height,
                   rgb_frame->data,
                   rgb_frame->linesize);

        frame = de_frame_new (rgb_frame, rgb_frame->data[0], yuv_frame->width, yuv_frame->height);

        printf("[LOG] Frame converted\n");

        return frame;
    }

    return NULL;
}

DeFrame *
de_context_get_next_frame (DeContext *context, int *got_frame) {
    AVPacket pkt;
    AVFrame *rgb_frame = NULL;
    AVFrame *yuv_frame;
    DeFrame *frame = NULL;
    int num_bytes;
    uint8_t *buffer;

    /* Init packet and let av_read_frame fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* Allocate frames */
    yuv_frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();

    // Determine required buffer size and allocate buffer
    num_bytes = avpicture_get_size (AV_PIX_FMT_RGB8, context->decoder_ctx->width, context->decoder_ctx->height);
    buffer = (uint8_t *)av_malloc (num_bytes * sizeof(uint8_t));

    avpicture_fill((AVPicture *)rgb_frame, buffer, AV_PIX_FMT_RGB8,
                   context->decoder_ctx->width, context->decoder_ctx->height);

    if (av_read_frame (context->fmt_ctx, &pkt) >= 0) {
        printf("[LOG] Got packet\n");
        if (pkt.stream_index == context->stream_id) {
            frame = de_context_decode_frame (context, yuv_frame, rgb_frame, &pkt, got_frame);;

            goto out;
        }
    } else if (!context->got_last) {
        pkt.data = NULL;
        pkt.size = 0;
        context->got_last = 1;

        frame = de_context_decode_frame (context, yuv_frame, rgb_frame, &pkt, got_frame);

        goto out;
    } else {
        *got_frame = -1;
    }

out:
    av_packet_unref (&pkt);
    av_frame_free (&yuv_frame);

    return frame;
}

void
de_context_prepare_encoding (DeContext *context, const char *outfile)
{
    AVCodec *encoder = NULL;
    printf("[LOG] Encode\n");

    /* find the video encoder */
    encoder = avcodec_find_encoder (context->decoder_ctx->codec_id);
    if (!encoder) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    context->encoder_ctx = avcodec_alloc_context3 (encoder);
    if (!context->encoder_ctx) {
        fprintf (stderr, "Could not allocate video encoder context\n");
        exit (1);
    }

       /* put sample parameters */
    context->encoder_ctx->bit_rate = context->decoder_ctx->bit_rate;
    /* resolution must be a multiple of two */
    context->encoder_ctx->width = context->decoder_ctx->width;
    context->encoder_ctx->height = context->decoder_ctx->height;
    /* frames per second */
    printf("FPS %d %d\n", context->decoder_ctx->time_base.num, context->decoder_ctx->time_base.den);
    context->encoder_ctx->time_base = (AVRational)context->decoder_ctx->time_base;
    context->encoder_ctx->gop_size = context->decoder_ctx->gop_size;
    context->encoder_ctx->max_b_frames = context->decoder_ctx->max_b_frames;
    context->encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (context->decoder_ctx->codec_id == AV_CODEC_ID_H264)
        av_opt_set(context->encoder_ctx->priv_data, "preset", "slow", 0);

    if (avcodec_open2(context->encoder_ctx, encoder, NULL) < 0) {
        fprintf (stderr, "Could not open encoder\n");
        exit (1);
    }

    context->outfile = fopen(outfile, "wb");
    if (!context->outfile) {
        fprintf (stderr, "Could not open %s\n", outfile);
        exit (1);
    }

    context->encoded_frame = av_frame_alloc();
    if (!context->encoded_frame) {
        fprintf(stderr, "Could not allocate video outFrame\n");
        exit(1);
    }

    context->encoded_frame->width = context->encoder_ctx->width;
    context->encoded_frame->height = context->encoder_ctx->height;
    context->encoded_frame->format = context->encoder_ctx->pix_fmt;

    context->encoder_sws_ctx = sws_getContext(context->encoded_frame->width,
                                              context->encoded_frame->height,
                                              AV_PIX_FMT_RGB8,
                                              context->encoded_frame->width,
                                              context->encoded_frame->height,
                                              AV_PIX_FMT_YUV420P,
                                              SWS_BICUBIC,
                                              0, 0, 0);

    av_image_alloc(context->encoded_frame->data,
                   context->encoded_frame->linesize,
                   context->encoder_ctx->width,
                   context->encoder_ctx->height,
                   context->encoder_ctx->pix_fmt,
                   32);
}

void
de_context_set_next_frame (DeContext *context, DeFrame *frame)
{
    AVPacket pkt;
    int got_output;
    int linesize[1] = { frame->width };
    uint8_t *data[1] = { frame->frame->data[0] };

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    sws_scale(context->encoder_sws_ctx,
              (const uint8_t * const *)data,
              linesize,
              0,
              frame->height,
              context->encoded_frame->data,
              context->encoded_frame->linesize);
    context->encoded_frame->pts = context->frame_count++;

    if (avcodec_encode_video2(context->encoder_ctx, &pkt, context->encoded_frame, &got_output) < 0) {
        fprintf (stderr, "Error encoding frame\n");
        exit (1);
    }

    if (got_output) {
        fwrite (pkt.data, 1, pkt.size, context->outfile);
        av_packet_unref (&pkt);
    }

    de_frame_free (frame);
}

void
de_context_end_encoding (DeContext *context)
{
    AVPacket pkt;
    int got_output;
    int ret;
    int i;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* Write delayed frames */
    for (got_output = 1; got_output; i++) {
        fflush(stdout);

        ret = avcodec_encode_video2 (context->encoder_ctx, &pkt, NULL, &got_output);
        if (ret < 0) {
            fprintf (stderr, "Error encoding frame\n");
            exit (1);
        }

        if (got_output) {
            fwrite (pkt.data, 1, pkt.size, context->outfile);
            av_packet_unref (&pkt);
        }
    }

    fwrite(endcode, 1, sizeof(endcode), context->outfile);
    fclose (context->outfile);
    de_context_free (context);
}
