#include "stdlib.h"
#include "graphics/bwgraphics.h"
#include "graphics/gfxfont.h"
#include "drivers/oled_display.h"
#include "drivers/adc.h"
#include "pipicofx/pipicofxui.h"
#include "images/editOverlay.h"
#include "images/settingsOverlay.h"
#include "romfunc.h"
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"

#define EDITLEVEL_CHAIN 0
#define EDITLEVEL_PARAMETERS 1
#define EDITLEVEL_MAX EDITLEVEL_PARAMETERS

extern FxPresetType presets[3];
extern uint8_t currentBank;
extern uint8_t currentPreset;
extern volatile uint8_t programsToInitialize[3];
extern volatile uint8_t programChangeState;

static volatile uint8_t editType; // 0: Chain
                                  // 1: Parameters

static void create(PiPicoFxUiType*data)
{
    char strbfr[8];
    const GFXfont * font = getGFXFont(FREESANS12PT7B);
    BwImageType * imgBuffer = getImageBuffer();
    clearImage(imgBuffer);
    *(strbfr) = 0;    
    appendToString(strbfr,presets[currentPreset].name);
    drawText(5,21,strbfr,imgBuffer,font);
    switch (editType)
    {
        case EDITLEVEL_CHAIN:
            *(strbfr) = 0;    
            appendToString(strbfr,"Edit Chain");
            break;
        case EDITLEVEL_PARAMETERS:
            *(strbfr) = 0;    
            appendToString(strbfr,"Edit Params");
            break;    
    }
    font = getGFXFont(FREESANS9PT7B);
    drawText(5,42,strbfr,imgBuffer,font);
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    BwImageType* imgBuffer = getImageBuffer();
    OledwriteFramebufferAsync(imgBuffer->data);
}


static void enterCallback(PiPicoFxUiType*data) 
{
    switch (editType)
    {
        case EDITLEVEL_CHAIN:
            uiStackPush(data,4);
            enterLevel7(data);
            break;
        case EDITLEVEL_PARAMETERS:
            if (presets[currentPreset].activeSlot >= FX_PRESET_CHAIN_SLOTS)
            {
                presets[currentPreset].activeSlot = 0;
            }
            setPresetActiveSlot(presets + currentPreset,presets[currentPreset].activeSlot);
            data->currentProgramIdx = presets[currentPreset].programNr;
            data->currentProgram = fxPrograms[data->currentProgramIdx];
            if (data->currentProgram->nParameters > 0)
            {
                uiStackPush(data,4);
                data->currentParameterIdx = 0;
                data->currentParameter = data->currentProgram->parameters + data->currentParameterIdx;
                enterLevel1(data);
            }
            break;
    }
}

static void exitCallback(PiPicoFxUiType*data)
{
    /* No save dialog — preset persistence requires extension board EEPROM.
     * Just let the global exit dispatcher handle navigation back.        */
    (void)data;
}

static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    if (encoderDelta > 0)
    {
        editType++;
        if (editType > EDITLEVEL_MAX)
        {
            editType = EDITLEVEL_MAX;
        }
    }
    else if (encoderDelta < 0)
    {
        editType--;
        if (editType > EDITLEVEL_MAX)
        {
            editType = 0;
        }
    }
    create(data);
}



void enterLevel4(PiPicoFxUiType*data)
{
    editType = 0;
    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    registerOnCreateCallback(&create);
    create(data);
}

