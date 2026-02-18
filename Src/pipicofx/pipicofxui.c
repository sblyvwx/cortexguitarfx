#include "stdlib.h"
#include "graphics/bwgraphics.h"
#include "drivers/oled_display.h"
#include "drivers/adc.h"
#include "pipicofx/pipicofxui.h"
#include "images/pipicofx_param_2_scaled.h"
#include "images/pipicofx_param_1_scaled.h"
#include "romfunc.h"
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"



static BwImageBufferType imgBuffer;
static BwImageType img;
const uiEnterFct uiEnterFunctions[]={&enterLevel0, &enterLevel1, &enterLevel2, &enterLevel3, &enterLevel4, &enterLevel5, &enterLevel6, &enterLevel7};


/*
Callback function pointers
*/
static void (*enterButtonPressedCallback)(PiPicoFxUiType *ui)=0; 
static void (*enterButtonReleasedCallback)(PiPicoFxUiType* ui)=0; 
static void (*exitButtonPressedCallback)(PiPicoFxUiType* ui)=0;
static void (*exitButtonReleasedCallback)(PiPicoFxUiType* ui)=0;
static void (*rotaryCallback)(int16_t val,PiPicoFxUiType* ui)=0;
static void (*knob0Callback)(uint16_t val,PiPicoFxUiType* ui)=0;
static void (*knob1Callback)(uint16_t val,PiPicoFxUiType* ui)=0;
static void (*knob2Callback)(uint16_t val,PiPicoFxUiType* ui)=0;
static void (*stompSwitch1PressedCallback)(PiPicoFxUiType* ui)=0;
static void (*stompSwitch1ReleasedCallback)(PiPicoFxUiType* ui)=0;
static void (*stompSwitch2PressedCallback)(PiPicoFxUiType* ui)=0;
static void (*stompSwitch2ReleasedCallback)(PiPicoFxUiType* ui)=0;
static void (*stompSwitch3PressedCallback)(PiPicoFxUiType* ui)=0;
static void (*stompSwitch3ReleasedCallback)(PiPicoFxUiType* ui)=0;
static void (*onUpdateCallback)(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)=0;
static void (*onCreateCallback)(PiPicoFxUiType* ui)=0;

BwImageType * getImageBuffer()
{
    img.data = imgBuffer.data;
    img.sx = imgBuffer.sx;
    img.sy = imgBuffer.sy;
    img.type = imgBuffer.type;
    return &img;
}


/*
 registration functions, used to attach a certain functionality to a
 ui element callback
 */
void registerEnterButtonPressedCallback(void(*cb)(PiPicoFxUiType*))
{
    enterButtonPressedCallback=cb;
}
void registerEnterButtonReleasedCallback(void(*cb)(PiPicoFxUiType*))
{
    enterButtonReleasedCallback=cb;
}
void registerExitButtonPressedCallback(void(*cb)(PiPicoFxUiType*))
{
    exitButtonPressedCallback=cb;
}
void registerExitButtonReleasedCallback(void(*cb)(PiPicoFxUiType*))
{
    exitButtonReleasedCallback=cb;
}
void registerStompswitch1PressedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch1PressedCallback=cb;
}
void registerStompswitch1ReleasedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch1ReleasedCallback=cb;
}
void registerStompswitch2PressedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch2PressedCallback=cb;
}
void registerStompswitch2ReleasedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch2ReleasedCallback=cb;
}
void registerStompswitch3PressedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch3PressedCallback=cb;
}
void registerStompswitch3ReleasedCallback(void(*cb)(PiPicoFxUiType*))
{
    stompSwitch3ReleasedCallback=cb;
}
void registerRotaryCallback(void(*cb)(int16_t,PiPicoFxUiType*))
{
    rotaryCallback=cb;
}
void registerKnob0Callback(void(*cb)(uint16_t,PiPicoFxUiType*))
{
    knob0Callback=cb;
}
void registerKnob1Callback(void(*cb)(uint16_t,PiPicoFxUiType*))
{
    knob1Callback=cb;
}
void registerKnob2Callback(void(*cb)(uint16_t,PiPicoFxUiType*))
{
    knob2Callback=cb;
}
void registerOnUpdateCallback(void(*cb)(int16_t,int16_t,uint8_t,PiPicoFxUiType*))
{
    onUpdateCallback=cb;
}
void registerOnCreateCallback(void(*cb)(PiPicoFxUiType*))
{
    onCreateCallback=cb;
}

