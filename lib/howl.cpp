#include "howl.h"
#include <new>
#include <utility>
#include <cstdlib>
#include <stdio.h>
#include <deque>
#include <cstdint>
#include <unistd.h>

#define SPECTROGRAM_WIDTH 500
#define SPECTROGRAM_HEIGHT 256
#define OVERLAP_PERCENTAGE 50
#define SILENCE_THRESHOLD 10.0

// #include <nonstd/ring_span.hpp>
#include <spectrogram.h>
#include <zncc.h>

using namespace std;

using AudioRingBuffer = std::deque<double>;

struct HowlLibContext
{
    double*                 _sourceBuffer;
    double*                 _captureBuffer;
    AudioRingBuffer*        _sourceRingBuffer;
    AudioRingBuffer*        _captureRingBuffer;
    int                     _sampleRate;
    int                     _bufferMs;
    int                     _bufferSize;
    float                   _sourceSnapshotTimeoutMs;
    float                   _captureSnapshotTimeoutMs;
    fpPreHowlDetected       _preHowlCb;
    float                   _sourceSilenceMs;
    float                   _captureSilenceMs;
    RENDER*                 _sourceRender;
    RENDER*                 _captureRender;
    double                  _sourceTriggerRender;
    double                  _captureTriggerRender;
};

void copySamples(
    const float*,
    const int,
    const int,
    AudioRingBuffer&);

HowlLibContext* createHowlLibContext()
{
    auto howlLibCtx = new(nothrow) HowlLibContext;
    return howlLibCtx;
}

void destroyHowlLibContext(
    HowlLibContext* ctx
)
{
    if (!ctx)
    {
        return;
    }

    ctx->_preHowlCb = nullptr;

    if (ctx->_sourceRingBuffer)
    {
        delete ctx->_sourceRingBuffer;
    }

    if (ctx->_captureRingBuffer)
    {
        delete ctx->_captureRingBuffer;
    }

    if (ctx->_sourceBuffer)
    {
        delete [] ctx->_sourceBuffer;
    }

    if (ctx->_captureBuffer)
    {
        delete [] ctx->_captureBuffer;
    }

    deinit_spectrogram(ctx->_sourceRender);

    delete ctx->_sourceRender;

    deinit_spectrogram(ctx->_captureRender);

    delete ctx->_captureRender;

    delete ctx;
}

int initHowlLibContext(
    HowlLibContext* ctx, // HowlLib
    int sampleRate, // SampleRate
    int bufferMs, // Buffer ms
    fpPreHowlDetected howlPreDetectCallback
)
{
    if (!ctx)
    {
        return -1;
    }

    ctx->_bufferSize = bufferMs * sampleRate / 1000;

    ctx->_sourceBuffer = new(std::nothrow) double[ctx->_bufferSize];
    ctx->_captureBuffer = new(std::nothrow) double[ctx->_bufferSize];

    if (!ctx->_sourceBuffer || !ctx->_captureBuffer)
    {
        return -1;
    }

    ctx->_sourceRender = new(std::nothrow) RENDER;

    if (!ctx->_sourceRender)
    {
        return -1;
    }

    if (init_spectrogram(ctx->_sourceRender) != 0)
    {
        return -1;
    }

    ctx->_captureRender = new(std::nothrow) RENDER;

    if (!ctx->_captureRender)
    {
        return -1;
    }

    if (init_spectrogram(ctx->_captureRender) != 0)
    {
        return -1;
    }

    // ctx->_sourceRingBuffer
    //     = new(std::nothrow) AudioRingBuffer(ctx->_sourceBuffer,
    //                                         ctx->_sourceBuffer + ctx->_bufferSize,
    //                                         ctx->_sourceBuffer,
    //                                         0);

    // ctx->_captureRingBuffer
    //     = new(std::nothrow) AudioRingBuffer(ctx->_captureBuffer,
    //                                         ctx->_captureBuffer + ctx->_bufferSize,
    //                                         ctx->_captureBuffer,
    //                                         0);

    ctx->_sourceRingBuffer = new(std::nothrow) deque<double>();
    ctx->_captureRingBuffer = new(std::nothrow) deque<double>();

    if (!ctx->_sourceRingBuffer || !ctx->_captureRingBuffer)
    {
        return -1;
    }

    ctx->_sampleRate = sampleRate;
    ctx->_bufferMs = bufferMs;
    ctx->_preHowlCb = howlPreDetectCallback;
    ctx->_sourceTriggerRender = pow(10, (SILENCE_THRESHOLD / 20.0) );
    ctx->_captureTriggerRender = pow(10, (SILENCE_THRESHOLD / 20.0) );

    return 0;
}

