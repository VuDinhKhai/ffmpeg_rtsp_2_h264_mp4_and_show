#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#elif __linux__
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/ffversion.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"
#include <libavdevice/avdevice.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libavutil/mem.h>
//#include <opencv2/opencv.hpp>

#include <stdio.h>
#include <SDL2/SDL.h>

void saveFrame(AVFrame* pFrame, int width, int height, int iFrame)
{
    FILE* pFile;
    char szFilename[32];
    int y;

    // Open file
    sprintf_s(szFilename, sizeof(szFilename), "frame%d.ppm", iFrame);
    if (fopen_s(&pFile, szFilename, "wb") != 0 || pFile == NULL)
        return;

    // Write file header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for (y = 0; y < height; y++)
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);

    // Close file
    fclose(pFile);
}


int flush_encoder(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, int vStreamIndex) {
    int      ret = 0;
    AVPacket* enc_pkt = av_packet_alloc();
    enc_pkt->data = NULL;
    enc_pkt->size = 0;

    if (!(codecCtx->codec->capabilities & AV_CODEC_CAP_DELAY))
        return 0;

    printf("Flushing stream #%u encoder\n", vStreamIndex);
    if (avcodec_send_frame(codecCtx, 0) >= 0) {
        while (avcodec_receive_packet(codecCtx, enc_pkt) >= 0) {
            printf("success encoder 1 frame.\n");

            // parpare packet for muxing
            enc_pkt->stream_index = vStreamIndex;
            av_packet_rescale_ts(enc_pkt, codecCtx->time_base,
                fmtCtx->streams[vStreamIndex]->time_base);
            ret = av_interleaved_write_frame(fmtCtx, enc_pkt);
            if (ret < 0) {
                break;
            }
        }
    }

    av_packet_unref(enc_pkt);

    return ret;
}