void clearCallbackAssignments()
{
    enterButtonPressedCallback = 0;
    enterButtonReleasedCallback = 0;
    exitButtonPressedCallback = 0;
    exitButtonReleasedCallback = 0;
    knob0Callback = 0;
    knob1Callback = 0;
    knob2Callback = 0;
    rotaryCallback = 0;
    stompSwitch1PressedCallback = 0;
    stompSwitch1ReleasedCallback = 0;
    stompSwitch2PressedCallback = 0;
    stompSwitch2ReleasedCallback = 0;
    stompSwitch3PressedCallback = 0;
    stompSwitch3ReleasedCallback = 0;
    onUpdateCallback = 0;
    onCreateCallback = 0;
}


/*
Callees used by the OS to dispatch UI event, should not be used by "user" code
*/

void onEnterPressed(PiPicoFxUiType*data)
{
    if (enterButtonPressedCallback!=0)
    {
        enterButtonPressedCallback(data);
    }
}

void onEnterReleased(PiPicoFxUiType*data)
{
    if (enterButtonReleasedCallback!=0)
    {
        enterButtonReleasedCallback(data);
    }
}

void onExitPressed(PiPicoFxUiType*data)
{
    uint8_t nextLevel;
    if (exitButtonPressedCallback!=0)
    {
        exitButtonPressedCallback(data);
    }
    if(uiStackCurrent(data) != 0xFF)
    {
        nextLevel = uiStackPop(data);
        if (nextLevel < (sizeof(uiEnterFunctions)/sizeof(uiEnterFunctions[0])))
        {
            uiEnterFunctions[nextLevel](data);
        }
    }
}

void onExitReleased(PiPicoFxUiType*data)
{
    if (exitButtonReleasedCallback!=0)
    {
        exitButtonReleasedCallback(data);
    }
}

void onRotaryChange(int16_t delta,PiPicoFxUiType*data)
{
    if(rotaryCallback!=0)
    {
        rotaryCallback(delta,data);
    }
}

void onKnob0(uint16_t val,PiPicoFxUiType*data)
{
    if(knob0Callback!=0)
    {
        knob0Callback(val,data);
    }
}

void onKnob1(uint16_t val,PiPicoFxUiType*data)
{
    if(knob1Callback!=0)
    {
        knob1Callback(val,data);
    }
}

void onKnob2(uint16_t val,PiPicoFxUiType*data)
{
    if(knob2Callback!=0)
    {
        knob2Callback(val,data);
    }
}

void onStompSwitch1Pressed(PiPicoFxUiType*data)
{
    if (stompSwitch1PressedCallback!=0)
    {
        stompSwitch1PressedCallback(data);
    }
}

void onStompSwitch1Released(PiPicoFxUiType*data)
{
    if (stompSwitch1ReleasedCallback!=0)
    {
        stompSwitch1ReleasedCallback(data);
    }
}

void onStompSwitch2Pressed(PiPicoFxUiType*data)
{
    if (stompSwitch2PressedCallback!=0)
    {
        stompSwitch2PressedCallback(data);
    }
}

void onStompSwitch2Released(PiPicoFxUiType*data)
{
    if (stompSwitch2ReleasedCallback!=0)
    {
        stompSwitch2ReleasedCallback(data);
    }
}

void onStompSwitch3Pressed(PiPicoFxUiType*data)
{
    if (stompSwitch3PressedCallback!=0)
    {
        stompSwitch3PressedCallback(data);
    }
}

void onStompSwitch3Released(PiPicoFxUiType*data)
{
    if (stompSwitch3ReleasedCallback!=0)
    {
        stompSwitch3ReleasedCallback(data);
    }
}

