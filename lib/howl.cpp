#include "howl.h"
#include "Util.h"
#include <new>
#include <utility>
#include <cstdlib>
#include <stdio.h>
#include <deque>
#include <cstdint>
#include <unistd.h>

#include <chrono>

#define SPECTROGRAM_WIDTH 250//500
#define SPECTROGRAM_HEIGHT 128//256
#define OVERLAP_PERCENTAGE 50
#define SILENCE_THRESHOLD 10.0
#define MAX_SPECTROGRAMS 1

// #include <nonstd/ring_span.hpp>
#include <spectrogram.h>
#include <zncc.h>
#include <arrayfire.h>
#include <cstdlib>
#include <float.h>

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

af::array normalize(af::array a);

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

    ctx->_captureRender = new(std::nothrow) SpectrogramRenders; //new(std::nothrow) RENDER;

    if (!ctx->_captureRender)
    {
        return -1;
    }

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

    af::setDevice(0);
    af::info();

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
    try
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
                    // Skip if spectrograms are too far apart timewise
                    // printf("SKIP\n");
                    continue;
                }

                unsigned int stride = 0;

                unsigned char* sourceBuffer = get_spectrogram_buffer(sourceRender, &width, &height, &stride);
                unsigned char* captureBuffer = get_spectrogram_buffer(captureRender, &width, &height, &stride);

                af::array img1(width, height, u32);
                af::array img2(width, height, u32);
                // maybe
                // use stride -> lock/unlock
                // or setup cairo or replace cairo
                img1.write(sourceBuffer, height * width * 4);
                img2.write(captureBuffer, height * width * 4);

                af::array result =
                    matchTemplate(img2, img1, AF_ZSSD);

                af::array disp_norm = normalize(result);
                // prepare for peaks...
                // TODO modify peaks and remove this
                af::array disp_res = 1.0 - disp_norm;

                std::vector<float> v(disp_res.elements());
                disp_res.host(&v.front());
                vector<int> idxs;

                findPeaks(v, idxs);

                float avgPeak = 0.0;

                for (int i = 0; i < idxs.size(); ++i)
                {
                    avgPeak += v[idxs[i]];
                }

                avgPeak /= idxs.size();

                bool bMatch = true;

                if (avgPeak >= 0.75)
                {
                    bMatch = false;
                }

                if (bMatch)
                {
                    if (ctx->_preHowlCb != NULL)
                    {
                        (*ctx->_preHowlCb)();
                    }

                    fprintf(stdout, "MATCH %f - %s %s!\n", avgPeak, sourceRender->pngfilepath, captureRender->pngfilepath);
                }
                else
                {
                    // fprintf(stdout, "NOT MATCH %f - %s %s!\n", avgPeak, sourceRender->pngfilepath, captureRender->pngfilepath);
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        fprintf(stderr, "%s\n", e.what());
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

// Modified for single loop from arrayfire example
af::array normalize(af::array a) {

    std::vector<float> hostImg(a.elements());
    a.host(&hostImg.front());

    float mx = FLT_MIN, mn = FLT_MAX;

    for (int i = 0; i < hostImg.size(); ++i)
    {
        float value = hostImg[i];
        if (value > mx)
        {
            mx = value;
        }

        if (value < mn)
        {
            mn = value;
        }
    }

    return (a - mn) / (mx - mn);
}