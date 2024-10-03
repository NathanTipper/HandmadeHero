#include "handmade.h"

#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <xinput.h>
#include <dsound.h>

#include "win32_handmade.h"
global_variable bool Running;
global_variable win32_offscreen_buffer GlobalBackBuffer;
global_variable LPDIRECTSOUNDBUFFER SecondaryBuffer;
global_variable int64 PerfCountFrequency;

#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE *pState)
typedef X_INPUT_GET_STATE(x_input_get_state);
X_INPUT_GET_STATE(XInputGetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration)
typedef X_INPUT_SET_STATE(x_input_set_state);
X_INPUT_SET_STATE(XInputSetStateStub)
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
global_variable x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPGUID lpGuid, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

DEBUG_PLATFORM_FREE_FILE_MEMORY(Win32_FreeFileMemory)
{
    if(Memory)
    {
        VirtualFree(Memory, 0, MEM_RELEASE);
    }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(Win32_ReadEntireFile)
{
    debug_read_file_result Result = {};
    HANDLE FileHandle = CreateFile(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER FileSize;
        if(GetFileSizeEx(FileHandle, &FileSize))
        {
            Assert(FileSize.QuadPart <= 0xFFFFFFFF);
            uint32 FileSize32 = (uint32)FileSize.QuadPart;
            Result.Contents = VirtualAlloc(0, FileSize32, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
            if(Result.Contents)
            {
                DWORD BytesRead;
                if(ReadFile(FileHandle, Result.Contents, FileSize32, &BytesRead, 0) && (FileSize32 == BytesRead))
                {
                    Result.ContentsSize = FileSize32;
                }
                else
                {
                    Win32_FreeFileMemory(Result.Contents);
                    Result.Contents = 0;
                }
            }
        }

        CloseHandle(FileHandle);
    }

    return Result;
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(Win32_WriteEntireFile)
{
    bool32 Result = false;
    HANDLE FileHandle = CreateFile(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if(FileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD BytesWritten;
        if(WriteFile(FileHandle, Memory, MemorySize, &BytesWritten, 0))
        {
            Result = (BytesWritten == MemorySize);
        }
        else 
        {

        }
        CloseHandle(FileHandle);
    }
    else
    {

    }

    return Result;
}


inline FILETIME
Win32GetLastWriteTime(char *Filename)
{
    FILETIME LastWriteTime = {};
    WIN32_FIND_DATA FindData;
    HANDLE FindHandle = FindFirstFile(Filename, &FindData);
    if(FindHandle != INVALID_HANDLE_VALUE)
    {
        LastWriteTime = FindData.ftLastWriteTime;
        FindClose(FindHandle);
    }

    return LastWriteTime;
}

internal win32_game_code
Win32LoadGameCode(char* SourceDLLName, char *TempDLLName)
{
    win32_game_code Result = {};

    Result.DLLLastWriteTime = Win32GetLastWriteTime(SourceDLLName);
    CopyFile(SourceDLLName, TempDLLName, FALSE);
    Result.GameCodeDLL = LoadLibraryA(TempDLLName);
    if(Result.GameCodeDLL)
    {
        Result.UpdateAndRender = (game_update_and_render*)GetProcAddress(Result.GameCodeDLL, "GameUpdateAndRender");
        Result.GetSoundSamples = (game_get_sound_samples*)GetProcAddress(Result.GameCodeDLL, "GameGetSoundSamples");

        Result.IsValid = (Result.UpdateAndRender && Result.GetSoundSamples);
    }

    if(!Result.IsValid)
    {
        Result.UpdateAndRender = GameUpdateAndRenderStub;
        Result.GetSoundSamples = GameGetSoundSamplesStub;
    }

    return Result;
}

internal void
Win32UnloadGameCode(win32_game_code *GameCode)
{
    if(GameCode->GameCodeDLL)
    {
        FreeLibrary(GameCode->GameCodeDLL);
    }

    GameCode->IsValid = false;
    GameCode->UpdateAndRender = GameUpdateAndRenderStub;
    GameCode->GetSoundSamples = GameGetSoundSamplesStub;
}

internal void
Win32LoadXInput(void)
{
    HMODULE XInputLibrary = LoadLibrary("xinput1_4.dll");
    if(!XInputLibrary)
    {
        XInputLibrary = LoadLibrary("xinput1_3.dll");
    }

    if(XInputLibrary)
    {
        XInputGetState = (x_input_get_state*)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state*)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal void
Win32InitDSound(HWND Window, int32 SamplesPerSecond, int32 BufferSize)
{
    HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");

    if(DSoundLibrary)
    {
        direct_sound_create *DirectSoundCreate = (direct_sound_create*)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

        LPDIRECTSOUND DirectSound;
        if(DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
        {
            WAVEFORMATEX WaveFormat = {};
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 2;
            WaveFormat.nSamplesPerSec = SamplesPerSecond;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = (WaveFormat.nChannels*WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec*WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;
            if(SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
            {
                DSBUFFERDESC BufferDescription;
                BufferDescription.dwSize = sizeof(BufferDescription);
                BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

                LPDIRECTSOUNDBUFFER PrimaryBuffer;
                if(SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
                {
                    HRESULT Error = PrimaryBuffer->SetFormat(&WaveFormat);
                    if(SUCCEEDED(Error))
                    {
                        OutputDebugStringA("Primary buffer format was set.\n");
                    }
                }
            }

            DSBUFFERDESC BufferDescription = {};
            BufferDescription.dwSize = sizeof(BufferDescription);
            BufferDescription.dwFlags = 0;
            BufferDescription.dwBufferBytes = BufferSize;
            BufferDescription.lpwfxFormat = &WaveFormat;
            HRESULT Error = DirectSound->CreateSoundBuffer(&BufferDescription, &SecondaryBuffer, 0);
            if(SUCCEEDED(Error))
            {
               OutputDebugStringA("Secondary buffer created successfully"); 
            }
        }
    }
}

internal win32_window_dimension Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension wd;

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    wd.Width = ClientRect.right - ClientRect.left;
    wd.Height = ClientRect.bottom - ClientRect.top;

    return wd;
}

internal void Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    if(Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->BytesPerPixel = 4;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (Buffer->Width*Buffer->Height) * Buffer->BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    Buffer->Pitch = Buffer->Width * Buffer->BytesPerPixel;
}

internal void Win32DisplayBufferInWindow(HDC DeviceContext, win32_offscreen_buffer *Buffer, int Width, int Height)
{
    StretchDIBits(DeviceContext,
                  /* X, Y, Width, Height,
                  X, Y, Width, Height,*/
                  0, 0, Width, Height,
                  0, 0, Buffer->Width, Buffer->Height,
                  Buffer->Memory,
                  &Buffer->Info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

internal void
Win32ClearBuffer(win32_sound_output *SoundOutput)
{

    VOID *Region1, *Region2;
    DWORD Region1Size, Region2Size; 

    if(SUCCEEDED(SecondaryBuffer->Lock(0, SoundOutput->SecondaryBufferSize, &Region1, &Region1Size, &Region2, &Region2Size, 0)))
    {
        uint8 *DestSample = (uint8*)Region1;
        for(DWORD ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex)
        {
            *DestSample++ = 0;
        }

        DestSample = (uint8*)Region2;
        for(DWORD ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex)
        {
            *DestSample++ = 0;
        }

        SecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void 
Win32WriteSoundBuffer(win32_sound_output* SoundOutput, DWORD ByteToLock, DWORD BytesToWrite, game_sound_output_buffer *SourceBuffer)
{
    VOID *Region1, *Region2;
    DWORD Region1Size, Region2Size; 

    if(SUCCEEDED(SecondaryBuffer->Lock(ByteToLock, BytesToWrite, &Region1, &Region1Size, &Region2, &Region2Size, 0)))
    {
        int16 *DestSample = (int16*)Region1;
        int16 *SourceSample = SourceBuffer->Samples;
        DWORD Region1SampleCount = Region1Size/SoundOutput->BytesPerSample;

        for(DWORD SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex)
        {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;

            ++SoundOutput->RunningSampleIndex;
        }

        DestSample = (int16 *)Region2;
        DWORD Region2SampleCount = Region2Size/SoundOutput->BytesPerSample;
        for(DWORD SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex)
        {
            *DestSample++ = *SourceSample++;
            *DestSample++ = *SourceSample++;

            ++SoundOutput->RunningSampleIndex;
        }

        SecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
    }
}

internal void
Win32ProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown)
{
    NewState->EndedDown = IsDown;
    ++NewState->HalfTransitionCount;
}

internal void
Win32ProcessXInputDigitalButton(DWORD XInputButtonState, game_button_state *OldState, DWORD ButtonBit, game_button_state *NewState)
{
    NewState->EndedDown = (XInputButtonState & ButtonBit) == ButtonBit;
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

internal real32 
Win32ProcessInputStickValue(SHORT Value, SHORT DeadzoneThreshold)
{
    real32 Result = 0;
    if(Value < DeadzoneThreshold)
    {
        Result = (real32)Value / 32768.0f;
    }
    else if(Value > DeadzoneThreshold)
    {
        Result = (real32)Value / 32767.0f;
    }

    return Result;
}

internal void
Win32BeginRecordingInput(win32_state *Win32State, int InputRecordingIndex)
{
    Win32State->InputRecordingIndex = InputRecordingIndex;
    char Filename[] = "input_playback.hmi";
    Win32State->RecordingHandle = CreateFileA(Filename, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    DWORD BytesToWrite = (DWORD)Win32State->TotalSize;
    Assert(Win32State->TotalSize == BytesToWrite);
    DWORD BytesWritten;
    WriteFile(Win32State->RecordingHandle, Win32State->GameMemoryBlock, BytesToWrite, &BytesWritten, 0);
}

internal void
Win32EndRecordingInput(win32_state *Win32State)
{
    CloseHandle(Win32State->RecordingHandle);
    Win32State->InputRecordingIndex = 0;
}

internal void
Win32RecordInput(win32_state *Win32State, game_input *InputToRecord)
{
    DWORD BytesWritten;
    WriteFile(Win32State->RecordingHandle, InputToRecord, sizeof(*InputToRecord), &BytesWritten, 0);
}

internal void
Win32BeginInputPlayback(win32_state *Win32State, int InputPlaybackIndex)
{
    Win32State->InputPlaybackIndex = InputPlaybackIndex;
    char Filename[] = "input_playback.hmi";
    Win32State->PlaybackHandle = CreateFile(Filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

    DWORD BytesToRead = (DWORD)Win32State->TotalSize;
    Assert(Win32State->TotalSize == BytesToRead);
    DWORD BytesRead;
    ReadFile(Win32State->PlaybackHandle, Win32State->GameMemoryBlock, BytesToRead, &BytesRead, 0);
}

internal void
Win32EndPlaybackInput(win32_state *Win32State)
{
    CloseHandle(Win32State->PlaybackHandle);
    Win32State->InputPlaybackIndex = 0;
}

internal void
Win32PlaybackInput(win32_state *Win32State, game_input *PlaybackInput)
{
    DWORD BytesRead = 0;
    if(ReadFile(Win32State->PlaybackHandle, PlaybackInput, sizeof(*PlaybackInput), &BytesRead, 0))
    {
        if(BytesRead == 0)
        {
            int PlayingIndex = Win32State->InputPlaybackIndex;
            Win32EndPlaybackInput(Win32State);
            Win32BeginInputPlayback(Win32State, PlayingIndex);
        }
    }
}

internal void
Win32ProcessPendingMessages(win32_state *Win32State, game_controller_input *KeyboardController)
{
    MSG Message;
    while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
    {
        if(Message.message == WM_QUIT)
        {
            Running = false;
        }
        switch(Message.message)
        {
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_KEYDOWN:
            case WM_KEYUP:
                {
                    uint32 VKCode = (uint32)Message.wParam;
                    bool WasDown = ((Message.lParam & (1 << 30)) != 0);
                    bool IsDown ((Message.lParam & (1 << 31)) == 0);

                    if(WasDown != IsDown)
                    {
                        if(VKCode == 'W')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->MoveUp, IsDown);
                        }
                        else if(VKCode == 'A')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->MoveLeft, IsDown);
                        }
                        else if(VKCode == 'S')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->MoveDown, IsDown);
                        }
                        else if(VKCode == 'D')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->MoveRight, IsDown);
                        }
                        else if(VKCode == 'Q')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->LeftShoulder, IsDown);
                        }
                        else if(VKCode == 'E')
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->RightShoulder, IsDown);
                        }
                        else if(VKCode == VK_UP)
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->ActionUp, IsDown);
                        }
                        else if(VKCode == VK_DOWN)
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->ActionDown, IsDown);
                        }
                        else if(VKCode == VK_LEFT)
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->ActionLeft, IsDown);
                        }
                        else if(VKCode == VK_RIGHT)
                        {
                            Win32ProcessKeyboardMessage(&KeyboardController->ActionRight, IsDown);
                        }
                        else if(VKCode == VK_ESCAPE)
                        {
                            OutputDebugStringA("Escape: ");
                            if(IsDown)
                            {
                                OutputDebugStringA("IsDown ");
                            }
                            if(WasDown)
                            {
                                OutputDebugStringA("WasDown ");
                            }
                            OutputDebugStringA("\n");
                        }
                        else if(VKCode == VK_SPACE)
                        {

                        }
#if HANDMADE_INTERNAL
                        else if(VKCode == 'P')
                        {

                        }
                        else if(VKCode == 'L')
                        {
                            if(IsDown)
                            {
                                if(Win32State->InputRecordingIndex == 0)
                                {
                                    Win32BeginRecordingInput(Win32State, 1);
                                }
                                else
                                {
                                    Win32EndRecordingInput(Win32State);
                                    Win32BeginInputPlayback(Win32State, 1);
                                }
                            }
                        }
                        else if(VKCode == '1')
                        {

                        }
#endif
                    }

                    bool AltKeyWasDown = (Message.lParam & (1 << 29));
                    if((VKCode == VK_F4) && AltKeyWasDown)
                    {
                        Running = false;
                    }
                } break;
            default:
                {
                    TranslateMessage(&Message);
                    DispatchMessage(&Message);
                } break;
        }
    }

}