void h264tomp4() {
    int frame_index = 0;//统计帧数
    int inVStreamIndex = -1, outVStreamIndex = -1;//输入输出视频流在文件中的索引位置
    const char* inVFileName = "D:\\C++\\ffmpeg_c++\\ffmpeg_beginer\\camera.h264";
    const char* outFileName = "result.mp4";

    AVFormatContext* inVFmtCtx = NULL, * outFmtCtx = NULL;
    AVCodecParameters* codecPara = NULL;
    AVStream* outVStream = NULL;
    const AVCodec* outCodec = NULL;
    AVCodecContext* outCodecCtx = NULL;
    AVCodecParameters* outCodecPara = NULL;
    AVStream* inVStream = NULL;
    AVPacket* pkt = av_packet_alloc();

    do {
        //======================输入部分============================//
        //打开输入文件
        if (avformat_open_input(&inVFmtCtx, inVFileName, NULL, NULL) < 0) {
            printf("Cannot open input file.\n");
            break;
        }

        //查找输入文件中的流
        if (avformat_find_stream_info(inVFmtCtx, NULL) < 0) {
            printf("Cannot find stream info in input file.\n");
            break;
        }

        //查找视频流在文件中的位置
        for (size_t i = 0; i < inVFmtCtx->nb_streams; i++) {
            if (inVFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                inVStreamIndex = (int)i;
                break;
            }
        }

        codecPara = inVFmtCtx->streams[inVStreamIndex]->codecpar;//输入视频流的编码参数

        printf("===============Input information========>\n");
        av_dump_format(inVFmtCtx, 0, inVFileName, 0);
        printf("===============Input information========<\n");

        //=====================输出部分=========================//
        //打开输出文件并填充格式数据
        if (avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, outFileName) < 0) {
            printf("Cannot alloc output file context.\n");
            break;
        }

        //打开输出文件并填充数据
        if (avio_open(&outFmtCtx->pb, outFileName, AVIO_FLAG_READ_WRITE) < 0) {
            printf("output file open failed.\n");
            break;
        }

        //在输出的mp4文件中创建一条视频流
        outVStream = avformat_new_stream(outFmtCtx, NULL);
        if (!outVStream) {
            printf("Failed allocating output stream.\n");
            break;
        }
        outVStream->time_base.den = 25;
        outVStream->time_base.num = 1;
        outVStreamIndex = outVStream->index;

        //查找编码器
        outCodec = avcodec_find_encoder(codecPara->codec_id);
        if (outCodec == NULL) {
            printf("Cannot find any encoder.\n");
            break;
        }

        //从输入的h264编码器数据复制一份到输出文件的编码器中
        outCodecCtx = avcodec_alloc_context3(outCodec);
        outCodecPara = outFmtCtx->streams[outVStream->index]->codecpar;
        if (avcodec_parameters_copy(outCodecPara, codecPara) < 0) {
            printf("Cannot copy codec para.\n");
            break;
        }
        if (avcodec_parameters_to_context(outCodecCtx, outCodecPara) < 0) {
            printf("Cannot alloc codec ctx from para.\n");
            break;
        }
        outCodecCtx->time_base.den = 25;
        outCodecCtx->time_base.num = 1;

        //打开输出文件需要的编码器
        if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0) {
            printf("Cannot open output codec.\n");
            break;
        }

        printf("============Output Information=============>\n");
        av_dump_format(outFmtCtx, 0, outFileName, 1);
        printf("============Output Information=============<\n");

        //写入文件头
        if (avformat_write_header(outFmtCtx, NULL) < 0) {
            printf("Cannot write header to file.\n");
            return -1;
        }

        //===============编码部分===============//

        inVStream = inVFmtCtx->streams[inVStreamIndex];
        while (av_read_frame(inVFmtCtx, pkt) >= 0) {//循环读取每一帧直到读完
            if (pkt->stream_index == inVStreamIndex) {//确保处理的是视频流
                //FIXME：No PTS (Example: Raw H.264)
                //Simple Write PTS
                //如果当前处理帧的显示时间戳为0或者没有等等不是正常值
                if (pkt->pts == AV_NOPTS_VALUE) {
                    printf("frame_index:%d\n", frame_index);
                    //Write PTS
                    AVRational time_base1 = inVStream->time_base;
                    //Duration between 2 frames (us)s
                    int64_t calc_duration = (int)(AV_TIME_BASE / av_q2d(inVStream->r_frame_rate));
                    //Parameters
                    pkt->pts = (int)((frame_index * calc_duration) / (av_q2d(time_base1) * AV_TIME_BASE));
                    pkt->dts = pkt->pts;
                    pkt->duration = (int)(calc_duration / (av_q2d(time_base1) * AV_TIME_BASE));
                    frame_index++;
                }
                //Convert PTS/DTS
                pkt->pts = av_rescale_q_rnd(pkt->pts, inVStream->time_base, outVStream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt->dts = av_rescale_q_rnd(pkt->dts, inVStream->time_base, outVStream->time_base, (enum AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt->duration = av_rescale_q(pkt->duration, inVStream->time_base, outVStream->time_base);
                pkt->pos = -1;
                pkt->stream_index = outVStreamIndex;
                printf("Write 1 Packet. size:%5d\tpts:%lld\n", pkt->size, pkt->pts);
                //Write
                if (av_interleaved_write_frame(outFmtCtx, pkt) < 0) {
                    printf("Error muxing packet\n");
                    break;
                }
                av_packet_unref(pkt);
            }
        }

        av_write_trailer(outFmtCtx);
    } while (0);

    //=================释放所有指针=======================
    av_packet_free(&pkt);
    avformat_close_input(&outFmtCtx);
    //avcodec_close(outCodecCtx);
    avcodec_free_context(&outCodecCtx);
    avformat_close_input(&inVFmtCtx);
    avformat_free_context(inVFmtCtx);
    //avio_close(outFmtCtx->pb);
}

void getRtsp2h264()
{
    int ret = 0;
    avdevice_register_all();

    AVFormatContext* inFmtCtx = avformat_alloc_context();
    AVCodecContext* inCodecCtx = NULL;
    const AVCodec* inCodec = NULL;
    AVPacket* inPkt = av_packet_alloc();
    AVFrame* srcFrame = av_frame_alloc();
    AVFrame* yuvFrame = av_frame_alloc();

    //打开输出文件，并填充fmtCtx数据
    AVFormatContext* outFmtCtx = avformat_alloc_context();
    const AVOutputFormat* outFmt = NULL;
    AVCodecContext* outCodecCtx = NULL;
    const AVCodec* outCodec = NULL;
    AVStream* outVStream = NULL;

    AVPacket* outPkt = av_packet_alloc();

    struct SwsContext* img_ctx = NULL;

    int inVideoStreamIndex = -1;

    do {
        /////////////解码器部分//////////////////////
        //打开摄像头
//#ifdef _WIN32
//        AVInputFormat* inFmt = av_find_input_format("dshow");
//        if (avformat_open_input(&inFmtCtx, "video=USB Camera", inFmt, NULL) < 0) {
//            printf("Cannot open camera.\n");
//            break;
//        }
//#elif __linux__
//        AVInputFormat* inFmt = av_find_input_format("v4l2");
//        if (avformat_open_input(&inFmtCtx, "/dev/video0", inFmt, NULL) < 0) {
//            printf("Cannot open camera.\n");
//            break;
//        }
//#endif

#ifdef _WIN32
        AVInputFormat* inFmt = av_find_input_format("rtsp");
        if (avformat_open_input(&inFmtCtx, "rtsp://admin:admin-12345@192.168.1.5:554/1/1", inFmt, NULL) < 0) {
            printf("Cannot open RTSP stream.\n");
            break;
        }
#elif __linux__
        AVInputFormat* inFmt = av_find_input_format("rtsp");
        if (avformat_open_input(&inFmtCtx, "rtsp://admin:admin-12345@192.168.1.7:554//Streaming/Channels/101", inFmt, NULL) < 0) {
            printf("Cannot open RTSP stream.\n");
            break;
        }
#endif

        if (avformat_find_stream_info(inFmtCtx, NULL) < 0) {
            printf("Cannot find any stream in file.\n");
            break;
        }

        for (uint32_t i = 0; i < inFmtCtx->nb_streams; i++) {
            if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                inVideoStreamIndex = i;
                break;
            }
        }
        if (inVideoStreamIndex == -1) {
            printf("Cannot find video stream in file.\n");
            break;
        }

        AVCodecParameters* inVideoCodecPara = inFmtCtx->streams[inVideoStreamIndex]->codecpar;
        if (!(inCodec = avcodec_find_decoder(inVideoCodecPara->codec_id))) {
            printf("Cannot find valid video decoder.\n");
            break;
        }
        if (!(inCodecCtx = avcodec_alloc_context3(inCodec))) {
            printf("Cannot alloc valid decode codec context.\n");
            break;
        }
        if (avcodec_parameters_to_context(inCodecCtx, inVideoCodecPara) < 0) {
            printf("Cannot initialize parameters.\n");
            break;
        }

        if (avcodec_open2(inCodecCtx, inCodec, NULL) < 0) {
            printf("Cannot open codec.\n");
            break;
        }

        img_ctx = sws_getContext(inCodecCtx->width,
            inCodecCtx->height,
            inCodecCtx->pix_fmt,
            inCodecCtx->width,
            inCodecCtx->height,
            AV_PIX_FMT_YUV420P,
            SWS_BICUBIC,
            NULL, NULL, NULL);

        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
            inCodecCtx->width,
            inCodecCtx->height, 1);
        uint8_t* out_buffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

        ret = av_image_fill_arrays(yuvFrame->data,
            yuvFrame->linesize,
            out_buffer,
            AV_PIX_FMT_YUV420P,
            inCodecCtx->width,
            inCodecCtx->height,
            1);
        if (ret < 0) {
            printf("Fill arrays failed.\n");
            break;
        }
        //////////////解码器部分结束/////////////////////

        //////////////编码器部分开始/////////////////////
        const char* outFile = "camera.h264";

        if (avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, outFile) < 0) {
            printf("Cannot alloc output file context.\n");
            break;
        }
        outFmt = outFmtCtx->oformat;

        //打开输出文件
        if (avio_open(&outFmtCtx->pb, outFile, AVIO_FLAG_READ_WRITE) < 0) {
            printf("output file open failed.\n");
            break;
        }

        //创建h264视频流，并设置参数
        outVStream = avformat_new_stream(outFmtCtx, outCodec);
        if (outVStream == NULL) {
            printf("create new video stream fialed.\n");
            break;
        }
        outVStream->time_base.den = 10;
        outVStream->time_base.num = 1;

        //编码参数相关
        AVCodecParameters* outCodecPara = outFmtCtx->streams[outVStream->index]->codecpar;
        outCodecPara->codec_type = AVMEDIA_TYPE_VIDEO;
        outCodecPara->codec_id = outFmt->video_codec;
        outCodecPara->width = 1920;
        outCodecPara->height = 1080;
        //outCodecPara->bit_rate = 110000;
        outCodecPara->bit_rate = 1000;

        //查找编码器
        outCodec = avcodec_find_encoder(outFmt->video_codec);
        if (outCodec == NULL) {
            printf("Cannot find any encoder.\n");
            break;
        }

        //设置编码器内容
        outCodecCtx = avcodec_alloc_context3(outCodec);
        avcodec_parameters_to_context(outCodecCtx, outCodecPara);
        if (outCodecCtx == NULL) {
            printf("Cannot alloc output codec content.\n");
            break;
        }
        outCodecCtx->codec_id = outFmt->video_codec;
        outCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        outCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        outCodecCtx->width = inCodecCtx->width;
        outCodecCtx->height = inCodecCtx->height;
        outCodecCtx->time_base.num = 1;
        outCodecCtx->time_base.den = 30;
        //outCodecCtx->bit_rate = 110000;
        outCodecCtx->bit_rate = 1000;
        outCodecCtx->gop_size = 5;

        if (outCodecCtx->codec_id == AV_CODEC_ID_H264) {
            outCodecCtx->qmin = 10;
            outCodecCtx->qmax = 51;
            outCodecCtx->qcompress = (float)0.6;
        }
        else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            outCodecCtx->max_b_frames = 2;
        }
        else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            outCodecCtx->mb_decision = 2;
        }

        //打开编码器
        if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0) {
            printf("Open encoder failed.\n");
            break;
        }
        ///////////////编码器部分结束////////////////////

        ///////////////编解码部分//////////////////////
        yuvFrame->format = outCodecCtx->pix_fmt;
        yuvFrame->width = outCodecCtx->width;
        yuvFrame->height = outCodecCtx->height;

        ret = avformat_write_header(outFmtCtx, NULL);

        int count = 0;
        while (1) {
            if (av_read_frame(inFmtCtx, inPkt) < 0) {
                // Nếu không thể đọc được thêm gói tin nữa, thoát khỏi vòng lặp
                break;
            }
            if (inPkt->stream_index == inVideoStreamIndex) {
                if (avcodec_send_packet(inCodecCtx, inPkt) >= 0) {
                    while ((ret = avcodec_receive_frame(inCodecCtx, srcFrame)) >= 0) {
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                            return -1;
                        else if (ret < 0) {
                            fprintf(stderr, "Error during decoding\n");
                            exit(1);
                        }
                        sws_scale(img_ctx,
                            (const uint8_t* const*)srcFrame->data,
                            srcFrame->linesize,
                            0, inCodecCtx->height,
                            yuvFrame->data, yuvFrame->linesize);

                        yuvFrame->pts = srcFrame->pts;
                        //encode
                        if (avcodec_send_frame(outCodecCtx, yuvFrame) >= 0) {
                            if (avcodec_receive_packet(outCodecCtx, outPkt) >= 0) {
                                printf("encode %d frame.\n", count);
                                ++count;
                                outPkt->stream_index = outVStream->index;
                                av_packet_rescale_ts(outPkt, outCodecCtx->time_base,
                                    outVStream->time_base);
                                outPkt->pos = -1;
                                av_interleaved_write_frame(outFmtCtx, outPkt);
                                av_packet_unref(outPkt);
                            }
                        }
#ifdef _WIN32
                        Sleep(2);//延时24毫秒
#elif __linux__
                        usleep(1000 * 24);
#endif
                    }
                }
                av_packet_unref(inPkt);
                fflush(stdout);
            }
        }

        ret = flush_encoder(outFmtCtx, outCodecCtx, outVStream->index);
        if (ret < 0) {
            printf("flushing encoder failed.\n");
            break;
        }

        av_write_trailer(outFmtCtx);
        ////////////////编解码部分结束////////////////
    } while (0);

    ///////////内存释放部分/////////////////////////
    av_packet_free(&inPkt);
    avcodec_free_context(&inCodecCtx);
    //avcodec_close(inCodecCtx);
    avformat_close_input(&inFmtCtx);
    av_frame_free(&srcFrame);
    av_frame_free(&yuvFrame);

    av_packet_free(&outPkt);
    avcodec_free_context(&outCodecCtx);
    //avcodec_close(outCodecCtx);
    avformat_close_input(&outFmtCtx);
}

