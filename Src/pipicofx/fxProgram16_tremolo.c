#include <stdint.h>
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"
#include "audio/sineChorus.h"

/*
 * Tremolo Effect
 * Amplitude modulation using an LFO (Sine, Triangle, or Square waveform).
 * Parameters: Rate (LFO frequency), Depth (modulation amount), Waveform
 */

static int16_t fxProgram16processSample(int16_t sampleIn, void* data)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    int16_t lfoVal;
    int32_t gain;
    int32_t sampleOut;

    // Advance LFO phase
    pData->lfoPhase += pData->lfoPhaseinc;

    // Get LFO value based on waveform type
    switch (pData->waveform)
    {
        case 0: // Sine: use getSineValue which returns -255..+255
            lfoVal = getSineValue(pData->lfoPhase);
            break;
        case 1: // Triangle: use quadrant-based triangle wave
        {
            uint8_t quadrant = pData->lfoPhase >> 30;
            uint16_t phase = (pData->lfoPhase >> 22) & 0xFF;
            if (quadrant == 0)
            {
                lfoVal = (int16_t)phase;
            }
            else if (quadrant == 1)
            {
                lfoVal = 255 - (int16_t)phase;
            }
            else if (quadrant == 2)
            {
                lfoVal = -(int16_t)phase;
            }
            else
            {
                lfoVal = -255 + (int16_t)phase;
            }
            break;
        }
        case 2: // Square
        default:
            lfoVal = (pData->lfoPhase >> 31) ? -255 : 255;
            break;
    }

    pData->lfoVal = lfoVal;

    // Map LFO value (-255..+255) to gain (0..32767)
    // At depth=0: gain is always 32767 (unity)
    // At depth=max: gain swings from 0 to 32767
    // gain = 32767 - (depth/4095) * ((255 - lfoVal)/2) * (32767/255)
    // Simplified: gain = 32767 - (depth * (255 - lfoVal)) >> 5  (approximately)
    gain = 32767 - (((int32_t)pData->depth * (255 - lfoVal)) >> 5);
    if (gain < 0)
    {
        gain = 0;
    }
    if (gain > 32767)
    {
        gain = 32767;
    }

    sampleOut = ((int32_t)sampleIn * gain) >> 15;
    return (int16_t)sampleOut;
}

static void fxProgram16Param1Callback(uint16_t val, void* data) // Rate
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    uint16_t freq;
    fxProgram16.parameters[0].rawValue = val;
    // Map 0-4095 to 10-1000 (0.1 Hz to 10.0 Hz in Hz/100)
    freq = ((uint32_t)val * 990 >> 12) + 10;
    pData->lfoPhaseinc = freq * 894; // same formula as sineChorusSetFrequency
}

static void fxProgram16Param1Display(void* data, char* res)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    // Recover frequency from lfoPhaseinc
    uint16_t freq = (uint16_t)(pData->lfoPhaseinc / 894);
    decimalInt16ToChar((int16_t)freq, res, 2);
    appendToString(res, " Hz");
}

static void fxProgram16Param2Callback(uint16_t val, void* data) // Depth
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    fxProgram16.parameters[1].rawValue = val;
    pData->depth = val; // 0-4095
}

static void fxProgram16Param2Display(void* data, char* res)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    int16_t pct = (int16_t)(((uint32_t)pData->depth * 100) >> 12);
    Int16ToChar(pct, res);
    appendToString(res, "%");
}

static void fxProgram16Param3Callback(uint16_t val, void* data) // Waveform
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    fxProgram16.parameters[2].rawValue = val;
    uint8_t wf = val >> 10; // 0-3, clamp to 0-2
    if (wf > 2) wf = 2;
    pData->waveform = wf;
}

static void fxProgram16Param3Display(void* data, char* res)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    *res = 0;
    switch (pData->waveform)
    {
        case 0:
            appendToString(res, "Sine");
            break;
        case 1:
            appendToString(res, "Triangle");
            break;
        case 2:
            appendToString(res, "Square");
            break;
    }
}

static void fxProgram16Setup(void* data)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    pData->lfoPhase = 0;
    pData->lfoPhaseinc = 500 * 894; // default 5 Hz
    pData->depth = 2048; // 50%
    pData->waveform = 0; // sine
    pData->lfoVal = 0;
    pData->lfoQuadrant = 0;
}

static void fxProgram16Reset(void* data)
{
    FxProgram16DataType* pData = (FxProgram16DataType*)data;
    pData->lfoPhase = 0;
    pData->lfoVal = 0;
    pData->lfoQuadrant = 0;
}

FxProgram16DataType fxProgram16data = {
    .lfoPhase = 0,
    .lfoPhaseinc = 447000, // ~5 Hz
    .depth = 2048,
    .lfoVal = 0,
    .waveform = 0,
    .lfoQuadrant = 0
};

FxProgramType fxProgram16 = {
    .name = "Tremolo",
    .nParameters = 3,
    .parameters = {
        {
            .name = "Rate           ",
            .control = 0,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram16Param1Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram16Param1Callback
        },
        {
            .name = "Depth          ",
            .control = 1,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram16Param2Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram16Param2Callback
        },
        {
            .name = "Waveform       ",
            .control = 2,
            .increment = 1024,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram16Param3Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram16Param3Callback
        }
    },
    .processSample = &fxProgram16processSample,
    .setup = &fxProgram16Setup,
    .reset = &fxProgram16Reset,
    .data = (void*)&fxProgram16data
};
