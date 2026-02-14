#ifndef _REVERB_UTILS_C_
#define _REVERB_UTILS_C_
#include "stdint.h"

typedef struct 
{
    int16_t coefficient;
    uint16_t delayPtr;
    uint16_t delayInSamples;
    int16_t oldValues;
    uint16_t bufferSize; 
    int16_t * delayLineIn;
    int16_t * delayLineOut;
} AllpassType;

typedef struct 
{
    int16_t * delayPointers[4];
    int16_t delayTimes[4];
    int16_t delayPointer;
    uint16_t diffusorSize;
} HadamardDiffuserType;

#ifdef RP2040_FEATHER
#include "audio/audiotools.h"
static inline __attribute__((always_inline)) int16_t allpassProcessSample(int16_t sampleIn,AllpassType*allpass,volatile uint32_t * audioStatePtr)
{
    int16_t sampleOut;
    int32_t sampleInterm;
    sampleInterm =  ((allpass->coefficient*sampleIn) >> 15) + *(allpass->delayLineIn + 
                    ((allpass->delayPtr - allpass->delayInSamples) & allpass->bufferSize)) -
                    ((*(allpass->delayLineOut + ((allpass->delayPtr - allpass->delayInSamples) & allpass->bufferSize))*allpass->coefficient) >> 15);  
    sampleInterm=clip(sampleInterm,audioStatePtr);
    sampleOut = (int16_t)sampleInterm;
    *(allpass->delayLineIn + allpass->delayPtr) = sampleIn;
    *(allpass->delayLineOut + allpass->delayPtr) = sampleOut;
    allpass->delayPtr++;
    allpass->delayPtr &= allpass->bufferSize;
    return sampleOut;
}
#else
int16_t  allpassProcessSample(int16_t sampleIn,AllpassType*allpass,volatile uint32_t*);
#endif

void hadamardDiffuserProcessArray(int32_t * channels,HadamardDiffuserType*data,volatile uint32_t * audioStatePtr);

#endif