int encodeAndWriteVideo(AVFormatContext* outFmtCtx, AVCodecContext* outCodecCtx, AVStream* outVStream, AVFrame* yuvFrame, struct SwsContext* img_ctx) {
    int ret = 0;
    int count = 0;
    AVPacket* outPkt = av_packet_alloc();
    if (!outPkt) {
        printf("Failed to allocate packet.\n");
        return -1;
    }

    while (1) {
        if (avcodec_send_frame(outCodecCtx, yuvFrame) < 0) {
            printf("Error sending frame to encoder.\n");
            break;
        }

        while (1) {
            ret = avcodec_receive_packet(outCodecCtx, outPkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return 0;
            else if (ret < 0) {
                printf("Error during encoding.\n");
                return -1;
            }

            printf("encode %d frame.\n", count);
            ++count;
            outPkt->stream_index = outVStream->index;
            av_packet_rescale_ts(outPkt, outCodecCtx->time_base, outVStream->time_base);
            outPkt->pos = -1;

            if (av_interleaved_write_frame(outFmtCtx, outPkt) < 0) {
                printf("Error writing video frame.\n");
                av_packet_unref(outPkt);
                return -1;
            }

            av_packet_unref(outPkt);
        }
    }

    return 0;
}

void getRtsp2RGB_final_1(const char* rtspUrl) {
    int ret = 0;
    avdevice_register_all();

    AVFormatContext* inFmtCtx = NULL;
    AVCodecContext* inCodecCtx = NULL;
    const AVCodec* inCodec = NULL;
    AVPacket* inPkt = av_packet_alloc();
    AVFrame* srcFrame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();

    struct SwsContext* img_ctx = NULL;

    int inVideoStreamIndex = -1;

    do {
        // Open RTSP stream
        AVInputFormat* inFmt = av_find_input_format("rtsp");
        if (avformat_open_input(&inFmtCtx, rtspUrl, inFmt, NULL) < 0) {
            printf("Cannot open RTSP stream.\n");
            break;
        }

        // Find stream information
        if (avformat_find_stream_info(inFmtCtx, NULL) < 0) {
            printf("Cannot find any stream in file.\n");
            break;
        }

        // Find video stream
        for (uint32_t i = 0; i < inFmtCtx->nb_streams; i++) {
            if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                inVideoStreamIndex = i;
                break;
            }
        }
        if (inVideoStreamIndex == -1) {
            printf("Cannot find video stream in file.\n");
            break;
        }

        // Find decoder for the video stream
        AVCodecParameters* inVideoCodecPara = inFmtCtx->streams[inVideoStreamIndex]->codecpar;
        if (!(inCodec = avcodec_find_decoder(inVideoCodecPara->codec_id))) {
            printf("Cannot find valid video decoder.\n");
            break;
        }
        if (!(inCodecCtx = avcodec_alloc_context3(inCodec))) {
            printf("Cannot alloc valid decode codec context.\n");
            break;
        }
        if (avcodec_parameters_to_context(inCodecCtx, inVideoCodecPara) < 0) {
            printf("Cannot initialize parameters.\n");
            break;
        }
        if (avcodec_open2(inCodecCtx, inCodec, NULL) < 0) {
            printf("Cannot open codec.\n");
            break;
        }

        // Create RGB frame
        rgbFrame->format = AV_PIX_FMT_RGB24;
        rgbFrame->width = inCodecCtx->width;
        rgbFrame->height = inCodecCtx->height;
        ret = av_frame_get_buffer(rgbFrame, 32);
        if (ret < 0) {
            printf("Failed to allocate RGB frame.\n");
            break;
        }

        // Allocate SwsContext for color space conversion
        img_ctx = sws_getContext(inCodecCtx->width, inCodecCtx->height, inCodecCtx->pix_fmt,
            inCodecCtx->width, inCodecCtx->height, AV_PIX_FMT_RGB24,
            SWS_BICUBIC, NULL, NULL, NULL);
        if (!img_ctx) {
            printf("Cannot initialize the conversion context.\n");
            break;
        }

        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL window
        //SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, inCodecCtx->width, inCodecCtx->height, SDL_WINDOW_SHOWN);

        SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,1280,720, SDL_WINDOW_SHOWN);
        if (window == NULL) {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL renderer
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (renderer == NULL) {
            printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL texture
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, inCodecCtx->width, inCodecCtx->height);
        if (texture == NULL) {
            printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }
        // Read frames from the RTSP stream
        while (av_read_frame(inFmtCtx, inPkt) >= 0) {
            if (inPkt->stream_index == inVideoStreamIndex) {
                if (avcodec_send_packet(inCodecCtx, inPkt) >= 0) {
                    while ((ret = avcodec_receive_frame(inCodecCtx, srcFrame)) >= 0) {
                        sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, rgbFrame->data, rgbFrame->linesize);

                        // Update SDL texture
                        SDL_UpdateTexture(texture, NULL, rgbFrame->data[0], rgbFrame->linesize[0]);

                        // Clear screen
                        SDL_RenderClear(renderer);

                        // Copy texture to renderer
                        SDL_RenderCopy(renderer, texture, NULL, NULL);

                        // Render the changes above
                        SDL_RenderPresent(renderer);

                    }
                }
            }
            av_packet_unref(inPkt);
        }

        // Flush the decoder
        avcodec_send_packet(inCodecCtx, NULL);
        while (avcodec_receive_frame(inCodecCtx, srcFrame) >= 0) {
            sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, rgbFrame->data, rgbFrame->linesize);


        }
        // Destroy SDL objects
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

    } while (0);

    // Clean up resources
    av_packet_free(&inPkt);
    avcodec_free_context(&inCodecCtx);
    avformat_close_input(&inFmtCtx);
    av_frame_free(&srcFrame);
    av_frame_free(&rgbFrame);
    sws_freeContext(img_ctx);
}

void getRtsp2yuv(const char* rtspUrl) {
    int ret = 0;
    avdevice_register_all();

    AVFormatContext* inFmtCtx = NULL;
    AVCodecContext* inCodecCtx = NULL;
    const AVCodec* inCodec = NULL;
    AVPacket* inPkt = av_packet_alloc();
    AVFrame* srcFrame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVFrame* yuvFrame = av_frame_alloc();

    //打开输出文件，并填充fmtCtx数据
    AVFormatContext* outFmtCtx = avformat_alloc_context();
    const AVOutputFormat* outFmt = NULL;
    AVCodecContext* outCodecCtx = NULL;
    const AVCodec* outCodec = NULL;
    AVStream* outVStream = NULL;

    AVPacket* outPkt = av_packet_alloc();

    struct SwsContext* img_ctx = NULL;

    int inVideoStreamIndex = -1;

    do {
        // Open RTSP stream
        AVInputFormat* inFmt = av_find_input_format("rtsp");
        if (avformat_open_input(&inFmtCtx, rtspUrl, inFmt, NULL) < 0) {
            printf("Cannot open RTSP stream.\n");
            break;
        }

        // Find stream information
        if (avformat_find_stream_info(inFmtCtx, NULL) < 0) {
            printf("Cannot find any stream in file.\n");
            break;
        }

        // Find video stream
        for (uint32_t i = 0; i < inFmtCtx->nb_streams; i++) {
            if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                inVideoStreamIndex = i;
                break;
            }
        }
        if (inVideoStreamIndex == -1) {
            printf("Cannot find video stream in file.\n");
            break;
        }

        // Find decoder for the video stream
        AVCodecParameters* inVideoCodecPara = inFmtCtx->streams[inVideoStreamIndex]->codecpar;
        if (!(inCodec = avcodec_find_decoder(inVideoCodecPara->codec_id))) {
            printf("Cannot find valid video decoder.\n");
            break;
        }
        if (!(inCodecCtx = avcodec_alloc_context3(inCodec))) {
            printf("Cannot alloc valid decode codec context.\n");
            break;
        }
        if (avcodec_parameters_to_context(inCodecCtx, inVideoCodecPara) < 0) {
            printf("Cannot initialize parameters.\n");
            break;
        }
        if (avcodec_open2(inCodecCtx, inCodec, NULL) < 0) {
            printf("Cannot open codec.\n");
            break;
        }

        // Create RGB frame
        yuvFrame->format = AV_PIX_FMT_YUV420P;
        yuvFrame->width = inCodecCtx->width;
        yuvFrame->height = inCodecCtx->height;
        ret = av_frame_get_buffer(yuvFrame, 32);
        if (ret < 0) {
            printf("Failed to allocate RGB frame.\n");
            break;
        }

        // Allocate SwsContext for color space conversion
        img_ctx = sws_getContext(inCodecCtx->width, inCodecCtx->height, inCodecCtx->pix_fmt,
            inCodecCtx->width, inCodecCtx->height, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, NULL, NULL, NULL);

        if (!img_ctx) {
            printf("Cannot initialize the conversion context.\n");
            break;
        }

        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL window
        //SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, inCodecCtx->width, inCodecCtx->height, SDL_WINDOW_SHOWN);

        SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (window == NULL) {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL renderer
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (renderer == NULL) {
            printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL texture
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, inCodecCtx->width, inCodecCtx->height);
        if (texture == NULL) {
            printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }
        // Read frames from the RTSP stream
        while (av_read_frame(inFmtCtx, inPkt) >= 0) {
            if (inPkt->stream_index == inVideoStreamIndex) {
                if (avcodec_send_packet(inCodecCtx, inPkt) >= 0) {
                    while ((ret = avcodec_receive_frame(inCodecCtx, srcFrame)) >= 0) {
                        sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, yuvFrame->data, yuvFrame->linesize);

                        // Update SDL texture
                        //SDL_UpdateTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0]);
                        SDL_UpdateYUVTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0], yuvFrame->data[1], yuvFrame->linesize[1], yuvFrame->data[2], yuvFrame->linesize[2],"texture update");

                        // Clear screen
                        SDL_RenderClear(renderer);

                        // Copy texture to renderer
                        SDL_RenderCopy(renderer, texture, NULL, NULL);

                        // Render the changes above
                        SDL_RenderPresent(renderer);

                    }
                }
            }
            av_packet_unref(inPkt);
        }

        // Flush the decoder
        avcodec_send_packet(inCodecCtx, NULL);
        while (avcodec_receive_frame(inCodecCtx, srcFrame) >= 0) {
            sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, yuvFrame->data, yuvFrame->linesize);


        }
        // Destroy SDL objects
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

    } while (0);

    // Clean up resources
    av_packet_free(&inPkt);
    avcodec_free_context(&inCodecCtx);
    avformat_close_input(&inFmtCtx);
    av_frame_free(&srcFrame);
    av_frame_free(&yuvFrame);
    sws_freeContext(img_ctx);
}

