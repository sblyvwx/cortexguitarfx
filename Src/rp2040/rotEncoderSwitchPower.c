
#include "drivers/rotEncoderSwitchPower.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/m0plus.h"
#include "hardware/regs/sio.h"
#include "hardware/rp2040_registers.h"
#include "drivers/cs4270_audio_codec.h"
#include "drivers/timer.h"
#include "drivers/systick.h"
#include "drivers/irq.h"

static volatile uint32_t encoderVal = 0x7FFFFFFF;
static volatile int16_t encoderStickyIncrement=0;
static volatile int16_t encoderStickyDecrement=0;
static volatile uint32_t currentUsVal;
static volatile uint32_t lastUsVal;
static volatile uint8_t switchPins[8];
static volatile uint8_t switchVals[8]; // bit 0: sticky bit set when button is pressed (chage from 0 to 1), bit 1: sticky bit set when button is released, bit 2: momentary value
static volatile uint32_t oldTickSwitches[8];
static volatile uint8_t switchCount;
static volatile uint8_t encoderPrevState;
static volatile int8_t encoderSubStep;
static volatile uint32_t lastEncoderTransitionUs;

static int8_t decodeQuadratureTransition(uint8_t prevState, uint8_t currentState)
{
    /* Gray-code transition table.
     * Positive sign is intentionally inverted versus raw A/B phase so that
     * clockwise turns map to increment on the current hardware wiring. */
    static const int8_t transitionLut[16] =
    {
         0,  1, -1,  0,
        -1,  0,  0,  1,
         1,  0,  0, -1,
         0, -1,  1,  0
    };
    return (int8_t)-transitionLut[(prevState << 2) | currentState];
}

static uint8_t readEncoderState()
{
    uint8_t state = 0;
    if ((*GPIO_IN & (1 << ENCODER_1)) != 0)
    {
        state |= (1 << 1);
    }
    if ((*GPIO_IN & (1 << ENCODER_2)) != 0)
    {
        state |= 1;
    }
    return state;
}

void isr_c1_io_irq_bank0_irq13()
{
    uint32_t* switchIntAddress;
    uint32_t irqMask;
    uint32_t encoderIrqFlags;
    uint32_t tickNow;
    uint8_t currentState;
    uint32_t nowUs;
    int8_t transition;

    irqMask = (1 << ENCODER_1_EDGE_HIGH) | (1 << ENCODER_1_EDGE_LOW) | (1 << ENCODER_2_EDGE_HIGH) | (1 << ENCODER_2_EDGE_LOW);
    encoderIrqFlags = (*ENCODER_1_INTR & irqMask);
    if (encoderIrqFlags != 0)
    {
        *ENCODER_1_INTR |= encoderIrqFlags;
        currentState = readEncoderState();
        nowUs = getTimeLW();
        if ((uint32_t)(nowUs - lastEncoderTransitionUs) >= ROTARY_ENCODER_MIN_TRANSITION_US)
        {
            transition = decodeQuadratureTransition(encoderPrevState,currentState);
            encoderPrevState = currentState;
            if (transition != 0)
            {
                encoderVal += (int32_t)transition;
                encoderSubStep += transition;
                if (encoderSubStep >= ROTARY_ENCODER_STEPS_PER_DETENT)
                {
                    encoderStickyIncrement++;
                    encoderSubStep = 0;
                    lastUsVal = currentUsVal;
                    currentUsVal = nowUs;
                }
                else if (encoderSubStep <= -ROTARY_ENCODER_STEPS_PER_DETENT)
                {
                    encoderStickyDecrement++;
                    encoderSubStep = 0;
                    lastUsVal = currentUsVal;
                    currentUsVal = nowUs;
                }
            }
            lastEncoderTransitionUs = nowUs;
        }
        else
        {
            encoderPrevState = currentState;
        }
    }

    if ((*POWERSENSE_INTR & (1 << POWERSENSE_EDGE_LOW)) == (1 << POWERSENSE_EDGE_LOW))
    {
        *POWERSENSE_INTR |= (1 << POWERSENSE_EDGE_LOW);
        cs4270PowerDown();
    }

    // UI switches 
    for (uint8_t c=0;c<switchCount;c++)
    {
        switchIntAddress = (uint32_t*)(IO_BANK0_BASE + IO_BANK0_INTR0_OFFSET + (((4*switchPins[c]) & 0xFFE0) >> 3)); 
        tickNow = getTickValue();
        if ((*switchIntAddress & (1 << (((4*switchPins[c]) & 0x1F)+3))) == (1 << (((4*switchPins[c]) & 0x1F)+3)))
        {
            *switchIntAddress |= (1 << (((4*switchPins[c]) & 0x1F)+3));
            if (oldTickSwitches[c] + ROTARY_ENCODER_DEBOUNCE < tickNow)
            {
                switchVals[c] &= ~(1 << 2);
                switchVals[c] |= (1 << 1);
                oldTickSwitches[c]=tickNow;
            }
        }

        if ((*switchIntAddress & (1 << (((4*switchPins[c]) & 0x1F)+2))) == (1 << (((4*switchPins[c]) & 0x1F)+2)))
        {
            *switchIntAddress |= (1 << (((4*switchPins[c]) & 0x1F)+2));
            if (oldTickSwitches[c] + ROTARY_ENCODER_DEBOUNCE < tickNow)
            {
                switchVals[c] |= ((1 << 2) | (1 << 0));
                oldTickSwitches[c]=tickNow;
            }
        }
    }


}

