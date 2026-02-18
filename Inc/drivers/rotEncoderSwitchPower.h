#ifndef _ROTARY_ENCODER_H_
#define _ROTARY_ENCODER_H_
#include <stdint.h>

#define ROTARY_ENCODER_DEBOUNCE 1
#define ROTARY_ENCODER_STEPS_PER_DETENT 2
#define ROTARY_ENCODER_MIN_TRANSITION_US 120


void initRotaryEncoder(const uint8_t* pins,const uint8_t nswitches);

uint32_t getEncoderValue();

uint8_t getSwitchValue(uint8_t);

void clearPressedStickyBit(uint8_t nr);

void clearReleasedStickyBit(uint8_t nr);

int16_t getStickyIncrementDelta();

int16_t consumeStickyIncrementDelta();

void clearStickyIncrementDelta();

uint32_t getRotaryDeltaT();

#endif