void onUpdate(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    if (onUpdateCallback != 0)
    {
        onUpdateCallback(avgInput, avgOutput, cpuLoad, data);
    }
    /* 3D-rotating "yvwx" in the bottom-right corner (page 7, cols 105-127).
     * Simulates Y-axis rotation by horizontally compressing/expanding the
     * 23-column source text through 12 frames (30° steps).  The back face
     * shows the text reversed ("xwvy").
     *
     * Race-free: the update callback already started async DMA from page 0;
     * page 7 is transferred last, so our writes land in the current frame.
     *
     * Cost per frame: 23 byte-clears + up to 23 byte copies via table
     * lookup.  No function calls, no division, no floats. */
    {
        /* Pre-computed 23-column source: y(5) gap v(5) gap w(5) gap x(5) */
        static const uint8_t srcCols[23] = {
            0x0C,0x50,0x50,0x50,0x3C, 0x00,  /* y + gap */
            0x1C,0x20,0x40,0x20,0x1C, 0x00,  /* v + gap */
            0x3C,0x40,0x30,0x40,0x3C, 0x00,  /* w + gap */
            0x44,0x28,0x10,0x28,0x44          /* x       */
        };

        /* Nearest-neighbour column maps for compressed widths (no division) */
        static const uint8_t map20[20] = {
            0,1,2,3,4,5,6,8,9,10,11,12,13,15,16,17,18,19,20,22};
        static const uint8_t map12[12] = {
            0,2,4,6,8,10,12,14,16,18,20,22};

        /* width = round(23*|cos(frame*30°)|), min 1 */
        static const uint8_t frameWidth[12] = {
            23,20,12,1, 12,20,23,20, 12,1,12,20};

        static uint8_t spinFrame = 0;
        uint8_t width    = frameWidth[spinFrame];
        uint8_t reversed = (spinFrame >= 4 && spinFrame <= 8);
        uint8_t *fb      = imgBuffer.data + 7 * 128;

        /* Clear the 23-col region */
        for (uint8_t i = 105; i < 128; i++) fb[i] = 0;

        if (width == 1) {
            /* Edge-on: thin vertical line at centre of region */
            fb[116] = (uint8_t)(0x7E >> 3);
        } else {
            const uint8_t *map = (width >= 23) ? (const uint8_t*)0
                               : (width >= 20) ? map20
                               :                 map12;
            /* Centre the projected text within the 23-col region */
            uint8_t startCol = (uint8_t)(105 + ((23 - width) >> 1));
            for (uint8_t d = 0; d < width; d++) {
                uint8_t src = map ? map[d] : d;
                if (reversed) src = 22 - src;
                fb[startCol + d] = (uint8_t)(srcCols[src] >> 3);
            }
        }

        spinFrame++;
        if (spinFrame >= 12) spinFrame = 0;
    }
}

void onCreate(PiPicoFxUiType*data)
{
    if (onCreateCallback != 0)
    {
        onCreateCallback(data);
    }
}

uint8_t uiStackPush(PiPicoFxUiType* piPicoUiController,uint8_t val)
{
    if (piPicoUiController->uiLevelStackPtr < PIPICOFX_UI_STACK_SIZE)
    {
        *(piPicoUiController->uiLevelStack + piPicoUiController->uiLevelStackPtr++) = val;
        return 0;
    }
    return 1;
}

uint8_t uiStackPop(PiPicoFxUiType* piPicoUiController)
{
    if (piPicoUiController->uiLevelStackPtr != 0)
    {
        return *(piPicoUiController->uiLevelStack + --piPicoUiController->uiLevelStackPtr);
    }
    return 0xFF;
}

uint8_t uiStackCurrent(PiPicoFxUiType* piPicoUiController)
{
    if (piPicoUiController->uiLevelStackPtr != 0)
    {
        return *(piPicoUiController->uiLevelStack + piPicoUiController->uiLevelStackPtr-1);
    }
    return 0xFF;
}

void piPicoFxUiSetup(PiPicoFxUiType* piPicoUiController)
{
    piPicoUiController->currentProgram=fxPrograms[0];
    piPicoUiController->currentProgramIdx=0;
    piPicoUiController->currentParameter=fxPrograms[piPicoUiController->currentProgramIdx]->parameters;
    piPicoUiController->currentParameterIdx=0;
    piPicoUiController->locked=0;
    piPicoUiController->editViaRotary =1;
    piPicoUiController->uiLevelStackPtr = 0;
    activeProgramChain[0] = piPicoUiController->currentProgramIdx;
    for (uint8_t c=1;c<FX_PRESET_CHAIN_SLOTS;c++)
    {
        activeProgramChain[c] = N_FX_PROGRAMS-1;
    }
    for (uint8_t c=0;c<8;c++)
    {
        *(piPicoUiController->uiLevelStack + c) = 0xFF;
    }
    imgBuffer.sx=128;
    imgBuffer.sy=64;
    #ifdef HORIZONTAL_DISPLAY
    imgBuffer.type = BWIMAGE_BW_IMAGE_STRUCT_VERTICAL_BYTES;
    #endif
    #ifdef VERTICAL_DISPLAY
    imgBuffer.type = BWIMAGE_BW_IMAGE_STRUCT_HORIZONTAL_BYTES;
    #endif
}

