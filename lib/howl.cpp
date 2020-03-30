#include "howl.h"
#include <new>
#include <utility>
#include <cstdlib>
#include <stdio.h>
#include <deque>

#define SPECTROGRAM_WIDTH 1000
#define SPECTROGRAM_HEIGHT 500

#include <nonstd/ring_span.hpp>
#include <spectrogram.h>

using namespace std;

using AudioRingBuffer = nonstd::ring_span<double>;

struct HowlLibContext
{
    double*                 _sourceBuffer;
    double*                 _captureBuffer;
    AudioRingBuffer*        _sourceRingBuffer;
    AudioRingBuffer*        _captureRingBuffer;
    int                     _sampleRate;
    int                     _bufferMs;
    int                     _bufferSize;
    fpPreHowlDetected       _preHowlCb;
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

    ctx->_sourceRingBuffer
        = new(std::nothrow) AudioRingBuffer(ctx->_sourceBuffer,
                                            ctx->_sourceBuffer + ctx->_bufferSize,
                                            ctx->_sourceBuffer,
                                            0);

    ctx->_captureRingBuffer
        = new(std::nothrow) AudioRingBuffer(ctx->_captureBuffer,
                                            ctx->_captureBuffer + ctx->_bufferSize,
                                            ctx->_captureBuffer,
                                            0);

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
    // Check buffer size to shift
    copySamples(samples,
                samplesSize,
                ctx->_bufferSize,
                *ctx->_sourceRingBuffer);

    unsigned char* bitmapData = nullptr;
    // get spectrogram
    if (0 != render_spectrogram_bitmap(
        ctx->_sourceBuffer,
        ctx->_sourceRingBuffer->size(),
        &bitmapData,
        SPECTROGRAM_WIDTH,
        SPECTROGRAM_HEIGHT
    ))
    {

        //
        return -1;
    }

    return 0;
}

int feedCaptureAudio(
    HowlLibContext* ctx,
    float* samples,
    int samplesSize
)
{
    // Check buffer size to shift
    copySamples(samples,
                samplesSize,
                ctx->_bufferSize,
                *ctx->_captureRingBuffer);

    unsigned char* bitmapData = nullptr;
    // get spectrogram
    if (0 != render_spectrogram_bitmap(
        ctx->_captureBuffer,
        ctx->_captureRingBuffer->size(),
        &bitmapData,
        SPECTROGRAM_WIDTH,
        SPECTROGRAM_HEIGHT
    ))
    {
        //
        return -1;
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