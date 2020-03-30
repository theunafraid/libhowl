#ifndef __HOWLLIB_H__

struct HowlLibContext;

typedef void (*fpPreHowlDetected)();

HowlLibContext* createHowlLibContext();

void destroyHowlLibContext(
    HowlLibContext*
);

int initHowlLibContext(
    HowlLibContext*, // HowlLib
    int, // SampleRate
    int, // Buffer ms
    fpPreHowlDetected
);

int feedSourceAudio(
    HowlLibContext*,
    float*,
    int
);

int feedCaptureAudio(
    HowlLibContext*,
    float*,
    int
);

#endif