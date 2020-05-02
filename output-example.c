/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * video encoding with libavcodec API example
 *
 * @example output-example.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include "libavformat/avformat.h"
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

// Global variables
AVOutputFormat *output_format;
AVFormatContext *format_context;
const AVCodec *codec;
enum AVCodecID video_codec_id;
AVCodecContext *codec_context;
AVStream *video_st;
AVFrame *video_frame;
int frame_count;
int width=720, height=480;
int fps=25;

// List of function headers
int create_video_file(const char *filename, const char *codec_name);
AVStream * add_video_stream(const char *filename);
int open_video_stream();
int create_video_frame();
int write_video_frame(AVPacket *pkt);
int str_end_with(const char *, const char*);
void end_video_file();

// Main procedure
int main(int argc, char **argv)
{
    // Compilation notes
    // export LD_LIBRARY_PATH=/usr/local/ffmpeg/lib:/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
    // gcc output-example.c -o output-example -L/usr/local/ffmpeg/lib -lavformat -lavcodec -lavutil
    // ./output-example test.mp4 libx264

    const char *filename, *codec_name;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    if (create_video_file(filename, codec_name)) {
        /* encode 1 second of video */
        for (int i = 0; i < fps; i++) {
            if (!create_video_frame()) {
                fprintf(stderr, "Error while writing frame at index %i\n", i);
                exit(0);
            }
        }
        end_video_file();
    }

    return 0;
}

// Functions implementation

int create_video_file(const char *filename, const char *codec_name)
{
    printf("create_video_file() - Tracing...\n");
    int ret;

    // AVOutputFormat
    output_format = av_guess_format(codec_name, filename, NULL);
    if (!output_format) {
        fprintf(stderr, "create_video_file() - Can't support format requested.\n");
        return 0;
    }

    if (!output_format) {
        fprintf(stderr, "create_video_file() - Output format variable is NULL.\n");
        return 0;
    }

    // AVFormatContext
    format_context = avformat_alloc_context();
    if (!format_context) {
        fprintf(stderr, "create_video_file() - Memory error while allocating format context.\n");
        return 0;
    }

    format_context->oformat = output_format;

    // AVStream
    video_codec_id = output_format->video_codec;
    printf("create_video_file() - codec id -> %i\n", video_codec_id);
    video_st = add_video_stream(filename);

    av_dump_format(format_context, 0, filename, 1);

    if (video_st) {
        int success = open_video_stream();
        if (!success) {
            fprintf(stderr, "create_video_file() - Could not initialize video codec.\n");
            return 0;
        }
    } else {
        fprintf(stderr, "create_video_file() - Video stream variable is NULL.\n");
        return 0;
    }

    if (!(output_format->flags & AVFMT_NOFILE)) {
        ret = avio_open(&format_context->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "create_video_file() - Could not open video file.\n");
            return 0;
        }
    }

    if (avformat_write_header(format_context, NULL) < 0) {
        fprintf(stderr, "create_video_file() - could not write video file header.\n");
        return 0;
    }

    if (video_frame) {
        video_frame->pts = 0;
    }

    frame_count = 0;

    return 1;
}

