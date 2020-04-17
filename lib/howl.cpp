#include "howl.h"
#include <new>
#include <utility>
#include <cstdlib>
#include <stdio.h>
#include <deque>
#include <cstdint>
#include <unistd.h>

#include <chrono>

#define SPECTROGRAM_WIDTH 500
#define SPECTROGRAM_HEIGHT 256
#define OVERLAP_PERCENTAGE 50
#define SILENCE_THRESHOLD 10.0
#define MAX_SPECTROGRAMS 1

// #include <nonstd/ring_span.hpp>
#include <spectrogram.h>
#include <zncc.h>

using namespace std;

using AudioRingBuffer = std::deque<double>;
using SpectrogramRenders = std::deque<RENDER*>;

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
    // RENDER*                 _sourceRender;
    // RENDER*                 _captureRender;
    double                  _sourceTriggerRender;
    double                  _captureTriggerRender;
    SpectrogramRenders*     _sourceRender;
    SpectrogramRenders*     _captureRender;
};

void copySamples(
    const float*,
    const int,
    const int,
    AudioRingBuffer&);

void setRenderTimestamp(RENDER* render);

RENDER* createNewRender();

void destroyRender(RENDER* render);

void addRender(RENDER* render, SpectrogramRenders* spectrograms);

void checkAllRenders(HowlLibContext* ctx);

static const char* getNextSourceRenderPath();

static const char* getNextCaptureRenderPath();

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

    if (ctx->_sourceRender)
    {
        for (auto r = ctx->_sourceRender->begin(); r != ctx->_sourceRender->end(); r++)
        {
            destroyRender(*r);
        }

        delete ctx->_sourceRender;
    }

    if (ctx->_captureRender)
    {
        for (auto r = ctx->_captureRender->begin(); r != ctx->_captureRender->end(); r++)
        {
            destroyRender(*r);
        }

        delete ctx->_captureRender;
    }

    // deinit_spectrogram(ctx->_sourceRender);

    // delete ctx->_sourceRender;

    // deinit_spectrogram(ctx->_captureRender);

    // delete ctx->_captureRender;

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

    ctx->_sourceRender = new(std::nothrow) SpectrogramRenders; //new(std::nothrow) RENDER;

    if (!ctx->_sourceRender)
    {
        return -1;
    }

    // if (init_spectrogram(ctx->_sourceRender) != 0)
    // {
    //     return -1;
    // }

    ctx->_captureRender = new(std::nothrow) SpectrogramRenders; //new(std::nothrow) RENDER;

    if (!ctx->_captureRender)
    {
        return -1;
    }

    // if (init_spectrogram(ctx->_captureRender) != 0)
    // {
    //     return -1;
    // }

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

            RENDER* sourceRender = createNewRender();

            setRenderTimestamp(sourceRender);

            sourceRender->pngfilepath = getNextSourceRenderPath();

            unsigned char* bitmapData = nullptr;
            // // get spectrogram

            int ret = render_spectrogram_bitmap(
                ctx->_sourceBuffer,
                ctx->_bufferSize,
                ctx->_sampleRate,
                &bitmapData,
                SPECTROGRAM_WIDTH,
                SPECTROGRAM_HEIGHT,
                sourceRender,
                ctx->_sourceTriggerRender
            );

            if (ret == 0)
            {
                addRender(sourceRender, ctx->_sourceRender);

                checkAllRenders(ctx);
            }
            else
            {
                destroyRender(sourceRender);
            }

            ctx->_sourceSnapshotTimeoutMs = 0;
        }
    }

    // if (ctx->_sourceSnapshotTimeoutMs > 1500)
    // {
    //     printf("SOURCE NOT ADDED!\n");
    // }

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

            RENDER* captureRender = createNewRender();

            setRenderTimestamp(captureRender);

            captureRender->pngfilepath = getNextCaptureRenderPath();

            unsigned char* bitmapData = nullptr;
            // get spectrogram

            int ret = render_spectrogram_bitmap(
                ctx->_captureBuffer,
                ctx->_bufferSize,
                ctx->_sampleRate,
                &bitmapData,
                SPECTROGRAM_WIDTH,
                SPECTROGRAM_HEIGHT,
                captureRender,
                ctx->_captureTriggerRender
            );

            if (ret == 0)
            {

                addRender(captureRender, ctx->_captureRender);

                checkAllRenders(ctx);

            }
            else
            {
                destroyRender(captureRender);                
            }

            ctx->_captureSnapshotTimeoutMs = 0;
        }
    }

    // if (ctx->_captureSnapshotTimeoutMs > 1500)
    // {
    //     printf("CAPTURE NOT ADDED!\n");
    // }

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

