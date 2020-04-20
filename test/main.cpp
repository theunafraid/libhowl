#include <unistd.h>
#include <sys/select.h>
#include <stdlib.h>
#include <signal.h>

#include <stdio.h>
#include <sstream>
#include <thread>
#include <atomic>

// #include <nonstd/ring_span.hpp>
#include <howl.h>
#include <deque>
#include <soundio/soundio.h>
// #include <chrono>

#define SAMPLES_SIZE 4096
#define SAMPLE_RATE 44100
#define AUDIO_FORMAT SoundIoFormatFloat32NE

static std::atomic_bool quit;

struct RecordContext {
    struct SoundIoRingBuffer *ring_buffer;
};

int initAudio(struct SoundIo**);
int closeAudio(struct SoundIo*,
                struct SoundIoDevice*,
                struct SoundIoDevice*);

bool promptAudioDevices(struct SoundIo*,
                        struct SoundIoDevice**,
                        struct SoundIoDevice**);

typedef void(*soundioreadcb)(struct SoundIoInStream*, int, int);
typedef void(*soundiooverflowcb)(struct SoundIoInStream*);

int startInDevice(struct SoundIo*,
                    struct SoundIoDevice*,
                    int sampleRate,
                    enum SoundIoFormat,
                    void* userdata,
                    soundioreadcb,
                    soundiooverflowcb,
                    struct SoundIoInStream**);

// audio callbacks
void sourceCallback( void* userdata, unsigned char* stream, int len );
void captureCallback( void* userdata, unsigned char* stream, int len );

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max);
static void overflow_callback(struct SoundIoInStream *instream);

//
static void preHowlDetected()
{
    fprintf(stdout, "Similar audio detected!\n");
}

void quitHandler(int dummy)
{
    // fprintf(stdout, "QUIT!\n");
    quit = true;
}