void getRtsp2yuv_save(const char* rtspUrl , const char* outfile) {
    int ret = 0;
    avdevice_register_all();

    AVFormatContext* inFmtCtx = NULL;
    AVCodecContext* inCodecCtx = NULL;
    const AVCodec* inCodec = NULL;
    AVPacket* inPkt = av_packet_alloc();
    AVFrame* srcFrame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVFrame* yuvFrame = av_frame_alloc();

    //打开输出文件，并填充fmtCtx数据
    AVFormatContext* outFmtCtx = avformat_alloc_context();
    const AVOutputFormat* outFmt = NULL;
    AVCodecContext* outCodecCtx = NULL;
    const AVCodec* outCodec = NULL;
    AVStream* outVStream = NULL;

    AVPacket* outPkt = av_packet_alloc();

    struct SwsContext* img_ctx = NULL;

    int inVideoStreamIndex = -1;

    do {
        // Open RTSP stream
        AVInputFormat* inFmt = av_find_input_format("rtsp");
        if (avformat_open_input(&inFmtCtx, rtspUrl, inFmt, NULL) < 0) {
            printf("Cannot open RTSP stream.\n");
            break;
        }

        // Find stream information
        if (avformat_find_stream_info(inFmtCtx, NULL) < 0) {
            printf("Cannot find any stream in file.\n");
            break;
        }

        // Find video stream
        for (uint32_t i = 0; i < inFmtCtx->nb_streams; i++) {
            if (inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                inVideoStreamIndex = i;
                break;
            }
        }
        if (inVideoStreamIndex == -1) {
            printf("Cannot find video stream in file.\n");
            break;
        }

        // Find decoder for the video stream
        AVCodecParameters* inVideoCodecPara = inFmtCtx->streams[inVideoStreamIndex]->codecpar;
        if (!(inCodec = avcodec_find_decoder(inVideoCodecPara->codec_id))) {
            printf("Cannot find valid video decoder.\n");
            break;
        }
        if (!(inCodecCtx = avcodec_alloc_context3(inCodec))) {
            printf("Cannot alloc valid decode codec context.\n");
            break;
        }
        if (avcodec_parameters_to_context(inCodecCtx, inVideoCodecPara) < 0) {
            printf("Cannot initialize parameters.\n");
            break;
        }
        if (avcodec_open2(inCodecCtx, inCodec, NULL) < 0) {
            printf("Cannot open codec.\n");
            break;
        }

        // Create RGB frame
        yuvFrame->format = AV_PIX_FMT_YUV420P;
        yuvFrame->width = inCodecCtx->width;
        yuvFrame->height = inCodecCtx->height;
        ret = av_frame_get_buffer(yuvFrame, 32);
        if (ret < 0) {
            printf("Failed to allocate RGB frame.\n");
            break;
        }

        // Allocate SwsContext for color space conversion
        img_ctx = sws_getContext(inCodecCtx->width, inCodecCtx->height, inCodecCtx->pix_fmt,
            inCodecCtx->width, inCodecCtx->height, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, NULL, NULL, NULL);

        if (!img_ctx) {
            printf("Cannot initialize the conversion context.\n");
            break;
        }
        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
                                                inCodecCtx->width,
                                                inCodecCtx->height, 1);

        uint8_t* out_buffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

        ret = av_image_fill_arrays(yuvFrame->data,
                                    yuvFrame->linesize,
                                    out_buffer,
                                    AV_PIX_FMT_YUV420P,
                                    inCodecCtx->width,
                                    inCodecCtx->height,
                                    1);
        if (ret < 0) {
            printf("Fill arrays failed.\n");
            break;
        }
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL window
        //SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, inCodecCtx->width, inCodecCtx->height, SDL_WINDOW_SHOWN);

        SDL_Window* window = SDL_CreateWindow("SDL Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (window == NULL) {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL renderer
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (renderer == NULL) {
            printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }

        // Create SDL texture
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, inCodecCtx->width, inCodecCtx->height);
        if (texture == NULL) {
            printf("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
            break;
        }


        //////////////编码器部分开始/////////////////////
        const char* outFile = "camera.h264";

        if (avformat_alloc_output_context2(&outFmtCtx, NULL, NULL, outFile) < 0) {
            printf("Cannot alloc output file context.\n");
            break;
        }

        outFmt = outFmtCtx->oformat;

        //打开输出文件
        if (avio_open(&outFmtCtx->pb, outFile, AVIO_FLAG_READ_WRITE) < 0) {
            printf("output file open failed.\n");
            break;
        }

        //创建h264视频流，并设置参数
        outVStream = avformat_new_stream(outFmtCtx, outCodec);
        if (outVStream == NULL) {
            printf("create new video stream fialed.\n");
            break;
        }
        outVStream->time_base.den = 10;
        outVStream->time_base.num = 1;

        //编码参数相关
        AVCodecParameters* outCodecPara = outFmtCtx->streams[outVStream->index]->codecpar;
        outCodecPara->codec_type = AVMEDIA_TYPE_VIDEO;
        outCodecPara->codec_id = outFmt->video_codec;
        outCodecPara->width = 1920;
        outCodecPara->height = 1080;
        //outCodecPara->bit_rate = 110000;
        outCodecPara->bit_rate = 1000;

        //查找编码器
        outCodec = avcodec_find_encoder(outFmt->video_codec);
        if (outCodec == NULL) {
            printf("Cannot find any encoder.\n");
            break;
        }

        //设置编码器内容
        outCodecCtx = avcodec_alloc_context3(outCodec);
        avcodec_parameters_to_context(outCodecCtx, outCodecPara);
        if (outCodecCtx == NULL) {
            printf("Cannot alloc output codec content.\n");
            break;
        }
        outCodecCtx->codec_id = outFmt->video_codec;
        outCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        outCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        outCodecCtx->width = inCodecCtx->width;
        outCodecCtx->height = inCodecCtx->height;
        outCodecCtx->time_base.num = 1;
        outCodecCtx->time_base.den = 30;
        //outCodecCtx->bit_rate = 110000;
        outCodecCtx->bit_rate = 1000;
        outCodecCtx->gop_size = 5;

        if (outCodecCtx->codec_id == AV_CODEC_ID_H264) {
            outCodecCtx->qmin = 10;
            outCodecCtx->qmax = 51;
            outCodecCtx->qcompress = (float)0.6;
        }
        else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            outCodecCtx->max_b_frames = 2;
        }
        else if (outCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            outCodecCtx->mb_decision = 2;
        }

        //打开编码器
        if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0) {
            printf("Open encoder failed.\n");
            break;
        }
        ///////////////编码器部分结束////////////////////
        yuvFrame->format = outCodecCtx->pix_fmt;
        yuvFrame->width = outCodecCtx->width;
        yuvFrame->height = outCodecCtx->height;

        ret = avformat_write_header(outFmtCtx, NULL);

        int count = 0;

        // Read frames from the RTSP stream
        while (1) {
            if (av_read_frame(inFmtCtx, inPkt) < 0) {
                // Nếu không thể đọc được thêm gói tin nữa, thoát khỏi vòng lặp
                break;
            }
            if (inPkt->stream_index == inVideoStreamIndex) {
                if (avcodec_send_packet(inCodecCtx, inPkt) >= 0) {
                    while ((ret = avcodec_receive_frame(inCodecCtx, srcFrame)) >= 0) {
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                            return -1;
                        else if (ret < 0) {
                            fprintf(stderr, "Error during decoding\n");
                            exit(1);
                        }
                        sws_scale(img_ctx,
                            (const uint8_t* const*)srcFrame->data,
                            srcFrame->linesize,
                            0, inCodecCtx->height,
                            yuvFrame->data, yuvFrame->linesize);
                        //sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, yuvFrame->data, yuvFrame->linesize);
                        yuvFrame->pts = srcFrame->pts;


                        // Update SDL texture
                        //SDL_UpdateTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0]);
                        SDL_UpdateYUVTexture(texture, NULL, yuvFrame->data[0], yuvFrame->linesize[0], yuvFrame->data[1], yuvFrame->linesize[1], yuvFrame->data[2], yuvFrame->linesize[2], "texture update");

                        // Clear screen
                        SDL_RenderClear(renderer);

                        // Copy texture to renderer
                        SDL_RenderCopy(renderer, texture, NULL, NULL);

                        // Render the changes above
                        SDL_RenderPresent(renderer);

                        //encode
                        if (avcodec_send_frame(outCodecCtx, yuvFrame) >= 0) {
                            if (avcodec_receive_packet(outCodecCtx, outPkt) >= 0) {
                                printf("encode %d frame.\n", count);
                                ++count;
                                outPkt->stream_index = outVStream->index;
                                av_packet_rescale_ts(outPkt, outCodecCtx->time_base,
                                    outVStream->time_base);
                                outPkt->pos = -1;
                                av_interleaved_write_frame(outFmtCtx, outPkt);
                                av_packet_unref(outPkt);
                            }
                        }
#ifdef _WIN32
                        Sleep(24);//延时24毫秒
#elif __linux__
                        usleep(1000 * 24);
#endif


                    }
                }
                av_packet_unref(inPkt);
                fflush(stdout);
            }
        }


        ret = flush_encoder(outFmtCtx, outCodecCtx, outVStream->index);
        if (ret < 0) {
            printf("flushing encoder failed.\n");
            break;
        }

        av_write_trailer(outFmtCtx);

        // Flush the decoder
        avcodec_send_packet(inCodecCtx, NULL);
        while (avcodec_receive_frame(inCodecCtx, srcFrame) >= 0) {
            sws_scale(img_ctx, (const uint8_t* const*)srcFrame->data, srcFrame->linesize, 0, inCodecCtx->height, yuvFrame->data, yuvFrame->linesize);


        }
        // Destroy SDL objects
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

    } while (0);

    // Clean up resources
    av_packet_free(&inPkt);
    avcodec_free_context(&inCodecCtx);
    avformat_close_input(&inFmtCtx);
    av_frame_free(&srcFrame);
    av_frame_free(&yuvFrame);
    sws_freeContext(img_ctx);

    av_packet_free(&outPkt);
    avcodec_free_context(&outCodecCtx);
    //avcodec_close(outCodecCtx);
    avformat_close_input(&outFmtCtx);
}


