#include "stdlib.h"
#include "graphics/bwgraphics.h"
#include "drivers/oled_display.h"
#include "drivers/adc.h"
#include "pipicofx/pipicofxui.h"
#include "images/pipicofx_param_2_scaled.h"
#include "romfunc.h"
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"


extern FxPresetType presets[3];
extern uint8_t currentBank;
extern uint8_t currentPreset;

static void drawHeader(PiPicoFxUiType* data,BwImageType* img)
{
    drawText(0,8,data->currentProgram->name,img,0);
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0)
    {
        drawText(0,16,"No parameters",img,0);
    }
    else
    {
        drawText(0,16,data->currentParameter->name,img,0);
    }
}

static void create(PiPicoFxUiType*data)
{
    BwImageType* imgBuffer = getImageBuffer();
    clearImage(imgBuffer);
    drawHeader(data,imgBuffer);
    OledwriteFramebufferAsync(imgBuffer->data);
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    BwImageType * img = getImageBuffer();
    const float knobCenterX = 64.0f;
    const float knobCenterY = 41.0f;
    const float knobNeedleRadius = 11.0f;
    const uint8_t knobPosX = 13;
    const uint8_t knobPosY = 17;
    const uint8_t paramValueY = 58;
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0 || data->currentParameter->getParameterDisplay == 0)
    {
        OledwriteFramebufferAsync(img->data);
        return;
    }
    clearSquareInt(0,17,128,64,img);
    drawHeader(data,img);
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
    px = knobCenterX - fsin(fValue)*knobNeedleRadius;
    py = knobCenterY + fcos(fValue)*knobNeedleRadius;
    cx = knobCenterX;
    cy = knobCenterY;
    drawImage(knobPosX,knobPosY,&pipicofx_param_2_scaled_streamimg,img);
    drawLine(cx,cy,px,py,img);   
    data->currentParameter->getParameterDisplay(data->currentProgram->data,paramValueBfr);
    drawText(0,paramValueY,paramValueBfr,img,0);
    OledwriteFramebufferAsync(img->data);
}


static void exitCallback(PiPicoFxUiType*data)
{
    // none
}

static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    int16_t newRaw;
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0 || data->currentParameter->setParameter == 0)
    {
        return;
    }
    newRaw = data->currentParameter->rawValue + encoderDelta*data->currentParameter->increment;
    if (newRaw < 0)
    {
        newRaw = 0;
    }
    else if  (newRaw > ((1 << 12)-1))
    {
        newRaw = ((1 << 12)-1);
    }
    /* Call setParameter first — many callbacks overwrite rawValue with a
     * mapped/converted value.  We restore the raw accumulator afterwards
     * so that the next rotary increment starts from the correct position. */
    data->currentParameter->setParameter((uint16_t)newRaw,data->currentProgram->data);
    data->currentParameter->rawValue = newRaw;
}

void enterLevel2(PiPicoFxUiType*data)
{
    if (data->currentProgram->nParameters == 0 || data->currentParameter == 0)
    {
        enterLevel1(data);
        return;
    }
    clearCallbackAssignments();
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    create(data);
}