int main(int argc, const char** argv)
{

    std::thread sourceFeedThread;
    std::thread captureFeedThread;

    int sourceFds[2];
    int captureFds[2];

    HowlLibContext* howlLib = NULL;

    struct SoundIoInStream *sourceInStream = NULL,
                                *captureInStream = NULL;

    struct SoundIoDevice *sourceDevice = NULL,
                            *captureDevice = NULL;

    struct RecordContext rcSource, rcCapture;

    struct SoundIo *soundio = NULL;

    enum SoundIoFormat fmt = AUDIO_FORMAT;

    signal (SIGINT, quitHandler);

    if (initAudio(&soundio) != 0)
    {
        fprintf(stderr, "Failed to initialize SDL!\n");
        return -1;
    }

    if (!promptAudioDevices(soundio,
                            &sourceDevice,
                            &captureDevice))
    {
        fprintf(stderr, "No devices selected!\n");
        return -1;
    }

    howlLib = createHowlLibContext();

    if (!howlLib)
    {
        fprintf(stderr, "Failed to initialize libhowl!\n");
        return -1;
    }

    if (0 != initHowlLibContext(
        howlLib,
        44100,
        3000,
        preHowlDetected))
    {
        fprintf(stderr, "Failed to initialize libhowl!\n");
        return -1;
    }

    soundio_device_sort_channel_layouts(sourceDevice);

    soundio_device_sort_channel_layouts(captureDevice);

    if (0 != startInDevice(soundio,
                            sourceDevice,
                            SAMPLE_RATE,
                            AUDIO_FORMAT,
                            &rcSource,
                            read_callback,
                            overflow_callback,
                            &sourceInStream))
    {
        fprintf(stderr, "Failed to start source\n");
        return -1;
    }

    if (0 != startInDevice(soundio,
                            captureDevice,
                            SAMPLE_RATE,
                            AUDIO_FORMAT,
                            &rcCapture,
                            read_callback,
                            overflow_callback,
                            &captureInStream))
    {
        fprintf(stderr, "Failed to start source\n");
        return -1;
    }

    fd_set stdset;
    FD_ZERO(&stdset);
    FD_SET(fileno(stdin), &stdset);

    struct timeval tv;
    memset(&tv, 0, sizeof(timeval));
    tv.tv_sec = 0;
    tv.tv_usec = 92000;

    FILE *out_f = fopen("captureraw", "wb");
    if (!out_f) {
        fprintf(stderr, "unable to open : %s\n", strerror(errno));
        return 1;
    }

    const char* outfile1 = "sourceraw";
    
    FILE *out_f1 = fopen(outfile1, "wb");
    if (!out_f1) {
        fprintf(stderr, "unable to opens: %s\n", strerror(errno));
        return 1;
    }

    for(;!quit;)
    {
        if (select(1, &stdset, NULL, NULL, &tv) < 0)
        {
            fprintf(stdout, "Failed to check input\n");
            exit(1);
        }

        if (FD_ISSET(fileno(stdin), &stdset))
        {
            // replace read
            int c = fgetc(stdin);

            if (c == 'e')
            {
                quit = true;
            }
        }

        FD_ZERO(&stdset);
        FD_SET(fileno(stdin), &stdset);

        soundio_flush_events(soundio);

        {
            int fill_bytes = soundio_ring_buffer_fill_count(rcSource.ring_buffer);
            char *read_buf = soundio_ring_buffer_read_ptr(rcSource.ring_buffer);

            // fprintf(stdout, "%d\n", fill_bytes);

            if (0 != feedSourceAudio(howlLib, (float*)read_buf, fill_bytes / sizeof(float)))
            {
                fprintf(stderr, "Failed to feed source audio...\n");
            }

            // size_t amt = fwrite(read_buf, 1, fill_bytes, out_f1);
            // if ((int)amt != fill_bytes) {
            //     fprintf(stderr, "write error: %s\n", strerror(errno));
            //     return 1;
            // }
            // fprintf(stdout, "source %d\n", fill_bytes);
            soundio_ring_buffer_advance_read_ptr(rcSource.ring_buffer, fill_bytes);
        }

        {
            int fill_bytes = soundio_ring_buffer_fill_count(rcCapture.ring_buffer);
            char *read_buf = soundio_ring_buffer_read_ptr(rcCapture.ring_buffer);

            if (0 != feedCaptureAudio(howlLib, (float*) read_buf, fill_bytes / sizeof(float)))
            {
                fprintf(stderr, "Failed to feed capture audio...\n");
            }

            // size_t amt = fwrite(read_buf, 1, fill_bytes, out_f);
            // if ((int)amt != fill_bytes) {
            //     fprintf(stderr, "write error: %s\n", strerror(errno));
            //     return 1;
            // }
            // fprintf(stdout, "capture %d\n", fill_bytes);
            soundio_ring_buffer_advance_read_ptr(rcCapture.ring_buffer, fill_bytes);
        }
    }

    fprintf(stdout, "exit...\n");

    soundio_instream_destroy(sourceInStream);
    soundio_instream_destroy(captureInStream);

    closeAudio(soundio,
                sourceDevice,
                captureDevice);

    // sourceFeedThread.join();

    // captureFeedThread.join();

    destroyHowlLibContext(howlLib);

    return 0;
}

int initAudio(struct SoundIo** soundio)
{
    *soundio = soundio_create();
    if (!*soundio) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    int err = soundio_connect(*soundio);

    if (err) {
        fprintf(stderr, "%s\n", soundio_strerror(err));
        return err;
    }

    return 0;
}

int closeAudio(struct SoundIo* soundio,
                struct SoundIoDevice* sourceDevice,
                struct SoundIoDevice* captureDevice)
{
    soundio_device_unref(sourceDevice);
    soundio_device_unref(captureDevice);
    soundio_destroy(soundio);
    return 0;
}

