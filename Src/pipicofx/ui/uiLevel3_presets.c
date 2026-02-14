#include "stdlib.h"
#include "graphics/bwgraphics.h"
#include "graphics/gfxfont.h"
#include "drivers/oled_display.h"
#include "drivers/adc.h"
#include "pipicofx/pipicofxui.h"
#include "images/editOverlay.h"
#include "images/fwUpgradeOverlay.h"
#include "images/aboutoverlay.h"
#include "images/fwupdateScreen.h"
#include "romfunc.h"
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"
#include "gen/version.h"

extern FxPresetType presets[3];
extern uint8_t currentBank;
extern uint8_t currentPreset;
static volatile uint8_t overlayNr=0xFF;
/* Settings overlay removed: CS4270 codec requires extension board */
const BwImageType* overlays[]={&editOverlay_streamimg, &aboutoverlay_streamimg, &fwUpgradeOverlay_streamimg};
extern volatile uint8_t programsToInitialize[3];
extern volatile uint8_t programChangeState;

#define OVERLAY_NR_EDIT 0
#define OVERLAY_NR_ABOUT 1
#define OVERLAY_NR_FWUPDATE 2
#define OVERLAY_NR_MAX 2

/* 0 = not confirming, 1 = confirm Yes, 2 = confirm No */
static volatile uint8_t fwUpdateConfirm = 0;

static void initGeneratedPresetToCurrentFx(PiPicoFxUiType*data,FxPresetType* preset,uint8_t pos)
{
    generateEmptyPreset(preset,currentBank,pos);
    setPresetActiveSlot(preset,0);
    preset->programNr = data->currentProgramIdx;
    parametersToPreset(preset,fxPrograms);
    applyPreset(preset,fxPrograms);
}

