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


extern FxPresetType presets[3];
extern uint8_t currentBank;
extern uint8_t currentPreset;

static void create(PiPicoFxUiType*data)
{
    BwImageType* imgBuffer = getImageBuffer();
    clearImage(imgBuffer);
    drawText(0,8,data->currentProgram->name,imgBuffer,0);
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0)
    {
        drawText(0,16,"No parameters",imgBuffer,0);
    }
    else
    {
        drawText(0,16,data->currentParameter->name,imgBuffer,0);
    }
    OledwriteFramebufferAsync(imgBuffer->data);
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    BwImageType * img=getImageBuffer();
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0 || data->currentParameter->getParameterDisplay == 0)
    {
        OledwriteFramebufferAsync(img->data);
        return;
    }
    clearSquareInt(0,16,128,64,img);
    float fValue,fMaxValue,fMinValue;
    char paramValueBfr[16];
    float cx,cy,px,py;
    (void)avgInput;
    (void)avgOutput;
    (void)cpuLoad;


    fValue = int2float((int32_t)data->currentParameter->rawValue);
    fMaxValue = int2float((int32_t)(1 << 12));
    fMinValue = int2float((int32_t)0);
    fValue = 0.7853981633974483f + 4.71238898038469f*(fValue - fMinValue)/(fMaxValue-fMinValue); //fValue is now an angle in radians from 45° to 315°
    // center is at (51+13(/(24+16)
    px = 64.0f - fsin(fValue)*14.0f;
    py = 40.0f + fcos(fValue)*14.0f;
    cx = 64.0f;
    cy = 40.0f;
    drawImage(13,16,&pipicofx_param_1_scaled_streamimg,img);
    drawLine(cx,cy,px,py,img);
    data->currentParameter->getParameterDisplay(data->currentProgram->data,paramValueBfr);
    drawText(0,64,paramValueBfr,img,0);
    OledwriteFramebufferAsync(img->data);
}

static void enterCallback(PiPicoFxUiType*data) 
{
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0)
    {
        return;
    }
    if (data->currentParameter->control == 0xFF || data->editViaRotary != 0)
    {
        uiStackPush(data, 1);
        enterLevel2(data);
    }
}

static void exitCallback(PiPicoFxUiType*data)
{
    parametersToPreset(presets+currentPreset,fxPrograms);
}

static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    if (data->currentProgram->nParameters == 0)
    {
        return;
    }
    data->currentParameterIdx += encoderDelta;
    if (data->currentParameterIdx >= data->currentProgram->nParameters && encoderDelta > 0)
    {
        data->currentParameterIdx=data->currentProgram->nParameters-1;
    }
    else if (data->currentParameterIdx >= data->currentProgram->nParameters && encoderDelta < 0)
    {
        data->currentParameterIdx = 0;
    }
    data->currentParameter = data->currentProgram->parameters + data->currentParameterIdx;
    create(data);
}

void enterLevel1(PiPicoFxUiType*data)
{
    if (data->currentProgram->nParameters == 0)
    {
        enterLevel0(data);
        return;
    }
    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    create(data);
}