int startInDevice(struct SoundIo* soundio,
                    struct SoundIoDevice* device,
                    int sampleRate,
                    enum SoundIoFormat fmt,
                    void* userdata,
                    soundioreadcb readcb,
                    soundiooverflowcb overflowcb,
                    struct SoundIoInStream** _instream)
{

    RecordContext* rc = (RecordContext*)userdata;

    int err = 0;

    *_instream = soundio_instream_create(device);
    struct SoundIoInStream *instream = *_instream;
    if (!instream) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    
    instream->layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
    instream->format = fmt;
    instream->sample_rate = sampleRate;
    instream->read_callback = readcb;
    instream->overflow_callback = overflowcb;
    instream->userdata = userdata;

    if ((err = soundio_instream_open(instream))) {
        fprintf(stderr, "unable to open input stream: %s", soundio_strerror(err));
        return 1;
    }

    // fprintf(stderr, "%s %dHz %s interleaved\n",
    //         instream->layout.name, sampleRate, soundio_format_string(fmt));

    const int ring_buffer_duration_seconds = 30;
    int capacity = ring_buffer_duration_seconds * instream->sample_rate * instream->bytes_per_frame;
    rc->ring_buffer = soundio_ring_buffer_create(soundio, capacity);
    if (!rc->ring_buffer) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    if ((err = soundio_instream_start(instream))) {
        fprintf(stderr, "unable to start input device: %s", soundio_strerror(err));
        return 1;
    }

    return 0;
}

/**
 * Source = input / original
 * Capture = input / modified
 */
bool promptAudioDevices(struct SoundIo* soundio,
                        struct SoundIoDevice** sourceDevice,
                        struct SoundIoDevice** captureDevice)
{
    std::stringstream promptText;

    soundio_flush_events(soundio);

    int input_count = soundio_input_device_count(soundio);

    fprintf(stdout, "--------Input Devices--------\n\n");
    for (int i = 0; i < input_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_input_device(soundio, i);

        fprintf(stdout, "#%d : %s\n", i, device->name);

        soundio_device_unref(device);
    }

    int sourceDeviceIndex = 0, captureDeviceIndex = 0;
    fprintf(stdout, "Specify source device(microphone):\n");
    fscanf(stdin, "%d", &sourceDeviceIndex);
    fprintf(stdout, "Specify capture device(soundflower):\n");
    fscanf(stdin, "%d", &captureDeviceIndex);

    *sourceDevice = soundio_get_input_device(soundio, sourceDeviceIndex);
    *captureDevice = soundio_get_input_device(soundio, captureDeviceIndex);

    return *sourceDevice != NULL && *captureDevice != NULL;
}

static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    struct RecordContext *rc = (struct RecordContext*)instream->userdata;
    struct SoundIoChannelArea *areas;
    int err;

    char *write_ptr = soundio_ring_buffer_write_ptr(rc->ring_buffer);
    int free_bytes = soundio_ring_buffer_free_count(rc->ring_buffer);
    int free_count = free_bytes / instream->bytes_per_frame;

    if (free_count < frame_count_min) {
        fprintf(stderr, "ring buffer overflow\n");
        exit(1);
    }

    int write_frames = min_int(free_count, frame_count_max);
    int frames_left = write_frames;

    for (;;) {
        int frame_count = frames_left;

        if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
            fprintf(stderr, "begin read error: %s", soundio_strerror(err));
            exit(1);
        }

        if (!frame_count)
            break;

        if (!areas) {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
        } else {
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
                    
                    { memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample); }
                    areas[ch].ptr += areas[ch].step;
                    { write_ptr += instream->bytes_per_sample; }

                }
            }
        }

        if ((err = soundio_instream_end_read(instream))) {
            fprintf(stderr, "end read error: %s", soundio_strerror(err));
            exit(1);
        }

        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }

    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(rc->ring_buffer, advance_bytes);
}

static void overflow_callback(struct SoundIoInStream *instream) {
    static int count = 0;
    fprintf(stderr, "overflow %d\n", ++count);
}