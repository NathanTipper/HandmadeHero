#include "handmade.h"

internal void
GameOutputSound(game_state *GameState, game_sound_output_buffer *SoundBuffer)
{
    int ToneHz = GameState->ToneHz;
    real32 tSine = GameState->tSine;

    int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    int16 *SampleOut = SoundBuffer->Samples;
    for(int SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex)
    {
        real32 SineValue = sinf(tSine);
#if 0
        int16 SampleValue = (int16)(SineValue * ToneHz);
#else
        int16 SampleValue = 0;
#endif
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        GameState->tSine += (real32)(2.0f*Pi32*1.0f)/(real32)WavePeriod;
        if(GameState->tSine > (2.0f*Pi32))
        {
            GameState->tSine -= (real32)(2.0f*Pi32);
        }
    }
}

void
RenderWeirdGradient(game_offscreen_buffer *Buffer, int BlueOffset, int GreenOffset)
{
    int Width = Buffer->Width;
    int Height = Buffer->Height;

    uint8 *Row = (uint8*)Buffer->Memory;

    for(int Y = 0; Y < Height; ++Y)
    {
        uint32 *Pixel = (uint32*)Row;
        for(int X = 0; X < Width; ++X)
        {
            uint8 Blue = (uint8)(X+BlueOffset);
            uint8 Green = (uint8)(Y+GreenOffset);

            *Pixel++ = ((Green << 8) | (Blue << 16));
        }

        Row += Buffer->Pitch;
    }
}

internal void
RenderPlayer(game_offscreen_buffer *Buffer, int PlayerX, int PlayerY)
{
    uint32 Color = 0xFFFFFFFF;
    int Top = PlayerY;
    int Bottom = PlayerY + 10;
    for(int X = PlayerX; X < PlayerX + 10; ++X)
    {
        uint8 *Pixel = ((uint8*)Buffer->Memory + X*Buffer->BytesPerPixel + Top*Buffer->Pitch);
        for(int Y = Top; Y < Bottom; ++Y)
        {
            if(Pixel >= Buffer->Memory)
            {
                *(uint32 *)Pixel = Color;
                Pixel += Buffer->Pitch;
            }
        }
    }
}

GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *GameState = (game_state*)Memory->PermanentStorage;
    if(!Memory->IsInitialized)
    {
        GameState->ToneHz = 256;
        GameState->GreenOffset = 0;
        GameState->BlueOffset = 0;

        GameState->PlayerX = 100;
        GameState->PlayerY = 100;
        Memory->IsInitialized = true;
    }

    for(int ControllerIndex = 0; ControllerIndex < ArrayCount(Input->Controllers); ++ControllerIndex)
    {
        game_controller_input *Controller = GetController(Input, ControllerIndex);

        if(Controller->IsAnalog)
        {
            GameState->ToneHz = 256 + (int)(120.f * Controller->StickAverageY);
            GameState->BlueOffset += (int)(4.0f* Controller->StickAverageX);
        }
        else
        {
            if(Controller->MoveLeft.EndedDown)
            {
                GameState->BlueOffset -= 1;
            }

            if(Controller->MoveRight.EndedDown)
            {
                GameState->BlueOffset += 1;
            }
        }

        if(Controller->MoveDown.EndedDown)
        {
            GameState->GreenOffset += 1;
        }

        GameState->PlayerX += (int)(4.f * Controller->StickAverageX);
        GameState->PlayerY -= (int)(4.f * Controller->StickAverageY);
    }

    RenderWeirdGradient(Buffer, GameState->BlueOffset, GameState->GreenOffset);
    RenderPlayer(Buffer, GameState->PlayerX, GameState->PlayerY);
}


GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *GameState = (game_state*)Memory->PermanentStorage;
    GameOutputSound(GameState, SoundBuffer);
}
