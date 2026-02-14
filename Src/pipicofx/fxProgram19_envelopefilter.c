#include <stdint.h>
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"
#include "romfunc.h"

/*
 * Envelope Filter / Auto-Wah Effect
 * A bandpass filter whose center frequency is controlled by the input
 * signal's envelope (dynamics). Playing harder sweeps the filter up;
 * soft playing lets it fall. Classic funk/touch-wah sound.
 *
 * Uses a dynamic biquad (non-const coefficients) so the bandpass can
 * be swept in real-time. Coefficients are recalculated every 16 samples
 * to save CPU while maintaining smooth sweeps.
 *
 * Biquad bandpass coefficients (Q14 fixed-point):
 *   alpha = sin(w0) / (2*Q)
 *   b0 = alpha,  b1 = 0,  b2 = -alpha
 *   a0 = 1 + alpha,  a1 = -2*cos(w0),  a2 = 1 - alpha
 *   Normalized: divide all by a0.
 *   In Q14: multiply by 16384.
 */

/* Precomputed sin/cos table for bandpass coefficient calculation.
 * Index = frequency / 100 Hz, range 100-4800 Hz (indices 1-48).
 * Values in Q15 (multiply by 1/32768 to get float).
 * sin(2*pi*f/48000) and cos(2*pi*f/48000) for f = index*100 */

#define COEFF_TABLE_SIZE 49

/* sin(2*pi*freq/48000) in Q15, freq = index * 100 Hz */
static const int16_t sinTable[COEFF_TABLE_SIZE] = {
    0,    428,   857,  1285,  1713,  2139,  2563,  2986,
    3406, 3823,  4236, 4647,  5053,  5453,  5850,  6240,
    6624, 7001,  7371, 7733,  8088,  8434,  8772,  9102,
    9422, 9733, 10035,10327, 10610, 10883, 11146, 11399,
   11641,11874, 12096,12307, 12508, 12698, 12878, 13047,
   13205,13352, 13489,13614, 13729, 13833, 13926, 14009,
   14081
};

/* cos(2*pi*freq/48000) in Q15 */
static const int16_t cosTable[COEFF_TABLE_SIZE] = {
   32767,32764, 32756,32741, 32720, 32693, 32660, 32621,
   32575,32523, 32465,32401, 32331, 32255, 32172, 32084,
   31990,31889, 31783,31670, 31552, 31428, 31298, 31163,
   31022,30875, 30723,30565, 30402, 30233, 30059, 29880,
   29696,29507, 29312,29113, 28909, 28700, 28487, 28269,
   28047,27821, 27590,27355, 27117, 26874, 26627, 26377,
   26123
};

static void updateBandpassCoeffs(FxProgram19DataType* pData, int16_t freqIndex)
{
    int32_t sinW0, cosW0, alpha;
    int32_t a0;

    // Clamp frequency index
    if (freqIndex < 1) freqIndex = 1;
    if (freqIndex >= COEFF_TABLE_SIZE) freqIndex = COEFF_TABLE_SIZE - 1;

    sinW0 = sinTable[freqIndex];
    cosW0 = cosTable[freqIndex];

    // alpha = sin(w0) / (2*Q), Q is in Q8 format (256 = Q of 1.0)
    // alpha_q15 = sinW0 * 128 / Q  (since 2*Q_q8 = Q_q9, and we want Q15)
    if (pData->q < 64) pData->q = 64; // minimum Q of 0.25
    alpha = ((int32_t)sinW0 << 7) / pData->q;
    if (alpha > 32767) alpha = 32767;

    // a0 = 1 + alpha (in Q15: 32768 + alpha — but we use Q14 for coefficients)
    a0 = (1 << 14) + (alpha >> 1); // Q14

    if (a0 == 0) a0 = 1; // prevent division by zero

    // Bandpass coefficients (normalized by a0), stored in Q14:
    // b0 = alpha / a0,  b1 = 0,  b2 = -alpha / a0
    // a1 = -2*cos(w0) / a0,  a2 = (1 - alpha) / a0
    pData->coeffB[0] = ((alpha >> 1) << 14) / a0;        // b0 = alpha (Q14)
    pData->coeffB[1] = 0;                                  // b1 = 0
    pData->coeffB[2] = -pData->coeffB[0];                  // b2 = -alpha

    pData->coeffA[0] = ((-cosW0 >> 1) << 14) / a0;        // a1 = -2*cos(w0) (actually -2*cos already factored)
    // More precisely: a1 = -2*cosW0 (Q15), need in Q14 normalized by a0
    pData->coeffA[0] = (int32_t)(-(int32_t)cosW0 << 14) / a0; // -2*cos(w0)/a0 in Q14
    pData->coeffA[1] = ((int32_t)((1 << 14) - (alpha >> 1)) << 14) / a0; // (1 - alpha)/a0
}