LRESULT CALLBACK Win32MainWindowCallback(
                            HWND hwnd,
                            UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam)
{
    LRESULT Result = 0;
    switch(uMsg)
    {
        case WM_SIZE:
            {
                break;
            }
        case WM_DESTROY:
            {
                Running = false;
                break;
            }
        case WM_CLOSE:
            {
                Running = false;
                break;
            }
        case WM_ACTIVATEAPP:
            {
                break;
            }
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
            {
                Assert(!"Keyboard input processed outside of normal route!");
            } break;
        case WM_PAINT:
            {
                PAINTSTRUCT Paint;
                HDC DeviceContext = BeginPaint(hwnd, &Paint);
                int x = Paint.rcPaint.left;
                int y = Paint.rcPaint.top;
                int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
                int Width = Paint.rcPaint.right - Paint.rcPaint.left;

                win32_window_dimension Dimension = Win32GetWindowDimension(hwnd);

                Win32DisplayBufferInWindow(DeviceContext, &GlobalBackBuffer, Width, Height);

                EndPaint(hwnd, &Paint);
                break;
            }
        default:
            {
                Result = DefWindowProc(hwnd, uMsg, wParam, lParam);
                break;
            }
    }

    return Result;
}
inline LARGE_INTEGER
Win32GetWallClock(void)
{
    LARGE_INTEGER Result;
    QueryPerformanceCounter(&Result);
    return Result;
}