int main() {
    //const char* rtspUrl = "rtsp://admin:admin-12345@192.168.1.7:554//Streaming/Channels/101";
    const char* rtspUrl = "rtsp://admin:admin-12345@192.168.1.5:554/1/1";
    const char* outfile = "camera.h264";
    getRtsp2yuv_save(rtspUrl, outfile);
    //getRtsp2h264();
    return 0;
}






















//int flush_encoder(AVFormatContext* fmtCtx, AVCodecContext* codecCtx, int vStreamIndex) {
//    int      ret;
//    AVPacket* enc_pkt = av_packet_alloc();
//    enc_pkt->data = NULL;
//    enc_pkt->size = 0;
//
//    if (!(codecCtx->codec->capabilities & AV_CODEC_CAP_DELAY))
//        return 0;
//
//    printf("Flushing stream #%u encoder\n", vStreamIndex);
//    if ((ret = avcodec_send_frame(codecCtx, 0)) >= 0) {
//        while (avcodec_receive_packet(codecCtx, enc_pkt) >= 0) {
//            printf("success encoder 1 frame.\n");
//
//            // parpare packet for muxing
//            enc_pkt->stream_index = vStreamIndex;
//            av_packet_rescale_ts(enc_pkt, codecCtx->time_base,
//                fmtCtx->streams[vStreamIndex]->time_base);
//            ret = av_interleaved_write_frame(fmtCtx, enc_pkt);
//            if (ret < 0) {
//                break;
//            }
//        }
//    }
//
//    return ret;
//}
////10.11.video_encode_yuv2h264
//int main()
//{
//    AVFormatContext* fmtCtx = NULL;
//    const AVOutputFormat* outFmt = NULL;
//    AVStream* vStream = NULL;
//    AVCodecContext* codecCtx = NULL;
//    const AVCodec* codec = NULL;
//    AVPacket* pkt = av_packet_alloc(); //创建已编码帧
//
//    uint8_t* picture_buf = NULL;
//    AVFrame* picFrame = NULL;
//    size_t size;
//    int ret = -1;
//
//    //[1]!打开视频文件
//    FILE* in_file = fopen("D:\\C++\\ffmpeg_c++\\ffmpeg_beginer\\result.yuv", "rb");
//    if (!in_file) {
//        printf("can not open file!\n");
//        return -1;
//    }
//    //[1]!
//
//    do {
//        //[2]!打开输出文件，并填充fmtCtx数据
//        // TODO 根据需要修改参数
//        int in_w = 1280, in_h = 720, frameCnt = 23005;
//        const char* outFile = "result.h264";
//        if (avformat_alloc_output_context2(&fmtCtx, NULL, NULL, outFile) < 0) {
//            printf("Cannot alloc output file context.\n");
//            break;
//        }
//        outFmt = fmtCtx->oformat;
//        //[2]!
//
//        //[3]!打开输出文件
//        if (avio_open(&fmtCtx->pb, outFile, AVIO_FLAG_READ_WRITE) < 0) {
//            printf("output file open failed.\n");
//            break;
//        }
//        //[3]!
//
//        //[4]!创建h264视频流，并设置参数
//        vStream = avformat_new_stream(fmtCtx, codec);
//        if (vStream == NULL) {
//            printf("failed create new video stream.\n");
//            break;
//        }
//        vStream->time_base.den = 25;
//        vStream->time_base.num = 1;
//        //[4]!
//
//        //[5]!编码参数相关
//        AVCodecParameters* codecPara = fmtCtx->streams[vStream->index]->codecpar;
//        codecPara->codec_type = AVMEDIA_TYPE_VIDEO;
//        codecPara->width = in_w;
//        codecPara->height = in_h;
//        //[5]!
//
//        //[6]!查找编码器
//        codec = avcodec_find_encoder(outFmt->video_codec);
//        if (codec == NULL) {
//            printf("Cannot find any endcoder.\n");
//            break;
//        }
//        //[6]!
//
//        //[7]!设置编码器内容
//        codecCtx = avcodec_alloc_context3(codec);
//        avcodec_parameters_to_context(codecCtx, codecPara);
//        if (codecCtx == NULL) {
//            printf("Cannot alloc context.\n");
//            break;
//        }
//
//        codecCtx->codec_id = outFmt->video_codec;
//        codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
//        codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
//        codecCtx->width = in_w;
//        codecCtx->height = in_h;
//        codecCtx->time_base.num = 1;
//        codecCtx->time_base.den = 25;
//        codecCtx->bit_rate = 400000;
//        codecCtx->gop_size = 12;
//
//        if (codecCtx->codec_id == AV_CODEC_ID_H264) {
//            codecCtx->qmin = 10;
//            codecCtx->qmax = 51;
//            codecCtx->qcompress = (float)0.6;
//        }
//        if (codecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
//            codecCtx->max_b_frames = 2;
//        if (codecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
//            codecCtx->mb_decision = 2;
//        //[7]!
//
//        //[8]!打开编码器
//        if (avcodec_open2(codecCtx, codec, NULL) < 0) {
//            printf("Open encoder failed.\n");
//            break;
//        }
//        //[8]!
//
//        av_dump_format(fmtCtx, 0, outFile, 1);//输出 输出文件流信息
//
//        //初始化帧
//        picFrame = av_frame_alloc();
//        picFrame->width = codecCtx->width;
//        picFrame->height = codecCtx->height;
//        picFrame->format = codecCtx->pix_fmt;
//        size = (size_t)av_image_get_buffer_size(codecCtx->pix_fmt, codecCtx->width, codecCtx->height, 1);
//        picture_buf = (uint8_t*)av_malloc(size);
//        av_image_fill_arrays(picFrame->data, picFrame->linesize,
//            picture_buf, codecCtx->pix_fmt,
//            codecCtx->width, codecCtx->height, 1);
//
//        //[9] --写头文件
//        ret = avformat_write_header(fmtCtx, NULL);
//        //[9]
//
//        int      y_size = codecCtx->width * codecCtx->height;
//        av_new_packet(pkt, (int)(size * 3));
//
//        //[10] --循环编码每一帧
//        for (int i = 0; i < frameCnt; i++) {
//            //读入YUV
//            if (fread(picture_buf, 1, (unsigned long)(y_size * 3 / 2), in_file) <= 0) {
//                printf("read file fail!\n");
//                return -1;
//            }
//            else if (feof(in_file))
//                break;
//
//            picFrame->data[0] = picture_buf;                  //亮度Y
//            picFrame->data[1] = picture_buf + y_size;         // U
//            picFrame->data[2] = picture_buf + y_size * 5 / 4; // V
//            // AVFrame PTS
//            picFrame->pts = i;
//
//            //编码
//            if (avcodec_send_frame(codecCtx, picFrame) >= 0) {
//                while (avcodec_receive_packet(codecCtx, pkt) >= 0) {
//                    printf("encoder %d success!\n", i);
//
//                    // parpare packet for muxing
//                    pkt->stream_index = vStream->index;
//                    av_packet_rescale_ts(pkt, codecCtx->time_base, vStream->time_base);
//                    pkt->pos = -1;
//                    ret = av_interleaved_write_frame(fmtCtx, pkt);
//                    if (ret < 0) {
//                        printf("error is: %s.\n", av_err2str(ret));
//                    }
//
//                    av_packet_unref(pkt);//刷新缓存
//                }
//            }
//        }
//        //[10]
//
//        //[11] --Flush encoder
//        ret = flush_encoder(fmtCtx, codecCtx, vStream->index);
//        if (ret < 0) {
//            printf("flushing encoder failed!\n");
//            break;
//        }
//        //[11]
//
//        //[12] --写文件尾
//        av_write_trailer(fmtCtx);
//        //[12]
//    } while (0);
//
//    //释放内存
//    av_packet_free(&pkt);
//    //avcodec_close(codecCtx);
//    av_free(picFrame);
//    av_free(picture_buf);
//
//    if (fmtCtx) {
//        avio_close(fmtCtx->pb);
//        avformat_free_context(fmtCtx);
//    }
//
//    fclose(in_file);
//
//    return 0;
//}



















