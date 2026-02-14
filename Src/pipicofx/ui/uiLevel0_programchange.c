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

uint8_t locksymbol[5]={0b01111000,0b01111110,0b01111001,0b01111110,0b01111000 };
BwImageType lock;
extern volatile uint8_t programsToInitialize[3];
extern volatile uint8_t programChangeState;
extern volatile uint8_t activeProgramChain[];
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
    uiStackPush(data, 0);
    enterLevel1(data);
}

static void exitCallback(PiPicoFxUiType*data)
{

    // apply current program and parameters to preset when coming from 4
    if(uiStackCurrent(data)==4)
    {
        setPresetActiveSlot(presets + currentPreset,presets[currentPreset].activeSlot);
        presets[currentPreset].programNr = data->currentProgramIdx;
        parametersToPreset(presets + currentPreset,fxPrograms);
        applyPreset(presets + currentPreset,fxPrograms);
    }
    else
    {
        // Encoder+2-button mode: Exit from Level0 opens preset view.
        // Push return level (0) and a temporary blocker (0xFF) so the
        // global exit dispatcher does not immediately pop back.
        uiStackPush(data, 0);
        uiStackPush(data, 0xFF);
        enterLevel3(data);
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

        if (presets[currentPreset].activeSlot >= FX_PRESET_CHAIN_SLOTS)
        {
            presets[currentPreset].activeSlot = 0;
        }
        setPresetActiveSlot(presets + currentPreset,presets[currentPreset].activeSlot);
        presets[currentPreset].programNr = data->currentProgramIdx;
        parametersToPreset(presets + currentPreset,fxPrograms);

        /* Trigger the fade-out / reset / fade-in state machine so the new
         * program is properly initialised before the DMA ISR uses it.    */
        programsToInitialize[0] = data->currentProgramIdx;
        programChangeState = 1;

        applyPreset(presets + currentPreset,fxPrograms);
    }
    create(data);
}

/*
register exit, rotary, knobs and stompswitch callbacks
remove enter callback
register onUpdate, on Create
*/
void enterLevel0(PiPicoFxUiType*data)
{
    if (presets[currentPreset].activeSlot >= FX_PRESET_CHAIN_SLOTS)
    {
        presets[currentPreset].activeSlot = 0;
    }
    setPresetActiveSlot(presets + currentPreset,presets[currentPreset].activeSlot);
    if (presets[currentPreset].programNr >= N_FX_PROGRAMS)
    {
        presets[currentPreset].programNr = N_FX_PROGRAMS-1;
    }
    data->currentProgramIdx = presets[currentPreset].programNr;
    data->currentProgram = fxPrograms[data->currentProgramIdx];
    data->currentParameterIdx = 0;
    data->currentParameter = data->currentProgram->parameters;
    data->locked = 0;

    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    registerOnCreateCallback(&create);
    create(data);
}