void setRenderTimestamp(RENDER* render)
{
    unsigned long milliseconds_since_epoch = 
        std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::system_clock::now().time_since_epoch()).count();

    render->time_stamp = milliseconds_since_epoch;
}

RENDER* createNewRender()
{
    RENDER* newRender = new (std::nothrow) RENDER;

    if (0 != init_spectrogram(newRender))
    {
        delete newRender;
        return NULL;
    }

    return newRender;
}

void destroyRender(RENDER* render)
{
    deinit_spectrogram(render);

    if (render->pngfilepath)
    {
        delete [] render->pngfilepath;
    }

    delete render;
}

void addRender(RENDER* render, SpectrogramRenders* spectrograms)
{
    if (spectrograms->size() >= MAX_SPECTROGRAMS)
    {
        RENDER* _render = spectrograms->front();

        destroyRender(_render);

        spectrograms->pop_front();
    }

    spectrograms->push_back(render);
}

void checkAllRenders(HowlLibContext* ctx)
{

    unsigned int width = 0, height = 0;

    // Check capture spectrograms against source spectrograms
    for (int i = 0; i < ctx->_captureRender->size(); ++i)
    {

        RENDER* captureRender = ctx->_captureRender->at(i);

        for (int j = 0; j < ctx->_sourceRender->size(); ++j)
        {

            RENDER* sourceRender = ctx->_sourceRender->at(j);

            long passed = captureRender->time_stamp - sourceRender->time_stamp > 0 ?
                            captureRender->time_stamp - sourceRender->time_stamp :
                            sourceRender->time_stamp - captureRender->time_stamp;

            if (passed >= ctx->_bufferMs)
            {
                printf("SKIP\n");
                continue;
            }

            unsigned char* sourceBuffer = get_spectrogram_buffer(sourceRender, &width, &height);
            unsigned char* captureBuffer = get_spectrogram_buffer(captureRender, &width, &height);

            std::vector<BYTE> imgOut;
            unsigned int widthOut, heightOut;

            // zncc_gpu(captureBuffer,
            //         sourceBuffer,
            //         SPECTROGRAM_WIDTH,
            //         SPECTROGRAM_HEIGHT,
            //         imgOut,
            //         widthOut,
            //         heightOut);
                    // 256);

            // printf("\n");
            // int pxcount = 0;
            // for (int x = 0; x < imgOut.size() / sizeof(int32_t); x += sizeof(int32_t))
            // {

            //     unsigned char* p = (unsigned char*)&imgOut[x];
            //     unsigned int r = (unsigned int)p[2];
            //     unsigned int g = (unsigned int)p[1];
            //     unsigned int b = (unsigned int)p[0];

            //     // printf("%d %d %d | ",r,g,b);

            //     if (r >= 28 &&
            //         g >= 28 &&
            //         b >= 28)
            //     {
            //         pxcount++;
            //         // printf("\rNO echo!!");
            //     }
            // }

            // float sc = ((float)pxcount / (float)(imgOut.size() / sizeof(int32_t))) * 100.0;

            // printf("%f\n", sc);

            // printf("\n");
            // if (imgOut.size())
            // {

            //     static int c = 0;

            //     static char path[64];

            //     memset(path, 0, sizeof(path));

            //     sprintf(path, "./disp_%d.png", c);

            //     lodepng::encode( path, imgOut.data(), widthOut, heightOut, LCT_GREY, 8U);
            //     // printf("render %s %s %s: ...\n", captureRender->pngfilepath, sourceRender->pngfilepath, path);

            //     c++;
            // }
        }
    }
}

static const char* getNextSourceRenderPath()
{
    static int count1 = 0;

    char* path1 = new(std::nothrow) char[64];

    memset(path1, 0, sizeof(char) * 64);

    sprintf(path1, "./source_%d.png", count1);

    count1++;

    return path1;
}

static const char* getNextCaptureRenderPath()
{
    static int count = 0;

    char* path = new(std::nothrow) char[64];

    memset(path, 0,  sizeof(char) * 64);

    sprintf(path, "./capture_%d.png", count);

    count++;

    return path;
}