static void create(PiPicoFxUiType*data)
{
    char strbfr[8];
    char nrbfr[8];
    BwImageType* imgBuffer = getImageBuffer();
    const GFXfont * font = getGFXFont(FREESANS12PT7B);
    // display preset Name and Bank Number
    clearImage(imgBuffer);
    *(strbfr) = 0;
    appendToString(strbfr,"Bank:");
    UInt8ToChar(currentBank,nrbfr);
    appendToString(strbfr,nrbfr);
    drawText(5,21,strbfr,imgBuffer,font);
    *(strbfr) = 0;
    appendToString(strbfr,presets[currentPreset].name);
    drawText(5,42,strbfr,imgBuffer,font);

    *(strbfr) = 0;
    appendToString(strbfr,"In");
    font = getGFXFont(FREEMONO12PT7B);
    drawText(5,42+10,strbfr,imgBuffer,(void*)0);

    *(strbfr) = 0;
    appendToString(strbfr,"Out");
    drawText(5,42+20,strbfr,imgBuffer,(void*)0);

    applyPreset(presets+currentPreset,fxPrograms);
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{    
    BwImageType* imgBuffer = getImageBuffer();
    // draw Level bars
    clearSquare(40.0f,43.0f,128.0f,64.0f,imgBuffer);
    //in
    drawSquare(40.0f,43.0f,40.0f + int2float(avgInput)*(128.0f-40.0f)/128.0f,52.0f,imgBuffer);
    //out
    drawSquare(40.0f,53.0f,40.0f + int2float(avgOutput)*(128.0f-40.0f)/128.0f,64.0f,imgBuffer);

    OledwriteFramebufferAsync(imgBuffer->data);
}

static void enterCallback(PiPicoFxUiType*data) 
{
    BwImageType* imgBuffer = getImageBuffer();
    char strbfr[24];
    // show overlay menu (if not there)
    if (overlayNr == 0xFF)
    {
        overlayNr = OVERLAY_NR_EDIT;
        drawImage(41,0,&editOverlay_streamimg,imgBuffer);
        uiStackPush(data,0xFF);
    }
    else
    {
        uiStackPop(data);
        uiStackPush(data, 3);
        if (overlayNr == OVERLAY_NR_EDIT)
        {
            enterLevel4(data);
        }
        else if (overlayNr == OVERLAY_NR_ABOUT)
        {
            clearSquareInt(0,0,128,43,imgBuffer);
            *strbfr=0;
            appendToString(strbfr,"About PiPicoFX");
            drawText(0,8,strbfr,imgBuffer,(void*)0);
            drawText(0,16,PI_PICO_FX_VERSION_NR,imgBuffer,(void*)0);
            *strbfr=0;
            appendToString(strbfr,"built ");
            appendToString(strbfr,PI_PICO_FX_BUILD_DATE);
            drawText(0,24,strbfr,imgBuffer,(void*)0);
            *strbfr=0;
            appendToString(strbfr,"      ");
            appendToString(strbfr,PI_PICO_FX_BUILD_TIME);
            drawText(0,32,strbfr,imgBuffer,(void*)0);

        }
        else if (overlayNr == OVERLAY_NR_FWUPDATE)
        {
            if (fwUpdateConfirm == 0)
            {
                /* Show confirmation dialog */
                fwUpdateConfirm = 2;
                clearSquareInt(0,0,128,43,imgBuffer);
                *strbfr=0;
                appendToString(strbfr,"FW Update");
                drawText(0,8,strbfr,imgBuffer,(void*)0);
                *strbfr=0;
                appendToString(strbfr,"Are you sure?");
                drawText(0,24,strbfr,imgBuffer,(void*)0);
                *strbfr=0;
                appendToString(strbfr,"> No");
                drawText(0,40,strbfr,imgBuffer,(void*)0);
            }
            else if (fwUpdateConfirm == 1)
            {
                /* Confirmed Yes — reboot into bootloader */
                drawImage(0,0,&fwupdateScreen_streamimg,imgBuffer);
                OledDisplayImageStandardAdressing(0,0,128,8,imgBuffer->data);
                reset_usb_boot(1 << 17,2);
            }
            else
            {
                /* Selected No — cancel and go back to overlay menu */
                fwUpdateConfirm = 0;
                create(data);
                drawImage(41,0,overlays[overlayNr],imgBuffer);
                uiStackPush(data,0xFF);
            }
        }
    }

}

static void exitCallback(PiPicoFxUiType*data)
{
    char strbfr[8];
    char nrbfr[8];
    const GFXfont * font = getGFXFont(FREESANS12PT7B);
    BwImageType* imgBuffer = getImageBuffer();
    // remove overlay menu (if there)
    if (overlayNr != 0xFF)
    {
        fwUpdateConfirm = 0;
        clearSquare(0.0f,0.0f,128.0f,43.0f,imgBuffer);
        *(strbfr) = 0;
        appendToString(strbfr,"Bank:");
        UInt8ToChar(currentBank,nrbfr);
        appendToString(strbfr,nrbfr);
        drawText(5,21,strbfr,imgBuffer,font);
        *(strbfr) = 0;
        appendToString(strbfr,presets[currentPreset].name);
        drawText(5,42,strbfr,imgBuffer,font);
        overlayNr=0xFF;
    }
    else
    {
        // If a temporary blocker is still present, remove it so the global
        // exit dispatcher can return to the previous level.
        if (uiStackCurrent(data) == 0xFF)
        {
            uiStackPop(data);
        }
    }
}



static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    BwImageType* imgBuffer = getImageBuffer();
    // change overlay icon (if there)
    if (overlayNr != 0xFF)
    {
        if (fwUpdateConfirm != 0)
        {
            /* Toggle between Yes (1) and No (2) */
            BwImageType* imgBuf = getImageBuffer();
            char tbfr[8];
            if (fwUpdateConfirm == 1)
            {
                fwUpdateConfirm = 2;
            }
            else
            {
                fwUpdateConfirm = 1;
            }
            clearSquareInt(0,32,128,48,imgBuf);
            *tbfr = 0;
            if (fwUpdateConfirm == 1)
            {
                appendToString(tbfr,"> Yes");
            }
            else
            {
                appendToString(tbfr,"> No");
            }
            drawText(0,40,tbfr,imgBuf,(void*)0);
            return;
        }
        if (encoderDelta > 0)
        {
            overlayNr++;
            if (overlayNr > OVERLAY_NR_MAX)
            {
                overlayNr=OVERLAY_NR_MAX;
            }
        }
        else
        {
            overlayNr--;
            if (overlayNr > OVERLAY_NR_MAX)
            {
                overlayNr=0;
            }

        }
        drawImage(41,0,overlays[overlayNr],imgBuffer);
    }
    else // change preset
    {
        if (encoderDelta > 0 && currentPreset < 2)
        {
            currentPreset++;
        }
        else if (encoderDelta < 0 && currentPreset > 0)
        {
            currentPreset--;
        }
        if (data->currentProgramIdx != presets[currentPreset].programNr)
        {
            programsToInitialize[0]=presets[currentPreset].programNr;
            programChangeState=1;
            //data->currentProgram = fxPrograms[data->currentProgramIdx];
            //data->currentParameterIdx=0;
            //data->currentParameter = data->currentProgram->parameters;
        }
        applyPreset(presets + currentPreset,fxPrograms);
        create(data);
    }     
}

void enterLevel3(PiPicoFxUiType*data)
{
    if (loadPreset(presets,currentBank*3)!=0)
    {
        initGeneratedPresetToCurrentFx(data,presets,0);
    }
    if (loadPreset(presets+1,currentBank*3+1)!=0)
    {
        initGeneratedPresetToCurrentFx(data,presets+1,1);
    }
    if (loadPreset(presets+2, currentBank*3+2)!=0)
    {
        initGeneratedPresetToCurrentFx(data,presets+2,2);
    }
    data->editViaRotary = 1;
    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    registerOnCreateCallback(&create);
    create(data);
    overlayNr=0xFF;
    if (data->currentProgramIdx != presets[currentPreset].programNr)
    {
        programsToInitialize[0]=presets[currentPreset].programNr;
        programChangeState = 1;
    }
    applyPreset(presets + currentPreset, fxPrograms);
}

