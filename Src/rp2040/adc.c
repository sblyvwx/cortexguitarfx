#include "drivers/adc.h"
#include "drivers/dma.h"
#include "system.h"
#include "drivers/irq.h"
#include "hardware/regs/resets.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/adc.h"
#include "hardware/regs/clocks.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/m0plus.h"
#include "hardware/regs/dma.h" 
#include "hardware/rp2040_registers.h"

static uint16_t audioInDoubleBuffer[AUDIO_INPUT_BUFFER_SIZE*2];
static volatile  uint32_t dbfrPtr; 
volatile uint32_t audioInputState;

extern uint32_t task;

volatile uint16_t adcChannel0Value, adcChannel1Value, adcChannel2Value;

static inline void configureAdcChannelPin(uint8_t channelnr)
{
    volatile uint32_t* adcPinCtrl;
    volatile uint32_t* adcPadCtrl;

    // ADC channel 0..3 are GPIO26..29
    adcPinCtrl = (volatile uint32_t*)(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + 8 * (26 + channelnr));
    *adcPinCtrl = (*adcPinCtrl & ~(IO_BANK0_GPIO0_CTRL_FUNCSEL_BITS))
        | (IO_BANK0_GPIO0_CTRL_FUNCSEL_VALUE_NULL << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB);

    // Disable pulls and digital input buffer, disable output driver for analog operation
    adcPadCtrl = (PADS_ADC0 + channelnr);
    *adcPadCtrl &= ~((1 << PADS_BANK0_GPIO26_PUE_LSB) | (1 << PADS_BANK0_GPIO26_PDE_LSB) | (1 << PADS_BANK0_GPIO26_IE_LSB));
    *adcPadCtrl |= (1 << PADS_BANK0_GPIO26_OD_LSB);
}

void isr_c1_adc_fifo_irq22()
{
    adcChannel0Value = *ADC_FIFO;
    adcChannel1Value = *ADC_FIFO;
    adcChannel2Value = *ADC_FIFO;
    task |= (1 << TASK_UPDATE_POTENTIOMETER_VALUES);
}

uint16_t getChannel0Value()
{
    return adcChannel0Value;
}

uint16_t getChannel1Value()
{
    return adcChannel1Value;
}
uint16_t getChannel2Value()
{
    return adcChannel2Value;
}
/**
 * @brief run on core 1, just sets the irq for pio1, sm0 so that core1 one can handle this interrupt without being affected by other irq's
 *   
 */
void core1IrqSync()
{
	*NVIC_ISER = (1 << 9);
    while(1);
}

/**
 * @brief initializes the adc, the usb pll must be on before the initialization can take place
 * 
 */
void initAdc()
{
    // enable the adc clock
    *CLK_ADC_CTRL |= (1 << CLOCKS_CLK_ADC_CTRL_ENABLE_LSB);

    // de-reset adc block
    *RESETS |= (1 << RESETS_RESET_ADC_LSB);
    *RESETS &= ~(1 << RESETS_RESET_ADC_LSB);
    while ((*RESETS_DONE & (1 << RESETS_RESET_ADC_LSB)) == 0);

    // enable adc
    *ADC_CS |= (1 << ADC_CS_EN_LSB);
}

/**
 * @brief reads the given adc channel (0 to 3)
 * 
 * @param channelnr the selected channel, 0 is GPIO26 up to 3 /GPIO29
 * @return uint16_t the 12-bit raw adc result
 */
uint16_t readChannel(uint8_t channelnr)
{   
    uint32_t adcChannelPadSettings;
    uint16_t res;
    adcChannelPadSettings = *(PADS_ADC0 + channelnr);
 // set IE high and OD low for the pad register of the channel
    *(PADS_ADC0 + channelnr) |= (1 << PADS_BANK0_GPIO26_IE_LSB);
    *(PADS_ADC0 + channelnr) &= ~(1 << PADS_BANK0_GPIO26_OD_LSB);

    while((*ADC_CS & (1 << ADC_CS_READY_LSB))==0);
    *ADC_CS |= (channelnr << ADC_CS_AINSEL_LSB); // choose channel

    // start the conversion
    *ADC_CS |= (1 << ADC_CS_START_ONCE_LSB);

    // wait until ready
    while((*ADC_CS & (1 << ADC_CS_READY_LSB))==0);

    // read the result
    res= (uint16_t)(*ADC_RESULT & 0xFFF);

    // reset the state of the pad
    *(PADS_ADC0 + channelnr) = adcChannelPadSettings;
    return res;
}

