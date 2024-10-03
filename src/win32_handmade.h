#include <windows.h>
#include "handmade.h"
struct win32_offscreen_buffer
{
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
    BITMAPINFO Info;
};

struct win32_window_dimension
{
    int Width;
    int Height;
};

struct win32_sound_output
{
    int SamplesPerSecond;
    uint32 RunningSampleIndex;
    int BytesPerSample;
    int SecondaryBufferSize;
    real32 tSine;
    DWORD SafetyBytes;
    int LatencySampleCount;
};

struct win32_game_code
{
    HMODULE GameCodeDLL;
    FILETIME DLLLastWriteTime;
    game_update_and_render *UpdateAndRender;
    game_get_sound_samples *GetSoundSamples;

    bool32 IsValid;
};

struct win32_state
{
    void *GameMemoryBlock;
    uint64 TotalSize;

    HANDLE RecordingHandle;
    int InputRecordingIndex;

    HANDLE PlaybackHandle;
    int InputPlaybackIndex;
};
