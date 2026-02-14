#include <stdint.h>
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"
#include "fastExpLog.h"
#include "romfunc.h"

/*
 * Noise Gate Effect
 * State machine: Closed -> Attack -> Open -> Hold -> Release -> Closed
 * Uses envelope follower from compressor library for level detection.
 * Smooth gain ramping prevents clicks at gate transitions.
 * Parameters: Threshold, Attack, Release, Hold
 */

static int16_t fxProgram17processSample(int16_t sampleIn, void* data)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    int16_t absSample;
    int16_t envelope;
    int32_t sampleOut;

    // Get absolute value for envelope follower
    absSample = sampleIn < 0 ? -sampleIn : sampleIn;

    // Update envelope using dual-coefficient lowpass (fast attack, slow release)
    envelope = firstOrderIirDualCoeffLPProcessSample(absSample, &pData->envelopeFollower);
    pData->currentEnvelope = envelope;

    // Convert envelope to log domain for threshold comparison
    int16_t envLog = fastlog(envelope);

    // State machine
    switch (pData->gateState)
    {
        case 0: // Closed
            if (envLog > pData->threshold)
            {
                pData->gateState = 1; // -> Attack
            }
            else
            {
                // Ramp gain down
                pData->currentGain -= pData->releaseRate;
                if (pData->currentGain < 0)
                {
                    pData->currentGain = 0;
                }
            }
            break;

        case 1: // Attack
            pData->currentGain += pData->attackRate;
            if (pData->currentGain >= 32767)
            {
                pData->currentGain = 32767;
                pData->gateState = 2; // -> Open
            }
            break;

        case 2: // Open
            pData->currentGain = 32767;
            if (envLog < (pData->threshold - pData->hysteresis))
            {
                pData->holdCounter = pData->holdTime;
                pData->gateState = 3; // -> Hold
            }
            break;

        case 3: // Hold
            pData->currentGain = 32767;
            if (envLog > pData->threshold)
            {
                pData->gateState = 2; // -> back to Open
            }
            else if (pData->holdCounter > 0)
            {
                pData->holdCounter--;
            }
            else
            {
                pData->gateState = 4; // -> Release
            }
            break;

        case 4: // Release
            if (envLog > pData->threshold)
            {
                pData->gateState = 1; // -> re-Attack
            }
            else
            {
                pData->currentGain -= pData->releaseRate;
                if (pData->currentGain <= 0)
                {
                    pData->currentGain = 0;
                    pData->gateState = 0; // -> Closed
                }
            }
            break;

        default:
            pData->gateState = 0;
            break;
    }

    // Apply gain
    sampleOut = ((int32_t)sampleIn * pData->currentGain) >> 15;
    return (int16_t)sampleOut;
}

static void fxProgram17Param1Callback(uint16_t val, void* data) // Threshold
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    fxProgram17.parameters[0].rawValue = val;
    // Map 0-4095 to threshold in log domain (0 = very sensitive, 4095 = high threshold)
    // fastlog range is roughly 0..32767
    pData->threshold = val << 3; // 0-32767
}

static void fxProgram17Param1Display(void* data, char* res)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    int16_t dbval = asDb(fastexp(pData->threshold));
    decimalInt16ToChar(dbval, res, 1);
    appendToString(res, " dB");
}

static void fxProgram17Param2Callback(uint16_t val, void* data) // Attack
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    fxProgram17.parameters[1].rawValue = val;
    // Map 0-4095 to attack rate
    // At 48kHz, 1ms = 48 samples. Gain range is 32767.
    // Fast attack = high rate, slow attack = low rate
    // rate = 32767 / (attack_ms * 48)
    // Map val to 0.1ms (rate=6826) to 10ms (rate=68)
    int32_t attackSamples = 5 + (((int32_t)val * 475) >> 12); // 5 to 480 samples (0.1ms to 10ms)
    pData->attackRate = (int16_t)(32767 / attackSamples);
    if (pData->attackRate < 1) pData->attackRate = 1;

    // Also set envelope follower attack coefficient
    pData->envelopeFollower.alphaRising = (1 << 15) - 2 - (val >> 6);
}

