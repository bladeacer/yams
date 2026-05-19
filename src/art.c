#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include "metadata.h"
#include "art.h"
#include "utils.h"
#include "img2ascii.h"

static int extract_frame(const char *url, uint8_t **out_pixels, int *out_w, int *out_h) {
    AVFormatContext *fmt_ctx = NULL;
    int ret;

    ret = avformat_open_input(&fmt_ctx, url, NULL, NULL);
    if (ret < 0) return -1;

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) { avformat_close_input(&fmt_ctx); return -1; }

    int stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            stream_idx = i;
            break;
        }
    }
    if (stream_idx < 0)
        stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (stream_idx < 0) { avformat_close_input(&fmt_ctx); return -1; }

    AVStream *stream = fmt_ctx->streams[stream_idx];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { avformat_close_input(&fmt_ctx); return -1; }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) { avformat_close_input(&fmt_ctx); return -1; }

    ret = avcodec_parameters_to_context(dec_ctx, stream->codecpar);
    if (ret < 0) { avcodec_free_context(&dec_ctx); avformat_close_input(&fmt_ctx); return -1; }

    ret = avcodec_open2(dec_ctx, decoder, NULL);
    if (ret < 0) { avcodec_free_context(&dec_ctx); avformat_close_input(&fmt_ctx); return -1; }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        ret = avcodec_send_packet(dec_ctx, &stream->attached_pic);
    } else {
        int got_packet = 0;
        while (av_read_frame(fmt_ctx, pkt) >= 0) {
            if (pkt->stream_index == stream_idx) {
                ret = avcodec_send_packet(dec_ctx, pkt);
                av_packet_unref(pkt);
                if (ret >= 0) { got_packet = 1; break; }
            } else {
                av_packet_unref(pkt);
            }
        }
        if (!got_packet) ret = -1;
    }

    if (ret < 0) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    ret = avcodec_receive_frame(dec_ctx, frame);
    if (ret < 0) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    int w = frame->width;
    int h = frame->height;

    enum AVPixelFormat src_fmt = frame->format;
    if (src_fmt == AV_PIX_FMT_YUVJ420P) {
        src_fmt = AV_PIX_FMT_YUV420P;
        frame->color_range = AVCOL_RANGE_JPEG;
    } else if (src_fmt == AV_PIX_FMT_YUVJ422P) {
        src_fmt = AV_PIX_FMT_YUV422P;
        frame->color_range = AVCOL_RANGE_JPEG;
    } else if (src_fmt == AV_PIX_FMT_YUVJ444P) {
        src_fmt = AV_PIX_FMT_YUV444P;
        frame->color_range = AVCOL_RANGE_JPEG;
    }

    struct SwsContext *sws = sws_getContext(w, h, src_fmt, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) {
        sws_freeContext(sws);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    uint8_t *dst_slice[4] = {rgba, NULL, NULL, NULL};
    int dst_stride[4] = {w * 4, 0, 0, 0};
    sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, h, dst_slice, dst_stride);

    *out_pixels = rgba;
    *out_w = w;
    *out_h = h;

    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}

pid_t render_art(int x, int y, int w, int h) {
    char *input_url = (strlen(m.art_url) > 5) ? m.art_url : m.file_path;
    if (!input_url || strlen(input_url) < 5) return -1;

    uint8_t *pixels = NULL;
    int img_w, img_h;

    if (extract_frame(input_url, &pixels, &img_w, &img_h) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        free(pixels);
        return -1;
    }

    if (pid == 0) {
        setpgid(0, 0);

        img2ascii_opts opts = {
            .target_width = get_braille_mode() ? w * 2 : w,
            .target_height = 0,
            .fuzziness = 1,
            .braille = get_braille_mode(),
        };

        char *art = img2ascii(pixels, img_w, img_h, 4, opts);
        free(pixels);

        if (!art) _exit(1);

        char clear[512];
        int cw = w < 510 ? w : 510;
        memset(clear, ' ', cw);
        clear[cw] = '\0';
        for (int r = 0; r < h; r++)
            printf("\x1b[%d;%dH%s", y + 1 + r, x + 1, clear);

        printf("\x1b[s");
        int row = 0;
        char *line = art;
        char *next;
        while ((next = strchr(line, '\n')) && row < h) {
            *next = '\0';
            printf("\x1b[%d;%dH%s", y + 1 + row, x + 1, line);
            row++;
            line = next + 1;
        }
        if (*line && row < h)
            printf("\x1b[%d;%dH%s", y + 1 + row, x + 1, line);

        printf("\x1b[u\x1b[0m");
        fflush(stdout);

        free(art);
        _exit(0);
    }

    return pid;
}