void initRotaryEncoder(const uint8_t* pins,const uint8_t nswitches)
{
    uint32_t* switchInteAddress;
    uint32_t* switchRegisterAddress;
    uint32_t clearMask;

    switchCount = nswitches;
    if (switchCount > 8)
    {
        switchCount = 8;
    }

    for (uint8_t c=0;c<8;c++)
    {
        switchPins[c] = 0;
        switchVals[c] = 0;
        oldTickSwitches[c] = 0;
    }

    encoderStickyIncrement = 0;
    encoderStickyDecrement = 0;
    encoderSubStep = 0;
    currentUsVal = 0;
    lastUsVal = 0;
    lastEncoderTransitionUs = getTimeLW();
    encoderPrevState = readEncoderState();

    // define pullups for encoder input and switch
    *ENCODER_1_PAD_CNTR &= ~(1 << PADS_BANK0_GPIO0_PDE_LSB);
    *ENCODER_1_PAD_CNTR |= (1 << PADS_BANK0_GPIO0_PUE_LSB);
    *ENCODER_2_PAD_CNTR &= ~(1 << PADS_BANK0_GPIO0_PDE_LSB);
    *ENCODER_2_PAD_CNTR |= (1 << PADS_BANK0_GPIO0_PUE_LSB);

    // set io bank control to sio
    *ENCODER_1_PIN_CNTR = 5;
    *ENCODER_2_PIN_CNTR = 5;
    //*SWITCH_PIN_CNTR = 5;

    clearMask = (1 << ENCODER_1_EDGE_LOW) | (1 << ENCODER_1_EDGE_HIGH) | (1 << ENCODER_2_EDGE_LOW) | (1 << ENCODER_2_EDGE_HIGH);
    *ENCODER_1_INTR |= clearMask;

    // enable level change interrupt
    *ENCODER_1_INTE |= (1 << ENCODER_1_EDGE_LOW) | (1 << ENCODER_1_EDGE_HIGH);
    *ENCODER_2_INTE |= (1 << ENCODER_2_EDGE_LOW) | (1 << ENCODER_2_EDGE_HIGH);
    for (uint8_t c=0;c<switchCount;c++)
    {
        // control disable pulldown and enable pullup
        switchRegisterAddress = (uint32_t*)(PADS_BANK0_BASE + PADS_BANK0_GPIO0_OFFSET + 4*pins[c]); //pad_ctrl: ((volatile uint32_t*)(PADS_BANK0_BASE + PADS_BANK0_GPIO0_OFFSET + 4*ENCODER_1))
        *switchRegisterAddress &= ~(1 << PADS_BANK0_GPIO0_PDE_LSB);
        *switchRegisterAddress |= (1 << PADS_BANK0_GPIO0_PUE_LSB);

        // set control to sio
        switchRegisterAddress = (uint32_t*)(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + 8*pins[c]); //pin_ctrl  // ((volatile uint32_t*)(IO_BANK0_BASE + IO_BANK0_GPIO0_CTRL_OFFSET + 8*ENCODER_1))
        *switchRegisterAddress = 5;

        // enable edge triggers 
        switchInteAddress = (uint32_t*)(IO_BANK0_BASE + IO_BANK0_PROC1_INTE0_OFFSET + (((4*pins[c]) & 0xFFE0) >> 3));
        *switchInteAddress |= (1 << (((4*pins[c]) & 0x1F)+2)) | (1 << (((4*pins[c]) & 0x1F)+3)); // (1 << SWITCH_EDGE_HIGH) | (1 << SWITCH_EDGE_LOW);
        switchPins[c]=pins[c];
    }

    // powersense config
    *POWERSENSE_PAD_CNTR &= ~ ((1 << PADS_BANK0_GPIO0_PDE_LSB) | (1 << PADS_BANK0_GPIO0_PDE_LSB));
    *POWERSENSE_PIN_CNTR = 5;
    *POWERSENSE_INTE |= (1 << POWERSENSE_EDGE_LOW); 


    *NVIC_ISER = (1 << 13);
    setInterruptPriority(13,1);
}

uint32_t getEncoderValue()
{
    return encoderVal;
}

uint8_t getSwitchValue(uint8_t nr)
{
    return switchVals[nr];
}

void clearPressedStickyBit(uint8_t nr)
{
    switchVals[nr] &= ~(1 << 0);
}

void clearReleasedStickyBit(uint8_t nr)
{
    switchVals[nr] &= ~(1 << 1);
}

int16_t getStickyIncrementDelta()
{
    return encoderStickyIncrement-encoderStickyDecrement;
}

int16_t consumeStickyIncrementDelta()
{
    int16_t delta;
    uint32_t irqWasEnabled;
    irqWasEnabled = (*NVIC_ISER & (1 << 13));
    *NVIC_ICER = (1 << 13);
    delta = encoderStickyIncrement-encoderStickyDecrement;
    encoderStickyDecrement=0;
    encoderStickyIncrement=0;
    if (irqWasEnabled != 0)
    {
        *NVIC_ISER = (1 << 13);
    }
    return delta;
}

// returns the time passed in us between sticky increments oder decrements
uint32_t getRotaryDeltaT()
{
    if (currentUsVal >= lastUsVal)
    {
        return currentUsVal - lastUsVal;
    }
    return 0xFFFFFFFF;
}

void clearStickyIncrementDelta()
{
    (void)consumeStickyIncrementDelta();
}