void initDoubleBufferedReading(uint8_t channelnr)
{
    // setup analog input pad
    configureAdcChannelPin(channelnr);

    // Run ADC at ADC_OVERSAMPLE_FACTOR × the I2S sample rate so that
    // we can average multiple raw readings per audio frame, lowering
    // the effective noise floor (about +3 dB for each doubling).
    // I2S rate: fs = F_SYS / (I2S_CLKDIV_DBL_INT*256 + I2S_CLKDIV_DBL_FRAC)
    {
        uint32_t i2sSamplingRate;
        i2sSamplingRate = F_SYS / ((I2S_CLKDIV_DBL_INT << 8) + I2S_CLKDIV_DBL_FRAC);
        *ADC_DIV = ((F_ADC_USB / (i2sSamplingRate * ADC_OVERSAMPLE_FACTOR)) - 1) << 8;
    }

    // ensure single selected channel (disable round-robin here)
    *ADC_CS &= ~(0x1F << ADC_CS_RROBIN_LSB);
    *ADC_CS = (*ADC_CS & ~(0x7 << ADC_CS_AINSEL_LSB)) | ((channelnr & 0x7) << ADC_CS_AINSEL_LSB);

    // enable fifo and dreq and set thresh to 1
    *ADC_FCS = (1 << ADC_FCS_EN_LSB) | (1 << ADC_FCS_DREQ_EN_LSB) | (1 << ADC_FCS_THRESH_LSB); 

    // enable irq for channel 3
    *DMA_INTE0 |= (1 << 3);

    // initialize DMA
	*DMA_CH3_READ_ADDR = (uint32_t)ADC_FIFO;
	dbfrPtr = 0;
	*DMA_CH3_WRITE_ADDR = dbfrPtr + (uint32_t)audioInDoubleBuffer;
	*DMA_CH3_TRANS_COUNT = AUDIO_INPUT_BUFFER_SIZE;
	*DMA_CH3_CTRL_TRIG = (36 << DMA_CH3_CTRL_TRIG_TREQ_SEL_LSB) 
						| (1 << DMA_CH3_CTRL_TRIG_INCR_WRITE_LSB) 
						| (1 << DMA_CH3_CTRL_TRIG_DATA_SIZE_LSB) // adc is single channel 16 bit
						| (0 << DMA_CH3_CTRL_TRIG_EN_LSB);
}


void initRoundRobinReading()
{
    // setup analog input pads
    configureAdcChannelPin(0);
    configureAdcChannelPin(1);
    configureAdcChannelPin(2);

    // set update frequency
    *ADC_DIV= 0xFFFF << 8; //((F_ADC_USB/(UI_UPDATE_RATE*3)) - 1) << 8; 

    // set threshhold to 3
    *ADC_FCS = (1 << ADC_FCS_EN_LSB) | (3 << ADC_FCS_THRESH_LSB); 

    // enable interrupt
    *ADC_INTE = (1 << ADC_INTE_FIFO_LSB);

    // enable global interrupt (handled by core 0)
    *NVIC_ISER = (1 << 22);
    setInterruptPriority(22,1);


    // set round robin for channels 0 to 2
    *ADC_CS |= ((1 << 0) << ADC_CS_RROBIN_LSB) | ((1 << 1) << ADC_CS_RROBIN_LSB) | ((1 << 2) << ADC_CS_RROBIN_LSB);

    // start reading
    *ADC_CS |= (1 << ADC_CS_START_MANY_LSB); 
}

void enableAudioInput(uint8_t freeRunning)
{
    *DMA_CH3_TRANS_COUNT = AUDIO_INPUT_BUFFER_SIZE;
    dbfrPtr = 0;
	*DMA_CH3_WRITE_ADDR = dbfrPtr + (uint32_t)audioInDoubleBuffer;
    if (freeRunning == 1)
    {
        *ADC_CS |= (1 << ADC_CS_START_MANY_LSB); 
    }
    *DMA_CH3_CTRL_TRIG |= (1 << DMA_CH3_CTRL_TRIG_EN_LSB);
}

void startConversion()
{
    *ADC_CS |= (1 << ADC_CS_START_ONCE_LSB); 
}

#ifndef I2S_INPUT
void toggleAudioInputBuffer()
{
    dbfrPtr += AUDIO_INPUT_BUFFER_SIZE*2;
	dbfrPtr &= (AUDIO_INPUT_BUFFER_SIZE*2*2-1);
	*DMA_CH3_WRITE_ADDR = dbfrPtr + (uint32_t)audioInDoubleBuffer;
    *DMA_CH3_TRANS_COUNT_TRIG = AUDIO_INPUT_BUFFER_SIZE; // write to alias 1 to trigger dma on writing transmission count
}
#endif

uint16_t* getReadableAudioBuffer()
{
    uint16_t * otherBuffer;
	otherBuffer = (uint16_t*)(((dbfrPtr + AUDIO_INPUT_BUFFER_SIZE*2) & (AUDIO_INPUT_BUFFER_SIZE*2*2-1)) + (uint32_t)audioInDoubleBuffer);
	return otherBuffer;
}
