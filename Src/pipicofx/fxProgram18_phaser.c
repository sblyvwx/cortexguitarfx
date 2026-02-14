#include <stdint.h>
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"
#include "audio/sineChorus.h"

/*
 * Phaser Effect
 * 4-8 cascaded first-order allpass filters with LFO-swept coefficients.
 * Allpass: y[n] = coeff * (x[n] - y[n-1]) + x[n-1]
 * When dry + wet are summed, the frequency-dependent phase shift creates
 * a comb filter with sweeping notches — the classic phaser sound.
 * Parameters: Rate, Depth, Feedback, Stages (4/6/8)
 */

/* First-order allpass inline: y = c*(x - y1) + x1 */
static inline int16_t allpassProcess(int16_t sampleIn, int16_t coeff,
                                      int16_t* x1, int16_t* y1)
{
    int32_t out;
    // y[n] = coeff * (x[n] - y[n-1]) + x[n-1]
    out = ((int32_t)coeff * ((int32_t)sampleIn - (int32_t)(*y1))) >> 15;
    out += *x1;

    // Clamp to int16_t range
    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;

    *x1 = sampleIn;
    *y1 = (int16_t)out;
    return (int16_t)out;
}

static int16_t fxProgram18processSample(int16_t sampleIn, void* data)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    int16_t lfoVal;
    int32_t coeff;
    int16_t wet;
    int32_t sampleOut;

    // Advance LFO
    pData->lfoPhase += pData->lfoPhaseinc;
    lfoVal = getSineValue(pData->lfoPhase); // -255..+255

    // Map LFO to allpass coefficient
    // coeff sweeps between minCoeff and maxCoeff
    // lfoVal range: -255..255, map to 0..510
    coeff = (int32_t)pData->minCoeff +
            ((((int32_t)pData->maxCoeff - (int32_t)pData->minCoeff) * (lfoVal + 255)) / 510);
    pData->coeff = (int16_t)coeff;

    // Mix in feedback from previous output
    int32_t input = sampleIn + ((pData->feedbackSample * (int32_t)pData->feedback) >> 15);
    // Clamp
    if (input > 32767) input = 32767;
    if (input < -32768) input = -32768;
    wet = (int16_t)input;

    // Cascade through allpass stages
    for (uint8_t s = 0; s < pData->nStages; s++)
    {
        wet = allpassProcess(wet, pData->coeff,
                             &pData->x1[s], &pData->y1[s]);
    }

    // Store feedback sample (before mixing with dry)
    pData->feedbackSample = wet;

    // Mix: phaser = dry + wet (additive mixing creates comb filter notches)
    sampleOut = (int32_t)sampleIn + (int32_t)wet;
    // Divide by 2 to prevent clipping (or let depth control the blend)
    sampleOut = (sampleOut * (int32_t)pData->depth) >> 15;
    // Add back some dry to maintain level
    sampleOut = ((int32_t)sampleIn * (32767 - pData->depth) >> 15) + sampleOut;

    if (sampleOut > 32767) sampleOut = 32767;
    if (sampleOut < -32768) sampleOut = -32768;

    return (int16_t)sampleOut;
}

static void fxProgram18Param1Callback(uint16_t val, void* data) // Rate
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    fxProgram18.parameters[0].rawValue = val;
    // Map 0-4095 to 5-500 (0.05 Hz to 5.0 Hz in Hz/100)
    uint16_t freq = ((uint32_t)val * 495 >> 12) + 5;
    pData->lfoPhaseinc = freq * 894;
}

static void fxProgram18Param1Display(void* data, char* res)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    uint16_t freq = (uint16_t)(pData->lfoPhaseinc / 894);
    decimalInt16ToChar((int16_t)freq, res, 2);
    appendToString(res, " Hz");
}

static void fxProgram18Param2Callback(uint16_t val, void* data) // Depth (wet/dry blend)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    fxProgram18.parameters[1].rawValue = val;
    pData->depth = val << 3; // 0-32767
}

static void fxProgram18Param2Display(void* data, char* res)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    Int16ToChar(pData->depth / 328, res);
    appendToString(res, "%");
}

static void fxProgram18Param3Callback(uint16_t val, void* data) // Feedback
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    fxProgram18.parameters[2].rawValue = val;
    // Map 0-4095 to -32767..+32767 (centered at 2048 = no feedback)
    pData->feedback = ((int32_t)val - 2048) << 4;
    if (pData->feedback > 32000) pData->feedback = 32000;   // limit to prevent instability
    if (pData->feedback < -32000) pData->feedback = -32000;
}

static void fxProgram18Param3Display(void* data, char* res)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    Int16ToChar(pData->feedback / 328, res);
    appendToString(res, "%");
}

static void fxProgram18Param4Callback(uint16_t val, void* data) // Stages
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    fxProgram18.parameters[3].rawValue = val;
    // Map to 4, 6, or 8 stages
    uint8_t sel = val / 1366; // 0, 1, or 2
    if (sel == 0) pData->nStages = 4;
    else if (sel == 1) pData->nStages = 6;
    else pData->nStages = 8;
}

static void fxProgram18Param4Display(void* data, char* res)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    *res = 0;
    switch (pData->nStages)
    {
        case 4: appendToString(res, "4 stages"); break;
        case 6: appendToString(res, "6 stages"); break;
        case 8: appendToString(res, "8 stages"); break;
    }
}

static void fxProgram18Setup(void* data)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    for (uint8_t s = 0; s < 8; s++)
    {
        pData->x1[s] = 0;
        pData->y1[s] = 0;
    }
    pData->feedbackSample = 0;
    pData->lfoPhase = 0;
}

static void fxProgram18Reset(void* data)
{
    FxProgram18DataType* pData = (FxProgram18DataType*)data;
    for (uint8_t s = 0; s < 8; s++)
    {
        pData->x1[s] = 0;
        pData->y1[s] = 0;
    }
    pData->feedbackSample = 0;
    pData->lfoPhase = 0;
}

FxProgram18DataType fxProgram18data = {
    .x1 = {0},
    .y1 = {0},
    .coeff = 0,
    .nStages = 4,
    .lfoPhase = 0,
    .lfoPhaseinc = 89400,  // ~1 Hz
    .depth = 16384,         // 50%
    .feedback = 0,
    .feedbackSample = 0,
    .minCoeff = 3000,       // allpass coefficient sweep range
    .maxCoeff = 28000       // determines notch frequency range
};

FxProgramType fxProgram18 = {
    .name = "Phaser",
    .nParameters = 4,
    .parameters = {
        {
            .name = "Rate           ",
            .control = 0,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram18Param1Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram18Param1Callback
        },
        {
            .name = "Depth          ",
            .control = 1,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram18Param2Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram18Param2Callback
        },
        {
            .name = "Feedback       ",
            .control = 2,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram18Param3Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram18Param3Callback
        },
        {
            .name = "Stages         ",
            .control = 0xFF,
            .increment = 1024,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram18Param4Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram18Param4Callback
        }
    },
    .processSample = &fxProgram18processSample,
    .setup = &fxProgram18Setup,
    .reset = &fxProgram18Reset,
    .data = (void*)&fxProgram18data
};