inline real32
Win32GetSecondsElapsed(LARGE_INTEGER Start, LARGE_INTEGER End)
{
    real32 Result = (((real32)End.QuadPart - (real32)Start.QuadPart) / (real32)PerfCountFrequency);
    return Result;
}

#if 0
internal void
Win32DebugDrawVertical(win32_offscreen_buffer *Buffer, int X, int Top, int Bottom, uint32 Color)
{
    uint8 *Pixel = ((uint8*)Buffer->Memory + X*Buffer->BytesPerPixel + Top*Buffer->Pitch);
    for(int Y = Top; Y < Bottom; ++Y)
    {
        *(uint32 *)Pixel = Color;
        Pixel += Buffer->Pitch;
    }
}

internal void
Win32DebugSyncDisplay(win32_offscreen_buffer *Buffer, int LastPlayCursorCount, DWORD *DebugLastPlayCursor, 
                        win32_sound_output *SoundOutput, real32 TargetSecondsPerFrame)
{
    int PadX = 16;
    int PadY = 16;

    int Top = PadY;
    int Bottom = Buffer->Height - PadY;
    real32 C = (real32)(Buffer->Width - (2*PadX)) / (real32)SoundOutput->SecondaryBufferSize;
    for(int PlayCursorIndex = 0; PlayCursorIndex < LastPlayCursorCount; ++PlayCursorIndex)
    {
        int X = PadX + (int)(C * (real32)DebugLastPlayCursor[PlayCursorIndex]);
        Win32DebugDrawVertical(Buffer, X, Top, Bottom, 0xFFFFFFFF);
    }
}
#endif
internal void
CatStrings(size_t SourceACount, char *SourceA, size_t SourceBCount, char *SourceB, size_t DestCount, char *Dest)
{
    for(int Index = 0; Index < SourceACount; ++Index)
    {
        *Dest++ = *SourceA++;
    }

    for(int Index = 0; Index < SourceBCount; ++Index)
    {
        *Dest++ = *SourceB++;
    }

    *Dest++ = 0;
}

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   PSTR lpCmdLine,
                   int nCmdShow)
{
    char ExeFilename[MAX_PATH];
    DWORD SizeOfFilename = GetModuleFileName(0, ExeFilename, sizeof(ExeFilename));
    char *OnePastLastSlash = ExeFilename;
    for(char *Scan = ExeFilename; *Scan; ++Scan)
    {
        if(*Scan == '\\')
        {
            OnePastLastSlash = Scan+1;
        }
    }
    char SourceGameCodeFilename[] = "handmade.dll";
    char SourceGameCodeDLLFullPath[MAX_PATH];
    CatStrings(OnePastLastSlash - ExeFilename, ExeFilename, sizeof(SourceGameCodeFilename) - 1, SourceGameCodeFilename, sizeof(SourceGameCodeDLLFullPath), SourceGameCodeDLLFullPath);

    char TempGameCodeDLLFilename[] = "handmade_temp.dll";
    char TempGameCodeDLLFullpath[MAX_PATH];

    CatStrings(OnePastLastSlash - ExeFilename, ExeFilename, sizeof(TempGameCodeDLLFilename) - 1, TempGameCodeDLLFilename, sizeof(TempGameCodeDLLFullpath), TempGameCodeDLLFullpath);
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency(&PerfCountFrequencyResult);
    PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    UINT DesiredSchedulerMS = 1;
    bool32 SleepIsGranular = timeBeginPeriod(DesiredSchedulerMS) == TIMERR_NOERROR;

    Win32LoadXInput();
    WNDCLASS WindowClass = {};
    Win32ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

    WindowClass.style = CS_HREDRAW|CS_VREDRAW;
    WindowClass.lpfnWndProc = Win32MainWindowCallback;
    WindowClass.hInstance = hInstance;
    WindowClass.lpszClassName = "HandmadeHeroWindowClass";

    // TODO: @ntipp Learn how to reliably get this from Windows
#define MonitorRefreshHz 60
#define GameUpdateHz (MonitorRefreshHz / 2)
    real32 TargetSecondsPerFrame = 1.f / (real32)GameUpdateHz;

    if(RegisterClass(&WindowClass))
    {
        HWND Window =
            CreateWindowExA(
                0,
                WindowClass.lpszClassName,
                "Handmade Hero",
                WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                0,
                0,
                hInstance,
                0);

        if(Window)
        {
            Running = true;

            win32_state Win32State = {};
            win32_sound_output SoundOutput = {};

            SoundOutput.SamplesPerSecond = 48000;
            SoundOutput.RunningSampleIndex = 0;
            SoundOutput.BytesPerSample = sizeof(int16)*2;
            SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
            SoundOutput.SafetyBytes = (SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample / GameUpdateHz) / 3;
            SoundOutput.LatencySampleCount = (SoundOutput.SamplesPerSecond / GameUpdateHz);

            Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
            Win32ClearBuffer(&SoundOutput);
            if(SecondaryBuffer)
            {
                SecondaryBuffer->Play(0,0,DSBPLAY_LOOPING);
            }

            int16 *Samples = (int16*)VirtualAlloc(0, SoundOutput.SecondaryBufferSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

#if HANDMADE_INTERNAL
            LPVOID BaseAddress = (LPVOID)Terabytes((uint64)2);
#else 
            LPVOID BaseAddress = 0;
#endif
            game_memory GameMemory = {};
            GameMemory.PermanentStorageSize = Megabytes(64);
            GameMemory.TransientStorageSize = Gigabytes(1);
            GameMemory.DEBUGPlatformFreeFileMemory = &Win32_FreeFileMemory;
            GameMemory.DEBUGPlatformReadFile = &Win32_ReadEntireFile;
            GameMemory.DEBUGPlatformWriteFile = &Win32_WriteEntireFile;

            Win32State.TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;
            Win32State.GameMemoryBlock = VirtualAlloc(BaseAddress, (size_t)Win32State.TotalSize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);

            GameMemory.PermanentStorage = Win32State.GameMemoryBlock;
            GameMemory.TransientStorage = ((uint8*) GameMemory.PermanentStorage + GameMemory.PermanentStorageSize);
            if(GameMemory.PermanentStorage && GameMemory.TransientStorage && Samples)
            {
                win32_game_code Game = Win32LoadGameCode(SourceGameCodeDLLFullPath, TempGameCodeDLLFullpath);

                LARGE_INTEGER LastCounter = Win32GetWallClock();
                LARGE_INTEGER FlipWallClock = Win32GetWallClock();

                game_input Input[2] = {};
                game_input *NewInput = &Input[0];
                game_input *OldInput = &Input[1];

                DWORD LastPlayCursor = 0;
                DWORD LastWriteCursor = 0;
                bool SoundIsValid = false;
                DWORD AudioLatencyBytes = 0;
                real32 AudioLatencySeconds = 0;

                while(Running)
                {
                    FILETIME NewDLLWriteTime = Win32GetLastWriteTime(SourceGameCodeDLLFullPath);
                    if(CompareFileTime(&NewDLLWriteTime, &Game.DLLLastWriteTime) != 0)
                    {
                        Win32UnloadGameCode(&Game);
                        Game = Win32LoadGameCode(SourceGameCodeDLLFullPath, TempGameCodeDLLFullpath);
                    }
                    game_controller_input *OldKeyboardController = GetController(OldInput, 0);
                    game_controller_input *NewKeyboardController = GetController(NewInput, 0);

                    game_controller_input ZeroController = {};

                    for(int ButtonIndex = 0; ButtonIndex < ArrayCount(NewKeyboardController->Buttons); ++ButtonIndex)
                    {
                        NewKeyboardController->Buttons[ButtonIndex].EndedDown = OldKeyboardController->Buttons[ButtonIndex].EndedDown;
                    }

                    Win32ProcessPendingMessages(&Win32State, NewKeyboardController);
                    DWORD MaxControllerCount = XUSER_MAX_COUNT;
                    if(MaxControllerCount > (ArrayCount(NewInput->Controllers) - 1))
                    {
                        MaxControllerCount = (ArrayCount(NewInput->Controllers) - 1);
                    }

                    for(DWORD ControllerIndex = 0; ControllerIndex < MaxControllerCount; ++ControllerIndex)
                    {
                        DWORD OurControllerIndex = ControllerIndex + 1;
                        game_controller_input *OldController = GetController(OldInput, OurControllerIndex);
                        game_controller_input *NewController = GetController(NewInput, OurControllerIndex);

                        XINPUT_STATE ControllerState;
                        if(XInputGetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS)
                        {
                            NewController->IsConnected = true;
                            NewController->IsAnalog = OldController->IsAnalog;
                            XINPUT_GAMEPAD *Pad = &ControllerState.Gamepad;
                            if(Pad)
                            {

                                NewController->StickAverageX = Win32ProcessInputStickValue(Pad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                                NewController->StickAverageY = Win32ProcessInputStickValue(Pad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

                                if(NewController->StickAverageX != 0.f && NewController->StickAverageY != 0.f)
                                {
                                    NewController->IsAnalog = true;
                                }

                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)
                                {
                                    NewController->StickAverageY = 1.0f;
                                    NewController->IsAnalog = false;
                                }

                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)
                                {
                                    NewController->StickAverageY = -1.0f;
                                    NewController->IsAnalog = false;
                                }

                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)
                                {
                                    NewController->StickAverageX = -1.0f;
                                    NewController->IsAnalog = false;
                                }

                                if(Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)
                                {
                                    NewController->StickAverageX = 1.0f;
                                    NewController->IsAnalog = false;
                                }

                                real32 Threshold = 0.5f;

                                Win32ProcessXInputDigitalButton((NewController->StickAverageX < -Threshold) ? 1 : 0, &OldController->MoveLeft, 1, &NewController->MoveLeft);
                                Win32ProcessXInputDigitalButton((NewController->StickAverageX > Threshold) ? 1 : 0, &OldController->MoveRight, 1, &NewController->MoveRight);
                                Win32ProcessXInputDigitalButton((NewController->StickAverageY < -Threshold) ? 1 : 0, &OldController->MoveDown, 1, &NewController->MoveDown);
                                Win32ProcessXInputDigitalButton((NewController->StickAverageY > Threshold) ? 1 : 0, &OldController->MoveUp, 1, &NewController->MoveUp);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->ActionDown, XINPUT_GAMEPAD_A, &NewController->ActionDown);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->ActionRight, XINPUT_GAMEPAD_B, &NewController->ActionRight);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->ActionLeft, XINPUT_GAMEPAD_X, &NewController->ActionLeft);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->ActionUp, XINPUT_GAMEPAD_Y, &NewController->ActionUp);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->LeftShoulder, XINPUT_GAMEPAD_LEFT_SHOULDER, &NewController->LeftShoulder);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->RightShoulder, XINPUT_GAMEPAD_RIGHT_SHOULDER, &NewController->RightShoulder);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Start, XINPUT_GAMEPAD_START, &NewController->Start);
                                Win32ProcessXInputDigitalButton(Pad->wButtons, &OldController->Back, XINPUT_GAMEPAD_BACK, &NewController->Back);
                            }
                        }
                        else
                        {
                            NewController->IsConnected = false;
                        }
                    }

                    game_offscreen_buffer Buffer = {};
                    Buffer.Memory = GlobalBackBuffer.Memory;
                    Buffer.Width = GlobalBackBuffer.Width;
                    Buffer.Height = GlobalBackBuffer.Height;
                    Buffer.Pitch = GlobalBackBuffer.Pitch;
                    Buffer.BytesPerPixel = GlobalBackBuffer.BytesPerPixel;

                    if(Win32State.InputRecordingIndex)
                    {
                        Win32RecordInput(&Win32State, NewInput);
                    }

                    if(Win32State.InputPlaybackIndex)
                    {
                        Win32PlaybackInput(&Win32State, NewInput);
                    }
                    Game.UpdateAndRender(&GameMemory, NewInput, &Buffer);

                    LARGE_INTEGER AudioWallClock = Win32GetWallClock();
                    real32 FromBeginToAudioSeconds = Win32GetSecondsElapsed(FlipWallClock, AudioWallClock);

                    DWORD PlayCursor;
                    DWORD WriteCursor;
                    if(SecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor) == DS_OK)
                    {
                        /* NOTE:: We define a safety value that is the number of samples we think our game update look
                         * may vary by (let's say up to 2ms)
                         * Here is how the sound output computation works
                         * When we wake up to write audio, we will look and see what the play cursor position is
                         * and we will forecast ahead where we think the play cursor will be on the next frame boundary.
                         * We will then look to see if the write cursor is before that by at least our safety value. If it is, the target
                         * fill position is that frame boundary plus one. This gives us perfect audio sync in the case of a card that has low 
                         * enough latency.
                         * If the write cursor is _after_ that safety margin, then we assume we can never sync the audio perfectly,
                         * so we will write one frame's worth of audio plus some number of guard samples (1ms, or something determined to be safe,
                         * whatever we the think the variability of our frame computation is).
                         * Continue to write to the sound buffer
                         */
                        if(!SoundIsValid)
                        {
                            SoundOutput.RunningSampleIndex = WriteCursor / SoundOutput.BytesPerSample;
                            SoundIsValid = true;
                        }

                        DWORD ByteToLock = (SoundOutput.RunningSampleIndex*SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;

                        DWORD ExpectedSoundBytesPerFrame = (SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample) / GameUpdateHz;
                        real32 SecondsLeftUntilFlip = (TargetSecondsPerFrame - FromBeginToAudioSeconds);
                        DWORD ExpectedBytesUntilFlip = (DWORD)((SecondsLeftUntilFlip/TargetSecondsPerFrame) * (real32)ExpectedSoundBytesPerFrame);
                        DWORD ExpectedFrameBoundaryByte = PlayCursor + ExpectedBytesUntilFlip;

                        DWORD SafeWriteCursor = WriteCursor;
                        if(SafeWriteCursor < PlayCursor)
                        {
                            SafeWriteCursor += SoundOutput.SecondaryBufferSize;
                        }
                        Assert(SafeWriteCursor >= PlayCursor);
                        SafeWriteCursor += SoundOutput.SafetyBytes;

                        bool32 AudioCardIsLowLatency = (SafeWriteCursor < ExpectedFrameBoundaryByte);
                        DWORD TargetCursor = 0;
                        if(AudioCardIsLowLatency)
                        {
                            TargetCursor = ExpectedFrameBoundaryByte + ExpectedSoundBytesPerFrame;
                        }
                        else
                        {
                            TargetCursor = WriteCursor + ExpectedSoundBytesPerFrame + SoundOutput.SafetyBytes;
                        }

                        TargetCursor = (TargetCursor % SoundOutput.SecondaryBufferSize);

                        DWORD BytesToWrite = 0;
                        if(ByteToLock > TargetCursor)
                        {
                            BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
                            BytesToWrite += TargetCursor;
                        }
                        else
                        {
                            BytesToWrite = TargetCursor - ByteToLock;
                        }

                        game_sound_output_buffer SoundBuffer = {};
                        SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                        SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                        SoundBuffer.Samples = Samples;
                        Game.GetSoundSamples(&GameMemory, &SoundBuffer);

                        Win32WriteSoundBuffer(&SoundOutput, ByteToLock, BytesToWrite, &SoundBuffer);
                    }
                    else 
                    {
                        SoundIsValid = false;
                    }
                    LARGE_INTEGER WorkCounter = Win32GetWallClock();
                    real32 WorkSecondsElapsed = Win32GetSecondsElapsed(LastCounter, WorkCounter);

                    real32 SecondsElapsedForFrame = WorkSecondsElapsed;
                    if(SecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        if(SleepIsGranular)
                        {
                            DWORD SleepMS = (DWORD)((TargetSecondsPerFrame - SecondsElapsedForFrame) * 1000.f);
                            if(SleepMS > 0)
                            {
                                Sleep(SleepMS);
                            }
                        }
                        real32 TestSecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter, Win32GetWallClock());
                        if(TestSecondsElapsedForFrame < TargetSecondsPerFrame)
                        {
                            // TODO: Log missed sleep here
                        }
                        while(SecondsElapsedForFrame < TargetSecondsPerFrame) 
                        {
                            SecondsElapsedForFrame = Win32GetSecondsElapsed(LastCounter, Win32GetWallClock());
                        }
                    }
                    else
                    {

                    }

                    LARGE_INTEGER EndCounter = Win32GetWallClock();
                    LastCounter = EndCounter;
                    HDC DeviceContext = GetDC(Window);
                    win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                    Win32DisplayBufferInWindow(DeviceContext, &GlobalBackBuffer, Dimension.Width, Dimension.Height);
#if 0
                    int32 FPS = (int32)(PerfCountFrequency / CounterElapsed);
                    uint32 MCPF = (uint32)(CyclesElapsed / (1000 * 1000));

                    char Buffer[256];
                    wsprintf(Buffer, "%dms/f, %df/s, %dc/f\n", MillisecondsPerFrame, FPS, MCPF);
                    OutputDebugStringA(Buffer);
#endif
                    game_input *Temp = NewInput;
                    NewInput = OldInput;
                    OldInput = Temp;
                } // end of game loop
            }
            else // No Memory or Sound
            {

            }
        }
    }
    else
    {
        // TODO:@ntipper Logging
    }

    return 0;
}