//10.06.2.video_decode_mp42yuv420sp
//int main() {
//        FILE* fp;
//        errno_t err;
//        err = fopen_s(&fp, "result.yuv", "w+b");
//        if (err != 0 || fp == NULL) {
//            printf("Cannot open file.\n");
//            return -1;
//        }
//
//    char filePath[] = "D:\\cte.mp4";//文件地址
//    int  videoStreamIndex = -1;//视频流所在流序列中的索引
//    int ret = 0;//默认返回值
//
//    //需要的变量名并初始化
//    AVFormatContext* fmtCtx = NULL;
//    AVPacket* pkt = NULL;
//    AVCodecContext* codecCtx = NULL;
//    AVCodecParameters* avCodecPara = NULL;
//    const AVCodec* codec = NULL;
//    AVFrame* yuvFrame = av_frame_alloc();
//    AVFrame* nv12Frame = av_frame_alloc();
//
//    unsigned char* out_buffer = NULL;
//
//    do {
//        //=========================== 创建AVFormatContext结构体 ===============================//
//        //分配一个AVFormatContext，FFMPEG所有的操作都要通过这个AVFormatContext来进行
//        fmtCtx = avformat_alloc_context();
//        //==================================== 打开文件 ======================================//
//        if ((ret = avformat_open_input(&fmtCtx, filePath, NULL, NULL)) != 0) {
//            printf("cannot open video file\n");
//            break;
//        }
//
//        //=================================== 获取视频流信息 ===================================//
//        if ((ret = avformat_find_stream_info(fmtCtx, NULL)) < 0) {
//            printf("cannot retrive video info\n");
//            break;
//        }
//
//        //循环查找视频中包含的流信息，直到找到视频类型的流
//        //便将其记录下来 保存到videoStreamIndex变量中
//        for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
//            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//                videoStreamIndex = i;
//                break;//找到视频流就退出
//            }
//        }
//
//        //如果videoStream为-1 说明没有找到视频流
//        if (videoStreamIndex == -1) {
//            printf("cannot find video stream\n");
//            break;
//        }
//
//        //打印输入和输出信息：长度 比特率 流格式等
//        av_dump_format(fmtCtx, 0, filePath, 0);
//
//        //=================================  查找解码器 ===================================//
//        avCodecPara = fmtCtx->streams[videoStreamIndex]->codecpar;
//        codec = avcodec_find_decoder(avCodecPara->codec_id);
//        if (codec == NULL) {
//            printf("cannot find decoder\n");
//            break;
//        }
//        //根据解码器参数来创建解码器内容
//        codecCtx = avcodec_alloc_context3(codec);
//        avcodec_parameters_to_context(codecCtx, avCodecPara);
//        if (codecCtx == NULL) {
//            printf("Cannot alloc context.");
//            break;
//        }
//
//        //================================  打开解码器 ===================================//
//        if ((ret = avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
//            printf("cannot open decoder\n");
//            break;
//        }
//
//        int w = codecCtx->width;//视频宽度
//        int h = codecCtx->height;//视频高度
//
//        //================================ 设置数据转换参数 ================================//
//        struct SwsContext* img_ctx = sws_getContext(
//            codecCtx->width, codecCtx->height, codecCtx->pix_fmt, //源地址长宽以及数据格式
//            codecCtx->width, codecCtx->height, AV_PIX_FMT_NV12,  //目的地址长宽以及数据格式
//            SWS_BICUBIC, NULL, NULL, NULL);                       //算法类型  AV_PIX_FMT_YUVJ420P   AV_PIX_FMT_BGR24
//
////==================================== 分配空间 ==================================//
////一帧图像数据大小
//        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, codecCtx->width, codecCtx->height, 1);
//        out_buffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));
//
//        //=========================== 分配AVPacket结构体 ===============================//
//        pkt = av_packet_alloc();                      //分配一个packet
//        av_new_packet(pkt, codecCtx->width * codecCtx->height); //调整packet的数据
//        //会将pFrameRGB的数据按RGB格式自动"关联"到buffer  即nv12Frame中的数据改变了
//        //out_buffer中的数据也会相应的改变
//        av_image_fill_arrays(nv12Frame->data, nv12Frame->linesize, out_buffer, AV_PIX_FMT_NV12,
//            codecCtx->width, codecCtx->height, 1);
//
//        //===========================  读取视频信息 ===============================//
//        int frameCnt = 0;//帧数
//        while (av_read_frame(fmtCtx, pkt) >= 0) { //读取的是一帧视频  数据存入一个AVPacket的结构中
//            if (pkt->stream_index == videoStreamIndex) {
//                if (avcodec_send_packet(codecCtx, pkt) == 0) {
//                    while (avcodec_receive_frame(codecCtx, yuvFrame) == 0) {
//                        sws_scale(img_ctx,
//                            (const uint8_t* const*)yuvFrame->data,
//                            yuvFrame->linesize,
//                            0,
//                            h,
//                            nv12Frame->data,
//                            nv12Frame->linesize);
//                        fwrite(nv12Frame->data[0], 1, w * h, fp);//y
//                        fwrite(nv12Frame->data[1], 1, w * h / 2, fp);//uv
//
//                        printf("save frame %d to file.\n", frameCnt++);
//                        fflush(fp);
//                    }
//                }
//            }
//            av_packet_unref(pkt);//重置pkt的内容
//        }
//    } while (0);
//    //===========================释放所有指针===============================//
//    av_packet_free(&pkt);
//    //avcodec_close(codecCtx);
//    avformat_close_input(&fmtCtx);
//    avformat_free_context(fmtCtx);
//    av_frame_free(&yuvFrame);
//    av_frame_free(&nv12Frame);
//
//    av_free(out_buffer);
//
//    return ret;
//}

