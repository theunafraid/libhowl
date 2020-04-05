#include "howl.h"
#include <new>
#include <utility>
#include <cstdlib>
#include <stdio.h>
#include <deque>
#include <unistd.h>

#define SPECTROGRAM_WIDTH 500
#define SPECTROGRAM_HEIGHT 256
#define OVERLAP_PERCENTAGE 50
#define SILENCE_SIGNAL_THRESHOLD 0.005
#define SILENCE_THRESHOLD 500

// #include <nonstd/ring_span.hpp>
#include <spectrogram.h>
#include <zncc.h>

using namespace std;

using AudioRingBuffer = std::deque<double>;//nonstd::ring_span<double>;

struct HowlLibContext
{
    double*                 _sourceBuffer;
    double*                 _captureBuffer;
    AudioRingBuffer*        _sourceRingBuffer;
    AudioRingBuffer*        _captureRingBuffer;
    int                     _sampleRate;
    int                     _bufferMs;
    int                     _bufferSize;
    // int                     _silenceSize;
    float                   _sourceSnapshotTimeoutMs;
    float                   _captureSnapshotTimeoutMs;
    fpPreHowlDetected       _preHowlCb;
    float                   _silenceMs;
    RENDER*                 _sourceRender;
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

    delete ctx->_sourceRender;

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

    // ctx->_silenceSize = ctx->_bufferSize / 4;
    // ctx->_bufferSize -= ctx->_silenceSize;

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

    return 0;
}

int feedSourceAudio(
    HowlLibContext* ctx,
    float* samples,
    int samplesSize
)
{
    int silenceCount = 0;

    for (int i = 0; i < samplesSize; ++i)
    {
        if (samples[i] <= SILENCE_SIGNAL_THRESHOLD)
        {
            silenceCount++;
        }
    }

    float p = ((float) silenceCount / (float) samplesSize) * 100;

    silenceCount = 0;

    if (p >= 98)
    {
        float msAdded = (float)samplesSize / ((float)ctx->_sampleRate / 1000);
        ctx->_silenceMs += msAdded;
    }
    else
    {
        ctx->_silenceMs = 0;
    }

    if (ctx->_silenceMs >= SILENCE_THRESHOLD)
    {
        return 0;
    }

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

            for (int i = 0; i < ctx->_bufferSize/* - ctx->_silenceSize*/; ++i) {
                ctx->_sourceBuffer[i/* + ctx->_silenceSize*/] = ctx->_sourceRingBuffer->at(i);
            }

            unsigned char* bitmapData = nullptr;
            // get spectrogram
            if (0 != render_spectrogram_bitmap(
                ctx->_sourceBuffer,
                ctx->_bufferSize,
                ctx->_sampleRate,
                &bitmapData,
                SPECTROGRAM_WIDTH,
                SPECTROGRAM_HEIGHT,
                ctx->_sourceRender
            ))
            {
                //
                return -1;
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

    return 0;
    // Check buffer size to shift
    copySamples(samples,
                samplesSize,
                ctx->_bufferSize,
                *ctx->_captureRingBuffer);

    // unsigned char* bitmapData = nullptr;
    // // get spectrogram
    // if (0 != render_spectrogram_bitmap(
    //     ctx->_captureBuffer,
    //     ctx->_captureRingBuffer->size(),
    //     ctx->_sampleRate,
    //     &bitmapData,
    //     SPECTROGRAM_WIDTH,
    //     SPECTROGRAM_HEIGHT
    // ))
    // {
    //     //
    //     return -1;
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