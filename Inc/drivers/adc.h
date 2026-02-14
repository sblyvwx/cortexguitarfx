#ifndef _ADC_H_
#define _ADC_H_
#include <stdint.h>

#define ADC_OVERSAMPLE_FACTOR 8
#define ADC_OVERSAMPLE_SHIFT 3
#define AUDIO_INPUT_BUFFER_SIZE (16 * ADC_OVERSAMPLE_FACTOR)
#define AUDIO_SAMPLING_RATE 48000
#define UI_UPDATE_RATE 10



void initAdc();
uint16_t readChannel(uint8_t channelnr);
void initDoubleBufferedReading(uint8_t channelnr);

void startConversion();
void core1IrqSync();

void enableAudioInput(uint8_t freeRunning);
#ifndef I2S_INPUT
void toggleAudioInputBuffer();
#endif
uint16_t * getReadableAudioBuffer();

void initRoundRobinReading();

uint16_t getChannel0Value();
uint16_t getChannel1Value();
uint16_t getChannel2Value();

#endif