static void fxProgram17Param2Display(void* data, char* res)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    // Calculate attack time from rate: time_ms = 32767 / (rate * 48) * 1000
    int32_t attackMs = (int32_t)32767 * 10 / ((int32_t)pData->attackRate * 48);
    if (attackMs > 999) attackMs = 999;
    decimalInt16ToChar((int16_t)attackMs, res, 1);
    appendToString(res, " ms");
}

static void fxProgram17Param3Callback(uint16_t val, void* data) // Release
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    fxProgram17.parameters[2].rawValue = val;
    // Map 0-4095 to release rate
    // Map to 10ms (rate=68) to 500ms (rate=1.4)
    int32_t releaseSamples = 480 + (((int32_t)val * 23520) >> 12); // 480 to 24000 samples (10ms to 500ms)
    pData->releaseRate = (int16_t)(32767 / releaseSamples);
    if (pData->releaseRate < 1) pData->releaseRate = 1;

    // Also set envelope follower release coefficient
    pData->envelopeFollower.alphaFalling = (1 << 15) - 2 - (val >> 6);
}

static void fxProgram17Param3Display(void* data, char* res)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    int32_t releaseMs = (int32_t)32767 * 10 / ((int32_t)pData->releaseRate * 48);
    if (releaseMs > 9999) releaseMs = 9999;
    Int16ToChar((int16_t)releaseMs, res);
    appendToString(res, " ms");
}

static void fxProgram17Param4Callback(uint16_t val, void* data) // Hold
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    fxProgram17.parameters[3].rawValue = val;
    // Map 0-4095 to 0-24000 samples (0-500ms at 48kHz)
    pData->holdTime = (uint16_t)(((uint32_t)val * 24000) >> 12);
}

static void fxProgram17Param4Display(void* data, char* res)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    uint16_t holdMs = pData->holdTime / 48;
    Int16ToChar((int16_t)holdMs, res);
    appendToString(res, " ms");
}

static void fxProgram17Setup(void* data)
{
    (void)data;
    // Initial state set via static initializer
}

static void fxProgram17Reset(void* data)
{
    FxProgram17DataType* pData = (FxProgram17DataType*)data;
    firstOrderIirDualCoeffLPReset(&pData->envelopeFollower);
    pData->currentEnvelope = 0;
    pData->currentGain = 0;
    pData->gateState = 0;
    pData->holdCounter = 0;
}

FxProgram17DataType fxProgram17data = {
    .envelopeFollower = {
        .alphaRising = 32000,     // fast attack for envelope detection
        .alphaFalling = 32700,    // slower release for natural feel
        .oldVal = 0,
        .oldXVal = 0
    },
    .currentEnvelope = 0,
    .threshold = 8000,      // moderate sensitivity
    .hysteresis = 2000,     // 6dB hysteresis
    .currentGain = 0,
    .attackRate = 683,      // ~1ms attack
    .releaseRate = 3,       // ~200ms release
    .holdCounter = 0,
    .holdTime = 4800,       // 100ms hold
    .gateState = 0
};

FxProgramType fxProgram17 = {
    .name = "Noise Gate",
    .nParameters = 4,
    .parameters = {
        {
            .name = "Threshold      ",
            .control = 0,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram17Param1Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram17Param1Callback
        },
        {
            .name = "Attack         ",
            .control = 1,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram17Param2Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram17Param2Callback
        },
        {
            .name = "Release        ",
            .control = 2,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram17Param3Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram17Param3Callback
        },
        {
            .name = "Hold           ",
            .control = 0xFF,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram17Param4Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram17Param4Callback
        }
    },
    .processSample = &fxProgram17processSample,
    .setup = &fxProgram17Setup,
    .reset = &fxProgram17Reset,
    .data = (void*)&fxProgram17data
};