//10.06.1.video_decode_mp42yuv420p
//int main() {
//    FILE* fp;
//    errno_t err;
//    err = fopen_s(&fp, "result.yuv", "w+b");
//    if (err != 0 || fp == NULL) {
//        printf("Cannot open file.\n");
//        return -1;
//    }
//
//    char filePath[] = "D:\\cte.mp4";//文件地址
//    int  videoStreamIndex = -1;//视频流所在流序列中的索引
//    int ret = 0;//默认返回值
//
//    //需要的变量名并初始化
//    AVFormatContext* fmtCtx = NULL;
//    AVPacket* pkt = NULL;
//    AVCodecContext* codecCtx = NULL;
//    AVCodecParameters* avCodecPara = NULL;
//    const AVCodec* codec = NULL;
//    AVFrame* yuvFrame = av_frame_alloc();
//
//    do {
//        //=========================== 创建AVFormatContext结构体 ===============================//
//        //分配一个AVFormatContext，FFMPEG所有的操作都要通过这个AVFormatContext来进行
//        fmtCtx = avformat_alloc_context();
//        //==================================== 打开文件 ======================================//
//        if ((ret = avformat_open_input(&fmtCtx, filePath, NULL, NULL)) != 0) {
//            printf("cannot open video file\n");
//            break;
//        }
//
//        //=================================== 获取视频流信息 ===================================//
//        if ((ret = avformat_find_stream_info(fmtCtx, NULL)) < 0) {
//            printf("cannot retrive video info\n");
//            break;
//        }
//
//        //循环查找视频中包含的流信息，直到找到视频类型的流
//        //便将其记录下来 保存到videoStreamIndex变量中
//        for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
//            if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//                videoStreamIndex = i;
//                break;//找到视频流就退出
//            }
//        }
//
//        //如果videoStream为-1 说明没有找到视频流
//        if (videoStreamIndex == -1) {
//            printf("cannot find video stream\n");
//            break;
//        }
//
//        //打印输入和输出信息：长度 比特率 流格式等
//        av_dump_format(fmtCtx, 0, filePath, 0);
//
//        //=================================  查找解码器 ===================================//
//        avCodecPara = fmtCtx->streams[videoStreamIndex]->codecpar;
//        codec = avcodec_find_decoder(avCodecPara->codec_id);
//        if (codec == NULL) {
//            printf("cannot find decoder\n");
//            break;
//        }
//        //根据解码器参数来创建解码器内容
//        codecCtx = avcodec_alloc_context3(codec);
//        avcodec_parameters_to_context(codecCtx, avCodecPara);
//        if (codecCtx == NULL) {
//            printf("Cannot alloc context.");
//            break;
//        }
//
//        //================================  打开解码器 ===================================//
//        if ((ret = avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
//            printf("cannot open decoder\n");
//            break;
//        }
//
//        int w = codecCtx->width;//视频宽度
//        int h = codecCtx->height;//视频高度
//
//        //=========================== 分配AVPacket结构体 ===============================//
//        pkt = av_packet_alloc();                      //分配一个packet
//        av_new_packet(pkt, codecCtx->width * codecCtx->height); //调整packet的数据
//
//        //===========================  读取视频信息 ===============================//
//        int frameCnt = 0;//帧数
//        while (av_read_frame(fmtCtx, pkt) >= 0) { //读取的是一帧视频  数据存入一个AVPacket的结构中
//            if (pkt->stream_index == videoStreamIndex) {
//                if (avcodec_send_packet(codecCtx, pkt) == 0) {
//                    while (avcodec_receive_frame(codecCtx, yuvFrame) == 0) {
//                        fwrite(yuvFrame->data[0], 1, w * h, fp);//y
//                        fwrite(yuvFrame->data[1], 1, w * h / 4, fp);//u
//                        fwrite(yuvFrame->data[2], 1, w * h / 4, fp);//v
//
//                        printf("save frame %d to file.\n", frameCnt++);
//                        fflush(fp);
//                    }
//                }
//            }
//            av_packet_unref(pkt);//重置pkt的内容
//        }
//    } while (0);
//    //===========================释放所有指针===============================//
//    av_packet_free(&pkt);
//    //avcodec_close(codecCtx);
//    avformat_close_input(&fmtCtx);
//    avformat_free_context(fmtCtx);
//    av_frame_free(&yuvFrame);
//
//    return ret;
//}







// 10.05.video_decode_frame_save
//int main()
//{
    //unsigned codecVer = avcodec_version();
    //int ver_major,ver_minor,ver_micro;
    //ver_major = (codecVer>>16)&0xff;
    //ver_minor = (codecVer>>8)&0xff;
    //ver_micro = (codecVer)&0xff;
    //printf("FFmpeg version is: %s .\navcodec version is: %d=%d.%d.%d.\n",
    //       FFMPEG_VERSION,
    //       codecVer,ver_major,ver_minor,ver_micro);


    //AVFormatContext* fmt_ctx = avformat_alloc_context();
    //int ret = 0;
    //char* fileName = "D:\\cte.mp4";

    //do {
    //    if ((ret = avformat_open_input(&fmt_ctx, fileName, NULL, NULL)) < 0)
    //        break;//Cannot open video file

    //    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
    //        printf("Cannot find stream information\n");
    //        break;
    //    }

    //    av_dump_format(fmt_ctx, 0, fileName, 0);
    //} while (0);

    //avformat_close_input(&fmt_ctx);

    //return ret;

    //char filePath[] = "D:\\cte.mp4";//文件地址
    //int  videoStreamIndex = -1;//视频流所在流序列中的索引
    //int ret = 0;//默认返回值

    ////需要的变量名并初始化
    //AVFormatContext* fmtCtx = NULL;
    //AVPacket* pkt = NULL;
    //AVCodecContext* codecCtx = NULL;
    //AVCodecParameters* avCodecPara = NULL;
    //const AVCodec* codec = NULL;

    //do {
    //    //=========================== 创建AVFormatContext结构体 ===============================//
    //    //分配一个AVFormatContext，FFMPEG所有的操作都要通过这个AVFormatContext来进行
    //    fmtCtx = avformat_alloc_context();
    //    //==================================== 打开文件 ======================================//
    //    if ((ret = avformat_open_input(&fmtCtx, filePath, NULL, NULL)) != 0) {
    //        printf("cannot open video file\n");
    //        break;
    //    }

    //    //=================================== 获取视频流信息 ===================================//
    //    if ((ret = avformat_find_stream_info(fmtCtx, NULL)) < 0) {
    //        printf("cannot retrive video info\n");
    //        break;
    //    }

    //    //循环查找视频中包含的流信息，直到找到视频类型的流
    //    //便将其记录下来 保存到videoStreamIndex变量中
    //    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
    //        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
    //            videoStreamIndex = i;
    //            break;//找到视频流就退出
    //        }
    //    }

    //    //如果videoStream为-1 说明没有找到视频流
    //    if (videoStreamIndex == -1) {
    //        printf("cannot find video stream\n");
    //        break;
    //    }

    //    //打印输入和输出信息：长度 比特率 流格式等
    //    av_dump_format(fmtCtx, 0, filePath, 0);

    //    //=================================  查找解码器 ===================================//
    //    avCodecPara = fmtCtx->streams[videoStreamIndex]->codecpar;
    //    codec = avcodec_find_decoder(avCodecPara->codec_id);
    //    if (codec == NULL) {
    //        printf("cannot find decoder\n");
    //        break;
    //    }
    //    //根据解码器参数来创建解码器内容
    //    codecCtx = avcodec_alloc_context3(codec);
    //    avcodec_parameters_to_context(codecCtx, avCodecPara);
    //    if (codecCtx == NULL) {
    //        printf("Cannot alloc context.");
    //        break;
    //    }

    //    //================================  打开解码器 ===================================//
    //    if ((ret = avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
    //        printf("cannot open decoder\n");
    //        break;
    //    }

    //    //=========================== 分配AVPacket结构体 ===============================//
    //    int       i = 0;
    //    pkt = av_packet_alloc();                  
    //    av_new_packet(pkt, codecCtx->width * codecCtx->height); 

    //    while (av_read_frame(fmtCtx, pkt) >= 0) { 
    //        if (pkt->stream_index == videoStreamIndex) {
    //            i++;
    //        }
    //        av_packet_unref(pkt);
    //    }
    //    printf("There are %d frames int total.\n", i);
    //} while (0);
    ////===========================释放所有指针===============================//
    //av_packet_free(&pkt);
    ////avcodec_close(codecCtx);
    //avformat_close_input(&fmtCtx);
    //avformat_free_context(fmtCtx);