static int16_t fxProgram19processSample(int16_t sampleIn, void* data)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    int16_t absSample;
    int16_t envelope;
    int32_t freqRange;
    int16_t freqIndex;
    int16_t wet, res;
    int32_t sampleOut;

    // Envelope follower
    absSample = sampleIn < 0 ? -sampleIn : sampleIn;
    envelope = firstOrderIirDualCoeffLPProcessSample(absSample, &pData->envelopeFollower);
    pData->currentEnvelope = envelope;

    // Update filter coefficients every 16 samples to save CPU
    pData->coeffUpdateCnt++;
    if (pData->coeffUpdateCnt >= 16)
    {
        pData->coeffUpdateCnt = 0;

        // Map envelope (0..32767) to frequency index
        // freq = minFreq + envelope * sensitivity * (maxFreq - minFreq) / 32767
        freqRange = (int32_t)pData->maxFreq - (int32_t)pData->minFreq;
        freqIndex = pData->minFreq +
                    (int16_t)(((int32_t)envelope * pData->sensitivity / 32767 * freqRange) >> 8);
        if (freqIndex < 1) freqIndex = 1;
        if (freqIndex >= COEFF_TABLE_SIZE) freqIndex = COEFF_TABLE_SIZE - 1;

        updateBandpassCoeffs(pData, freqIndex);
    }

    // Process through dynamic biquad (same math as SecondOrderIirFilterType)
    pData->acc += pData->coeffB[0] * (int32_t)sampleIn;
    pData->acc += pData->coeffB[1] * (int32_t)pData->x1;
    pData->acc += pData->coeffB[2] * (int32_t)pData->x2;
    pData->acc -= pData->coeffA[0] * (int32_t)pData->y1;
    pData->acc -= pData->coeffA[1] * (int32_t)pData->y2;
    if (pData->acc > ((1 << 29) - 1))
    {
        pData->acc = ((1 << 29) - 1);
    }
    if (pData->acc < -(1 << 29))
    {
        pData->acc = -(1 << 29);
    }
    res = (int16_t)(pData->acc >> 14);
    pData->x2 = pData->x1;
    pData->x1 = sampleIn;
    pData->y2 = pData->y1;
    pData->y1 = res;
    pData->acc &= ((1 << 14) - 1);
    wet = res;

    // Mix dry/wet
    sampleOut = (((int32_t)(32767 - pData->mix) * sampleIn) >> 15) +
                (((int32_t)pData->mix * wet) >> 15);
    if (sampleOut > 32767) sampleOut = 32767;
    if (sampleOut < -32768) sampleOut = -32768;

    return (int16_t)sampleOut;
}

static void fxProgram19Param1Callback(uint16_t val, void* data) // Sensitivity
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    fxProgram19.parameters[0].rawValue = val;
    // Map 0-4095 to 0-256 (sensitivity scaling factor, Q8)
    pData->sensitivity = (val >> 4); // 0-255
}

static void fxProgram19Param1Display(void* data, char* res)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    int16_t pct = (int16_t)(((uint32_t)pData->sensitivity * 100) >> 8);
    Int16ToChar(pct, res);
    appendToString(res, "%");
}

static void fxProgram19Param2Callback(uint16_t val, void* data) // Range
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    fxProgram19.parameters[1].rawValue = val;
    // Map 0-4095 to a sweep range
    // minFreq stays at 2 (200 Hz), maxFreq goes from 5 (500 Hz) to 40 (4000 Hz)
    pData->maxFreq = 5 + (int16_t)(((uint32_t)val * 35) >> 12);
}

static void fxProgram19Param2Display(void* data, char* res)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    Int16ToChar(pData->maxFreq * 100, res);
    appendToString(res, " Hz");
}

static void fxProgram19Param3Callback(uint16_t val, void* data) // Resonance (Q)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    fxProgram19.parameters[2].rawValue = val;
    // Map 0-4095 to Q in Q8 format: 64 (Q=0.25) to 2560 (Q=10.0)
    pData->q = 64 + (int16_t)(((uint32_t)val * 2496) >> 12);
}

static void fxProgram19Param3Display(void* data, char* res)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    decimalInt16ToChar((int16_t)(((int32_t)pData->q * 100) >> 8), res, 2);
}

static void fxProgram19Param4Callback(uint16_t val, void* data) // Mix
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    fxProgram19.parameters[3].rawValue = val;
    pData->mix = val << 3; // 0-32767
}

static void fxProgram19Param4Display(void* data, char* res)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    Int16ToChar(pData->mix / 328, res);
    appendToString(res, "%");
}

static void fxProgram19Setup(void* data)
{
    (void)data;
}

static void fxProgram19Reset(void* data)
{
    FxProgram19DataType* pData = (FxProgram19DataType*)data;
    pData->x1 = 0;
    pData->x2 = 0;
    pData->y1 = 0;
    pData->y2 = 0;
    pData->acc = 0;
    firstOrderIirDualCoeffLPReset(&pData->envelopeFollower);
    pData->currentEnvelope = 0;
    pData->coeffUpdateCnt = 0;
}

FxProgram19DataType fxProgram19data = {
    .x1 = 0, .x2 = 0, .y1 = 0, .y2 = 0, .acc = 0,
    .coeffA = {0, 0},
    .coeffB = {0, 0, 0},
    .envelopeFollower = {
        .alphaRising = 32600,   // fast attack
        .alphaFalling = 32750,  // moderate release
        .oldVal = 0,
        .oldXVal = 0
    },
    .currentEnvelope = 0,
    .sensitivity = 200,  // ~78%
    .minFreq = 2,        // 200 Hz
    .maxFreq = 25,       // 2500 Hz
    .q = 512,            // Q = 2.0
    .mix = 24576,        // 75%
    .coeffUpdateCnt = 0
};

FxProgramType fxProgram19 = {
    .name = "Envelope Filter",
    .nParameters = 4,
    .parameters = {
        {
            .name = "Sensitivity    ",
            .control = 0,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram19Param1Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram19Param1Callback
        },
        {
            .name = "Range          ",
            .control = 1,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram19Param2Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram19Param2Callback
        },
        {
            .name = "Resonance      ",
            .control = 2,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram19Param3Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram19Param3Callback
        },
        {
            .name = "Mix            ",
            .control = 0xFF,
            .increment = 32,
            .rawValue = 0,
            .getParameterDisplay = &fxProgram19Param4Display,
            .getParameterValue = 0,
            .setParameter = &fxProgram19Param4Callback
        }
    },
    .processSample = &fxProgram19processSample,
    .setup = &fxProgram19Setup,
    .reset = &fxProgram19Reset,
    .data = (void*)&fxProgram19data
};