int feedSourceAudio(
    HowlLibContext* ctx,
    float* samples,
    int samplesSize
)
{
    copySamples(samples,
                samplesSize,
                ctx->_bufferSize,
                *ctx->_sourceRingBuffer);

    if (ctx->_sourceRingBuffer->size() ==
        ctx->_bufferSize)
    {

        float msAdded = (float)samplesSize / ((float)ctx->_sampleRate / 1000);

        ctx->_sourceSnapshotTimeoutMs += msAdded;

        //fprintf(stdout, "%f added %f total\n", msAdded, ctx->_sourceSnapshotTimeoutMs);

        if (ctx->_sourceSnapshotTimeoutMs >  ((OVERLAP_PERCENTAGE / 100) * ctx->_bufferMs) )
        {
            // fprintf(stdout, "%f milliseconds have passed\n", ctx->_sourceSnapshotTimeoutMs);

            for (int i = 0; i < ctx->_bufferSize; ++i) {
                ctx->_sourceBuffer[i] = ctx->_sourceRingBuffer->at(i);
            }

                static int count1 = 0;

                static char path1[256];

                memset(path1, 0, sizeof(path1));

                sprintf(path1, "./source_%d.png", count1);

                count1++;

                ctx->_sourceRender->pngfilepath = path1;

            // printf("source!\n");

            unsigned char* bitmapData = nullptr;
            // // get spectrogram
            if (0 != render_spectrogram_bitmap(
                ctx->_sourceBuffer,
                ctx->_bufferSize,
                ctx->_sampleRate,
                &bitmapData,
                SPECTROGRAM_WIDTH,
                SPECTROGRAM_HEIGHT,
                ctx->_sourceRender,
                ctx->_sourceTriggerRender
            ))
            {
                //
                return -1;
            }

            if (bitmapData != NULL)
            {
                //fprintf(stdout, "got bitmapdata\n");

                unsigned char* bitmapData1 = new(std::nothrow) unsigned char[SPECTROGRAM_WIDTH * SPECTROGRAM_HEIGHT * sizeof(int32_t)];

                memcpy(bitmapData1, bitmapData, SPECTROGRAM_WIDTH * SPECTROGRAM_HEIGHT * sizeof(int32_t));

                // std::vector<BYTE> imgOut;
                // unsigned int widthOut, heightOut;

                // zncc_gpu(bitmapData,
                //         bitmapData1,
                //         SPECTROGRAM_WIDTH,
                //         SPECTROGRAM_HEIGHT,
                //         imgOut,
                //         widthOut,
                //         heightOut);

                // // lodepng::encode(	"outputs/depthmap.png"	, temp, result_w, result_h, LCT_GREY, 8U);

                // static int dmapCount = 0;

                // if (imgOut.size())
                // {

                //     char path[256];

                //     memset(path, 0, sizeof(path));

                //     sprintf(path, "./dispmap_%d.png", dmapCount);

                //     lodepng::encode( path, imgOut.data(), widthOut, heightOut, LCT_GREY, 8U);

                //     dmapCount++;
                // }

                delete [] bitmapData1;
            }

            ctx->_sourceSnapshotTimeoutMs = 0;
        }
    }

    return 0;
}

int feedCaptureAudio(
    HowlLibContext* ctx,
    float* samples,
    int samplesSize
)
{

    copySamples(samples,
                samplesSize,
                ctx->_bufferSize,
                *ctx->_captureRingBuffer);

    if (ctx->_captureRingBuffer->size() ==
        ctx->_bufferSize)
    {

        float msAdded = (float)samplesSize / ((float)ctx->_sampleRate / 1000);

        ctx->_captureSnapshotTimeoutMs += msAdded;

        //fprintf(stdout, "%f added %f total\n", msAdded, ctx->_sourceSnapshotTimeoutMs);

        if (ctx->_captureSnapshotTimeoutMs >  ((OVERLAP_PERCENTAGE / 100) * ctx->_bufferMs) )
        {
            // fprintf(stdout, "%f milliseconds have passed\n", ctx->_sourceSnapshotTimeoutMs);

            for (int i = 0; i < ctx->_bufferSize; ++i) {
                ctx->_captureBuffer[i] = ctx->_captureRingBuffer->at(i);
            }

                static int count = 0;

                static char path[256];

                memset(path, 0, sizeof(path));

                sprintf(path, "./capture_%d.png", count);

                count++;

                ctx->_captureRender->pngfilepath = path;

            // printf("capture\n");

            unsigned char* bitmapData = nullptr;
            // get spectrogram
            if (0 != render_spectrogram_bitmap(
                ctx->_captureBuffer,
                ctx->_bufferSize,
                ctx->_sampleRate,
                &bitmapData,
                SPECTROGRAM_WIDTH,
                SPECTROGRAM_HEIGHT,
                ctx->_captureRender,
                ctx->_captureTriggerRender
            ))
            {
                //
                return -1;
            }

            if (bitmapData != NULL)
            {
                //fprintf(stdout, "got bitmapdata\n");

                unsigned char* bitmapData1 = new(std::nothrow) unsigned char[SPECTROGRAM_WIDTH * SPECTROGRAM_HEIGHT * sizeof(int32_t)];

                memcpy(bitmapData1, bitmapData, SPECTROGRAM_WIDTH * SPECTROGRAM_HEIGHT * sizeof(int32_t));

                // std::vector<BYTE> imgOut;
                // unsigned int widthOut, heightOut;

                // zncc_gpu(bitmapData,
                //         bitmapData1,
                //         SPECTROGRAM_WIDTH,
                //         SPECTROGRAM_HEIGHT,
                //         imgOut,
                //         widthOut,
                //         heightOut);

                // // lodepng::encode(	"outputs/depthmap.png"	, temp, result_w, result_h, LCT_GREY, 8U);

                // static int dmapCount = 0;

                // if (imgOut.size())
                // {

                //     char path[256];

                //     memset(path, 0, sizeof(path));

                //     sprintf(path, "./dispmap_%d.png", dmapCount);

                //     lodepng::encode( path, imgOut.data(), widthOut, heightOut, LCT_GREY, 8U);

                //     dmapCount++;
                // }

                delete [] bitmapData1;
            }

            ctx->_captureSnapshotTimeoutMs = 0;
        }
    }

    return 0;
}

void copySamples(
    const float* samples,
    const int samplesSize,
    const int maxBufferSize,
    AudioRingBuffer& buffer)
{

    for (int i = 0; i < samplesSize; ++i)
    {
        if (buffer.size() >= maxBufferSize)
        {
            buffer.pop_front();
        }

        buffer.push_back(samples[i]);
    }
}