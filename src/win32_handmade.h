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
};

struct win32_game_code
{
    HMODULE GameCodeDLL;
    FILETIME DLLLastWriteTime;
    game_update_and_render *UpdateAndRender;
    game_get_sound_samples *GetSoundSamples;

    bool32 IsValid;
};
#define WIN32_STATE_FILE_NAME_COUNT MAX_PATH
struct win32_replay_buffer
{
    HANDLE FileHandle;
    HANDLE MemoryMap;
    char Filename[WIN32_STATE_FILE_NAME_COUNT];
    void *MemoryBlock;
};

struct win32_state
{
    void *GameMemoryBlock;
    uint64 TotalSize;
    win32_replay_buffer ReplayBuffers[4];

    HANDLE RecordingHandle;
    int InputRecordingIndex;

    HANDLE PlaybackHandle;
    int InputPlaybackIndex;

    char EXEFilename[MAX_PATH];
    char *OnePastLastEXEFileNameSlash;
};