AVStream * add_video_stream(const char *filename)
{
    printf("add_video_stream() - Tracing...\n");
    AVStream *st;

    // Find the video encoder
    codec = avcodec_find_encoder(video_codec_id);
    if (!codec) {
        fprintf(stderr, "add_video_stream() - Could not find video encoder.\n");
        fprintf(stderr, "add_video_stream() - Unavailable Codec ID -> %i.\n", video_codec_id);
        return 0;
    }

    st = avformat_new_stream(format_context, codec);
    if (!st) {
        fprintf(stderr, "add_video_stream() - Could not video alloc stream.\n");
        return 0;
    }

    /*
    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        fprintf(stderr, "add_video_stream() - Could not allocate video codec context.\n");
        return 0;
    }
    if (avcodec_parameters_to_context(codec_context, st->codecpar) < 0) {
        fprintf(stderr, "add_video_stream() - Could not copy parameters to context.\n");
        return 0;
    }
    */

    codec_context = st->codec;

    // Put sample parameters
    codec_context->bit_rate = 6000000;
    if (fps == 1) {
        codec_context->bit_rate = 4000000;
    }

    // Resolution must be a multiple of two
    codec_context->width = width;
    codec_context->height = height;

    codec_context->gop_size = 0;
    codec_context->max_b_frames = 0;

    codec_context->time_base = (AVRational){1, fps};

    if (str_end_with(filename, "gif")) {
        st->time_base.num = 1;
        st->time_base.den = fps;
        codec_context->pix_fmt = AV_PIX_FMT_RGB24;
    } else {
        codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    if (format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    return st;
}

int open_video_stream()
{
    printf("open_video_stream() - Tracing...\n");

    // Open the codec
    int ret = avcodec_open2(codec_context, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "open_video_stream() - Could not open input codec (error '%s').\n", av_err2str(ret));
        return 0;
    }

    // Allocate and init a re-usable frame
    // AVFrame
    video_frame = av_frame_alloc();

    if (!video_frame) {
        fprintf(stderr, "open_video_stream() - There is no available memory to export your project as a video.\n");
        return 0;
    }

    return 1;
}

int create_video_frame()
{
    printf("create_video_frame() - Tracing...\n");
    fflush(stdout);

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL; // packet data will be allocated by the encoder
    pkt.size = 0;

    int ret = av_frame_make_writable(video_frame);
    if (ret < 0) {
        fprintf(stderr, "create_video_frame() - Frame is not writable.\n");
        exit(1);
    }

    /* prepare a dummy image */
    /* Y */
    for (int y = 0; y < codec_context->height; y++) {
        for (int x = 0; x < codec_context->width; x++) {
             video_frame->data[0][y * video_frame->linesize[0] + x] = x + y + frame_count * 3;
        }
    }

    /* Cb and Cr */
    for (int y = 0; y < codec_context->height/2; y++) {
         for (int x = 0; x < codec_context->width/2; x++) {
              video_frame->data[1][y * video_frame->linesize[1] + x] = 128 + y + frame_count * 2;
              video_frame->data[2][y * video_frame->linesize[2] + x] = 64 + x + frame_count * 5;
         }
    }

    video_frame->pts = frame_count;

    ret = avcodec_send_frame(codec_context, video_frame);
    if (ret < 0) {
        fprintf(stderr, "create_video_frame() - Error while sending a frame for encoding.\n");
        return 0;
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_context, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return (ret == AVERROR(EAGAIN)) ? 0:1;
        } else if (ret < 0) {
            fprintf(stderr, "create_video_frame() - Error during encoding.\n");
            return 0;
        }
        ret = write_video_frame(&pkt);
        if (ret < 0) {
           fprintf(stderr, "create_video_frame() - Error while writing video frame.\n");
           return 0;
        }
        av_packet_unref(&pkt);
    }

    frame_count++;

    return 1;
}


int write_video_frame(AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, video_st->time_base, video_st->time_base);
    pkt->stream_index = video_st->index;

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(format_context, pkt);
}

int str_end_with(const char *s, const char *t)
{
    const char *init = t; /* Hold the initial position of *t */

    while (*s) {
        while (*s == *t) {
            if (!(*s)) {
                return 1;
            }
            s++;
            t++;
        }
        s++;
        t = init;
    }
    return 0;
}

void end_video_file()
{
    printf("end_video_frame() - Tracing...\n");
    av_write_trailer(format_context);

    if (codec_context) {
        avcodec_close(codec_context);
    }

    av_frame_free(&video_frame);

    if (!(output_format->flags & AVFMT_NOFILE)) {
        avio_close(format_context->pb);
    }

    avformat_free_context(format_context);
}
