#include <SDL2/SDL.h>
#include <sstream>

#include <howl.h>

SDL_AudioDeviceID sourceDeviceID;
SDL_AudioDeviceID captureDeviceID;

HowlLibContext* howlLib = NULL;

//Main window
SDL_Window* gWindow = NULL;

bool initSDL();
void closeSDL();
bool promptAudioDevices();
int startSourceDevice();
int startCaptureDevice();

// SDL audio callbacks
void sourceCallback( void* userdata, Uint8* stream, int len );
void captureCallback( void* userdata, Uint8* stream, int len );

//
static void preHowlDetected()
{
    fprintf(stdout, "Pre howling detected!\n");
}

int main(int argc, const char** argv)
{

    if (!initSDL())
    {
        fprintf(stderr, "Failed to initialize SDL!\n");
    }

    if (!promptAudioDevices())
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
        1000,
        preHowlDetected))
    {
        fprintf(stderr, "Failed to initialize libhowl!\n");
        return -1;
    }

    if (startSourceDevice() != 0)
    {
        fprintf(stderr, "Failed to start source device!\n");
        return -1;
    }

    if (startCaptureDevice() != 0)
    {
        fprintf(stderr, "Failed to start capture device!\n");
        return -1;
    }

    //Main loop flag
    bool quit = false;

    //Event handler
    SDL_Event e;

    //While application is running
    while( !quit )
    {
        //Handle events on queue
        while( SDL_PollEvent( &e ) != 0 )
        {
            switch(e.type)
            {
                case SDL_QUIT:
                {
                    quit = true;
                    break;
                }
                case SDL_KEYDOWN:
                {
                    if (e.key.keysym.sym == SDLK_ESCAPE)
                    {
                        quit = true;
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }

    closeSDL();

    return 0;
}

void sourceCallback( void* userdata, Uint8* stream, int len )
{
    HowlLibContext* howlLib = (HowlLibContext*) userdata;

    if (feedSourceAudio(
        howlLib,
        (float*)stream,
        (len/sizeof(float))) != 0)
    {
        fprintf(stdout, "Failed to feed source audio!\n");
    }
}

void captureCallback( void* userdata, Uint8* stream, int len )
{
    HowlLibContext* howlLib = (HowlLibContext*) userdata;

    if (feedCaptureAudio(
        howlLib,
        (float*)stream,
        (len/sizeof(float))) != 0)
    {
        fprintf(stdout, "Failed to feed capture audio!\n");
    }
}

int startSourceDevice()
{
    SDL_AudioSpec sourceDeviceSpec, sourceDeviceSpecGiven;
    SDL_zero(sourceDeviceSpec);
	sourceDeviceSpec.freq = 44100;
	sourceDeviceSpec.format = AUDIO_F32;
	sourceDeviceSpec.channels = 1;
	sourceDeviceSpec.samples = 4096;
	sourceDeviceSpec.callback = sourceCallback;
    sourceDeviceSpec.userdata = howlLib;

    sourceDeviceID = SDL_OpenAudioDevice(
                        SDL_GetAudioDeviceName(sourceDeviceID, SDL_TRUE),
                        SDL_TRUE,
                        &sourceDeviceSpec,
                        &sourceDeviceSpecGiven,
                        SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    
    if (sourceDeviceID == 0)
    {
        fprintf(stderr, "\nFailed to open recording device! SDL Error: %s", SDL_GetError() );
        return -1;
    }

    SDL_PauseAudioDevice( sourceDeviceID, SDL_FALSE );

    return 0;
}

int startCaptureDevice()
{
    SDL_AudioSpec captureDeviceSpec, captureDeviceSpecGiven;
    SDL_zero(captureDeviceSpec);
	captureDeviceSpec.freq = 44100;
	captureDeviceSpec.format = AUDIO_F32;
	captureDeviceSpec.channels = 1;
	captureDeviceSpec.samples = 4096;
	captureDeviceSpec.callback = captureCallback;
    captureDeviceSpec.userdata = howlLib;

    captureDeviceID = SDL_OpenAudioDevice(
                        SDL_GetAudioDeviceName(captureDeviceID, SDL_FALSE),
                        SDL_FALSE,
                        &captureDeviceSpec,
                        &captureDeviceSpecGiven,
                        SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    
    if (captureDeviceID == 0)
    {
        fprintf(stderr, "\nFailed to open recording device! SDL Error: %s", SDL_GetError() );
        return -1;
    }

    SDL_PauseAudioDevice( captureDeviceID, SDL_FALSE );

    return 0;
}

bool initSDL()
{
    bool bSuccess = true;

    //Initialize SDL
    if( SDL_Init( SDL_INIT_AUDIO | SDL_INIT_EVENTS ) < 0 )
    {
        printf( "SDL could not initialize! SDL Error: %s\n", SDL_GetError() );
        bSuccess = false;
    }
    else
    {
        //Create window
        gWindow = SDL_CreateWindow( "Echo detection - press e", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 160, SDL_WINDOW_HIDDEN );
        if( gWindow == NULL )
        {
            printf( "Window could not be created! SDL Error: %s\n", SDL_GetError() );
            bSuccess = false;
        }
    }

    return bSuccess;
}

void closeSDL()
{
    SDL_Quit();
}

/**
 * Source = input / original
 * Capture = output / modified
 */
bool promptAudioDevices()
{
    std::stringstream promptText;

    // Select capture
    int captureDeviceCount = SDL_GetNumAudioDevices( SDL_FALSE );

    if (captureDeviceCount < 1)
    {
        fprintf(stderr, "No capture device found!\n");
        return false;
    }
    else
    {
        fprintf(stdout, "Capture(output) devices :\n");
        for (int i = 0; i < captureDeviceCount; ++i)
        {
            promptText.str("");
            promptText << "DeviceID : " << i << " Name : " << SDL_GetAudioDeviceName(i, SDL_FALSE );
            fprintf(stdout, "%s\n", promptText.str().c_str());
        }
    }

    // Select source
    int sourceDeviceCount = SDL_GetNumAudioDevices( SDL_TRUE );

    if (sourceDeviceCount < 1)
    {
        fprintf(stderr, "No source device found!\n");
    }
    else
    {
        fprintf(stdout, "Source(input) devices :\n");
        for (int i =0; i < sourceDeviceCount; ++i)
        {
            promptText.str("");
            promptText << "DeviceID : " << i << " Name : " << SDL_GetAudioDeviceName(i, SDL_TRUE );
            fprintf(stdout, "%s\n", promptText.str().c_str());
        }
    }

    int selectedSourceDeviceId, selectedCaptureDeviceId;
    fprintf(stdout, "Specify capture device :\n");
    fscanf(stdin, "%d", &selectedSourceDeviceId);
    fprintf(stdout, "Specify source device :\n");
    fscanf(stdin, "%d", &selectedCaptureDeviceId);

    if (selectedCaptureDeviceId < 0 || selectedCaptureDeviceId > captureDeviceCount)
    {
        fprintf(stdout, "Invalid capture device id\n");
        return false;
    }

    if (selectedSourceDeviceId < 0 || selectedSourceDeviceId > sourceDeviceCount)
    {
        fprintf(stdout, "Invalid source device id\n");
        return false;
    }

    sourceDeviceID = selectedSourceDeviceId;
    captureDeviceID = selectedCaptureDeviceId;

    return true;
}