//
//
//    char filePath[]       = "D:\\cte.mp4";//文件地址
//    int  videoStreamIndex = -1;//视频流所在流序列中的索引
//    int ret=0;//默认返回值
//
//    //需要的变量名并初始化
//    AVFormatContext *fmtCtx=NULL;
//    AVPacket *pkt =NULL;
//    AVCodecContext *codecCtx=NULL;
//    AVCodecParameters *avCodecPara=NULL;
//    const AVCodec *codec=NULL;
//    AVFrame *yuvFrame = av_frame_alloc();
//    AVFrame *rgbFrame = av_frame_alloc();
//
//    do{
//        //=========================== 创建AVFormatContext结构体 ===============================//
//        //分配一个AVFormatContext，FFMPEG所有的操作都要通过这个AVFormatContext来进行
//        fmtCtx = avformat_alloc_context();
//        //==================================== 打开文件 ======================================//
//        if ((ret=avformat_open_input(&fmtCtx, filePath, NULL, NULL)) != 0) {
//            printf("cannot open video file\n");
//            break;
//        }
//
//        //=================================== 获取视频流信息 ===================================//
//        if ((ret=avformat_find_stream_info(fmtCtx, NULL)) < 0) {
//            printf("cannot retrive video info\n");
//            break;
//        }
//
//        //循环查找视频中包含的流信息，直到找到视频类型的流
//        //便将其记录下来 保存到videoStreamIndex变量中
//        for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
//            if (fmtCtx->streams[ i ]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//                videoStreamIndex = i;
//                break;//找到视频流就退出
//            }
//        }
//
//        //如果videoStream为-1 说明没有找到视频流
//        if (videoStreamIndex == -1) {
//            printf("cannot find video stream\n");
//            break;
//        }
//
//        //打印输入和输出信息：长度 比特率 流格式等
//        av_dump_format(fmtCtx, 0, filePath, 0);
//
//        //=================================  查找解码器 ===================================//
//        avCodecPara = fmtCtx->streams[ videoStreamIndex ]->codecpar;
//        codec       = avcodec_find_decoder(avCodecPara->codec_id);
//        if (codec == NULL) {
//            printf("cannot find decoder\n");
//            break;
//        }
//        //根据解码器参数来创建解码器内容
//        codecCtx = avcodec_alloc_context3(codec);
//        avcodec_parameters_to_context(codecCtx, avCodecPara);
//        if (codecCtx == NULL) {
//            printf("Cannot alloc context.");
//            break;
//        }
//
//        //================================  打开解码器 ===================================//
//        if ((ret=avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
//            printf("cannot open decoder\n");
//            break;
//        }
//
//        //================================ 设置数据转换参数 ================================//
//        struct SwsContext *img_ctx = sws_getContext(
//            codecCtx->width, codecCtx->height, codecCtx->pix_fmt, //源地址长宽以及数据格式
//            codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB32,  //目的地址长宽以及数据格式
//            SWS_BICUBIC, NULL, NULL, NULL);                       //算法类型  AV_PIX_FMT_YUVJ420P   AV_PIX_FMT_BGR24
//
//        //==================================== 分配空间 ==================================//
//        //一帧图像数据大小
//        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, codecCtx->width, codecCtx->height, 1);
//        unsigned char *out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));
//
//
//        //=========================== 分配AVPacket结构体 ===============================//
//        int       i   = 0;//用于帧计数
//        pkt = av_packet_alloc();                      //分配一个packet
//        av_new_packet(pkt, codecCtx->width * codecCtx->height); //调整packet的数据
//
//        //会将pFrameRGB的数据按RGB格式自动"关联"到buffer  即pFrameRGB中的数据改变了
//        //out_buffer中的数据也会相应的改变
//        av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, out_buffer, AV_PIX_FMT_RGB32,
//                             codecCtx->width, codecCtx->height, 1);
//
//        //===========================  读取视频信息 ===============================//
//        while (av_read_frame(fmtCtx, pkt) >= 0) { //读取的是一帧视频  数据存入一个AVPacket的结构中
//            if (pkt->stream_index == videoStreamIndex){
//                if (avcodec_send_packet(codecCtx, pkt) == 0){
//                    while (avcodec_receive_frame(codecCtx, yuvFrame) == 0){
//                        if (++i <= 500 && i >= 455){
//                            sws_scale(img_ctx,
//                                      (const uint8_t* const*)yuvFrame->data,
//                                      yuvFrame->linesize,
//                                      0,
//                                      codecCtx->height,
//                                      rgbFrame->data,
//                                      rgbFrame->linesize);
//                            saveFrame(rgbFrame, codecCtx->width, codecCtx->height, i);
//                        }
//                    }
//                }
//            }
//            av_packet_unref(pkt);//重置pkt的内容
//        }
//        printf("There are %d frames int total.\n", i);
//    }while(0);
//    //===========================释放所有指针===============================//
//    av_packet_free(&pkt);
//    //avcodec_close(codecCtx);
//    avformat_close_input(&fmtCtx);
//    avformat_free_context(fmtCtx);
//    av_frame_free(&yuvFrame);
//    av_frame_free(&rgbFrame);
//
//    return 0;
//}

//
//    char filePath[]       = "D:\\cte.mp4";//文件地址
//    int  videoStreamIndex = -1;//视频流所在流序列中的索引
//    int ret=0;//默认返回值
//
//    //需要的变量名并初始化
//    AVFormatContext *fmtCtx=NULL;
//    AVPacket *pkt =NULL;
//    AVCodecContext *codecCtx=NULL;
//    AVCodecParameters *avCodecPara=NULL;
//    const AVCodec *codec=NULL;
//    AVFrame *yuvFrame = av_frame_alloc();
//    AVFrame *rgbFrame = av_frame_alloc();
//
//    do{
//        //=========================== 创建AVFormatContext结构体 ===============================//
//        //分配一个AVFormatContext，FFMPEG所有的操作都要通过这个AVFormatContext来进行
//        fmtCtx = avformat_alloc_context();
//        //==================================== 打开文件 ======================================//
//        if ((ret=avformat_open_input(&fmtCtx, filePath, NULL, NULL)) != 0) {
//            printf("cannot open video file\n");
//            break;
//        }
//
//        //=================================== 获取视频流信息 ===================================//
//        if ((ret=avformat_find_stream_info(fmtCtx, NULL)) < 0) {
//            printf("cannot retrive video info\n");
//            break;
//        }
//
//        //循环查找视频中包含的流信息，直到找到视频类型的流
//        //便将其记录下来 保存到videoStreamIndex变量中
//        for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
//            if (fmtCtx->streams[ i ]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//                videoStreamIndex = i;
//                break;//找到视频流就退出
//            }
//        }
//
//        //如果videoStream为-1 说明没有找到视频流
//        if (videoStreamIndex == -1) {
//            printf("cannot find video stream\n");
//            break;
//        }
//
//        //打印输入和输出信息：长度 比特率 流格式等
//        av_dump_format(fmtCtx, 0, filePath, 0);
//
//        //=================================  查找解码器 ===================================//
//        avCodecPara = fmtCtx->streams[ videoStreamIndex ]->codecpar;
//        codec       = avcodec_find_decoder(avCodecPara->codec_id);
//        if (codec == NULL) {
//            printf("cannot find decoder\n");
//            break;
//        }
//        //根据解码器参数来创建解码器内容
//        codecCtx = avcodec_alloc_context3(codec);
//        avcodec_parameters_to_context(codecCtx, avCodecPara);
//        if (codecCtx == NULL) {
//            printf("Cannot alloc context.");
//            break;
//        }
//
//        //================================  打开解码器 ===================================//
//        if ((ret=avcodec_open2(codecCtx, codec, NULL)) < 0) { // 具体采用什么解码器ffmpeg经过封装 我们无须知道
//            printf("cannot open decoder\n");
//            break;
//        }
//
//        //================================ 设置数据转换参数 ================================//
//        struct SwsContext *img_ctx = sws_getContext(
//            codecCtx->width, codecCtx->height, codecCtx->pix_fmt, //源地址长宽以及数据格式
//            codecCtx->width, codecCtx->height, AV_PIX_FMT_RGB32,  //目的地址长宽以及数据格式
//            SWS_BICUBIC, NULL, NULL, NULL);                       //算法类型  AV_PIX_FMT_YUVJ420P   AV_PIX_FMT_BGR24
//
//        //==================================== 分配空间 ==================================//
//        //一帧图像数据大小
//        int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, codecCtx->width, codecCtx->height, 1);
//        unsigned char *out_buffer = (unsigned char *)av_malloc(numBytes * sizeof(unsigned char));
//
//
//        //=========================== 分配AVPacket结构体 ===============================//
//        int       i   = 0;//用于帧计数
//        pkt = av_packet_alloc();                      //分配一个packet
//        av_new_packet(pkt, codecCtx->width * codecCtx->height); //调整packet的数据
//
//        //会将pFrameRGB的数据按RGB格式自动"关联"到buffer  即pFrameRGB中的数据改变了
//        //out_buffer中的数据也会相应的改变
//        av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, out_buffer, AV_PIX_FMT_RGB32,
//                             codecCtx->width, codecCtx->height, 1);
//
//        //===========================  读取视频信息 ===============================//
//        while (av_read_frame(fmtCtx, pkt) >= 0) { //读取的是一帧视频  数据存入一个AVPacket的结构中
//            if (pkt->stream_index == videoStreamIndex){
//                if (avcodec_send_packet(codecCtx, pkt) == 0){
//                    while (avcodec_receive_frame(codecCtx, yuvFrame) == 0){
//                        if (++i <= 500 && i >= 455){
//                            sws_scale(img_ctx,
//                                      (const uint8_t* const*)yuvFrame->data,
//                                      yuvFrame->linesize,
//                                      0,
//                                      codecCtx->height,
//                                      rgbFrame->data,
//                                      rgbFrame->linesize);
//                            saveFrame(rgbFrame, codecCtx->width, codecCtx->height, i);
//                        }
//                    }
//                }
//            }
//            av_packet_unref(pkt);//重置pkt的内容
//        }
//        printf("There are %d frames int total.\n", i);
//    }while(0);
//    //===========================释放所有指针===============================//
//    av_packet_free(&pkt);
//    //avcodec_close(codecCtx);
//    avformat_close_input(&fmtCtx);
//    avformat_free_context(fmtCtx);
//    av_frame_free(&yuvFrame);
//    av_frame_free(&rgbFrame);
//
//    return 0;
//}
