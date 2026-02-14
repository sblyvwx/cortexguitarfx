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
#include "drivers/stompswitches.h"

uint8_t locksymbol[5]={0b01111000,0b01111110,0b01111001,0b01111110,0b01111000 };
BwImageType lock;
extern volatile uint8_t programsToInitialize[3];
extern volatile uint8_t programChangeState;
extern const uint8_t stompswitch_progs[];
extern FxPresetType presets[3];
extern uint8_t currentBank;
extern uint8_t currentPreset;

static void create(PiPicoFxUiType*data)
{
    char lineBuffer[24];
    BwImageType* imgBuffer = getImageBuffer();
    lock.data =locksymbol;
    lock.sx=5;
    lock.sy=8;
    lock.type=BWIMAGE_BW_IMAGE_STRUCT_VERTICAL_BYTES;
    clearImage(imgBuffer);
    drawText(0,1*8,data->currentProgram->name,imgBuffer,0);
    if (data->locked != 0)
    {
        drawImage(122,0,&lock,imgBuffer);
    }

    for (uint8_t c=0;c<data->currentProgram->nParameters;c++)
    {
        if (data->currentProgram->parameters[c].control == 0)
        {
            lineBuffer[0]=0;
            appendToString(lineBuffer,"P1:");
            appendToString(lineBuffer,data->currentProgram->parameters[c].name);
            drawText(0,5*8,lineBuffer,imgBuffer,0);
        }
        if (data->currentProgram->parameters[c].control == 1)
        {
            lineBuffer[0]=0;
            appendToString(lineBuffer,"P2:");
            appendToString(lineBuffer,data->currentProgram->parameters[c].name);
            drawText(0,6*8,lineBuffer,imgBuffer,0);
        }
        if (data->currentProgram->parameters[c].control == 2)
        {
            lineBuffer[0]=0;
            appendToString(lineBuffer,"P3:");
            appendToString(lineBuffer,data->currentProgram->parameters[c].name);
            drawText(0,7*8,lineBuffer,imgBuffer,0);
        }                
    }

    OledwriteFramebufferAsync(imgBuffer->data);
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    BwImageType bargraph;
    BwImageType* imgBuffer = getImageBuffer();
    char stateBuffer[4] = "S0";
    uint8_t bargraphBuffer[128];
    bargraph.data = bargraphBuffer;
    bargraph.sx=128;
    bargraph.sy = 8;
    bargraph.type=BWIMAGE_BW_IMAGE_STRUCT_VERTICAL_BYTES;

    (void)data;
    stateBuffer[1] = (char)('0' + (programChangeState % 10));

    // show basic display
    for (uint8_t c=0;c<128;c++)
    {
        if (c<=avgInput)
        {
            bargraphBuffer[c] = 126;
        }
        else
        {
            bargraphBuffer[c] = 0;
        }
    }
    drawImage(0,1*8,&bargraph,imgBuffer);

    for (uint8_t c=0;c<128;c++)
    {
        if (c<=avgOutput)
        {
            bargraphBuffer[c] = 126;
        }
        else
        {
            bargraphBuffer[c] = 0;
        }
    }
    drawImage(0,2*8,&bargraph,imgBuffer);

    for (uint8_t c=0;c<128;c++)
    {
        if (c<=cpuLoad)
        {
            bargraphBuffer[c] = 126;
        }
        else
        {
            bargraphBuffer[c] = 0;
        }
    }
    drawImage(0,3*8,&bargraph,imgBuffer);

    // Draw state overlay on a guaranteed visible line (same page as CPU bar).
    // If cpuLoad is very low, this will still be readable.
    drawText(104,3*8,stateBuffer,imgBuffer,0);

    OledwriteFramebufferAsync(imgBuffer->data);
}

static void enterCallback(PiPicoFxUiType*data) 
{
    // Off/zero-parameter effects should not enter parameter pages.
    if (data->currentProgram->nParameters == 0)
    {
        return;
    }
    if (data->locked == 0)
    {
        uiStackPush(data, 0);
        enterLevel1(data);
    }
}

static void exitCallback(PiPicoFxUiType*data)
{

    // apply current program and parameters to preset when coming from 4
    if(uiStackCurrent(data)==4)
    {
        presets[currentPreset].programNr = data->currentProgramIdx;
        for (uint8_t c=0;c<fxPrograms[presets[currentPreset].programNr]->nParameters;c++)
        {
            if (fxPrograms[presets[currentPreset].programNr]->parameters[c].control<0xFF)
            {
                presets[currentPreset].parameters[c] = fxPrograms[presets[currentPreset].programNr]->parameters[c].rawValue;
            }
        }
    }
    else
    {
        data->locked ^= 0x1;
        create(data);
    }

}

static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    if (encoderDelta != 0)
    {
        data->currentProgramIdx += encoderDelta;
        if (data->currentProgramIdx >= N_FX_PROGRAMS && encoderDelta > 0)
        {
            data->currentProgramIdx = N_FX_PROGRAMS-1;
        } 
        else if (data->currentProgramIdx >= N_FX_PROGRAMS && encoderDelta < 0)
        {
            data->currentProgramIdx = 0;
        }
        data->currentProgram = fxPrograms[data->currentProgramIdx];
        data->currentParameterIdx=0;
        data->currentParameter = data->currentProgram->parameters;
        parametersToPreset(presets + currentPreset,fxPrograms);
    }
    create(data);
}

static void genericStompSwitchCallback(uint8_t switchNr, PiPicoFxUiType* data)
{
    currentPreset = switchNr;
    uiStackPush(data, 0);
    enterLevel3(data);
}

static void stompswitch1Callback(PiPicoFxUiType* data)
{
    genericStompSwitchCallback(0,data);
}

static void stompswitch2Callback(PiPicoFxUiType* data)
{
    genericStompSwitchCallback(1,data);
}

static void stompswitch3Callback(PiPicoFxUiType* data)
{
    genericStompSwitchCallback(2,data);
}


/*
register exit, rotary, knobs and stompswitch callbacks
remove enter callback
register onUpdate, on Create
*/
void enterLevel0(PiPicoFxUiType*data)
{
    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerStompswitch1ReleasedCallback(&stompswitch1Callback);
    registerStompswitch2ReleasedCallback(&stompswitch2Callback);
    registerStompswitch3ReleasedCallback(&stompswitch3Callback);
    registerOnUpdateCallback(&update);
    registerOnCreateCallback(&create);
    create(data);
}
