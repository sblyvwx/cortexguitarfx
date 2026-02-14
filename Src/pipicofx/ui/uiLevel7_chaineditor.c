#include "stdlib.h"
#include "graphics/bwgraphics.h"
#include "drivers/oled_display.h"
#include "pipicofx/pipicofxui.h"
#include "pipicofx/fxPrograms.h"
#include "stringFunctions.h"

extern FxPresetType presets[3];
extern uint8_t currentPreset;
extern volatile uint8_t programsToInitialize[3];
extern volatile uint8_t programChangeState;

static uint8_t selectedSlot = 0;
static uint8_t editMode = 0;

static void syncUiProgramToSelectedSlot(PiPicoFxUiType* data)
{
    setPresetActiveSlot(presets + currentPreset, selectedSlot);
    data->currentProgramIdx = presets[currentPreset].programNr;
    if (data->currentProgramIdx >= N_FX_PROGRAMS)
    {
        data->currentProgramIdx = N_FX_PROGRAMS-1;
    }
    data->currentProgram = fxPrograms[data->currentProgramIdx];
    data->currentParameterIdx = 0;
    data->currentParameter = data->currentProgram->parameters;
}

static void create(PiPicoFxUiType*data)
{
    char lineBuffer[32];
    char nrBuffer[8];
    uint8_t programIdx;
    BwImageType* imgBuffer = getImageBuffer();

    clearImage(imgBuffer);
    drawText(0,0,"Chain Editor",imgBuffer,0);

    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        lineBuffer[0] = 0;
        appendToString(lineBuffer,(s == selectedSlot) ? ">" : " ");
        appendToString(lineBuffer,"S");
        UInt8ToChar((uint8_t)(s+1),nrBuffer);
        appendToString(lineBuffer,nrBuffer);
        appendToString(lineBuffer,":");

        programIdx = presets[currentPreset].slotProgramNr[s];
        if (programIdx >= N_FX_PROGRAMS)
        {
            programIdx = N_FX_PROGRAMS-1;
        }
        appendToString(lineBuffer,fxPrograms[programIdx]->name);
        drawText(0,(uint16_t)((s+1)*8),lineBuffer,imgBuffer,0);
    }

    if (editMode == 0)
    {
        drawText(0,6*8,"Rot:Slot Ent:Edit",imgBuffer,0);
    }
    else
    {
        drawText(0,6*8,"Rot:Fx Ent/Ex:OK",imgBuffer,0);
    }
    drawText(0,7*8,"Exit:Back",imgBuffer,0);

    OledwriteFramebufferAsync(imgBuffer->data);
    (void)data;
}

static void update(int16_t avgInput,int16_t avgOutput,uint8_t cpuLoad,PiPicoFxUiType*data)
{
    BwImageType* imgBuffer = getImageBuffer();
    OledwriteFramebufferAsync(imgBuffer->data);
    (void)avgInput;
    (void)avgOutput;
    (void)cpuLoad;
    (void)data;
}

static void enterCallback(PiPicoFxUiType*data)
{
    if (editMode == 0)
    {
        editMode = 1;
        if (uiStackCurrent(data) != 0xFF)
        {
            uiStackPush(data,0xFF);
        }
    }
    else
    {
        editMode = 0;
        if (uiStackCurrent(data) == 0xFF)
        {
            uiStackPop(data);
        }
    }
    syncUiProgramToSelectedSlot(data);
    create(data);
}

static void exitCallback(PiPicoFxUiType*data)
{
    if (editMode == 1)
    {
        // Leave edit mode only. Keep blocker in place for this keypress so
        // global exit dispatch does not jump to the previous screen.
        editMode = 0;
        create(data);
    }
    else
    {
        // Allow leaving this level via global exit dispatch.
        if (uiStackCurrent(data) == 0xFF)
        {
            uiStackPop(data);
        }
    }
}

static void rotaryCallback(int16_t encoderDelta,PiPicoFxUiType*data)
{
    int16_t nextVal;

    if (encoderDelta == 0)
    {
        return;
    }

    if (editMode == 0)
    {
        nextVal = (int16_t)selectedSlot + ((encoderDelta > 0) ? 1 : -1);
        if (nextVal < 0)
        {
            nextVal = 0;
        }
        else if (nextVal >= FX_PRESET_CHAIN_SLOTS)
        {
            nextVal = FX_PRESET_CHAIN_SLOTS-1;
        }
        selectedSlot = (uint8_t)nextVal;
        syncUiProgramToSelectedSlot(data);
        create(data);
        return;
    }

    nextVal = (int16_t)presets[currentPreset].slotProgramNr[selectedSlot] + ((encoderDelta > 0) ? 1 : -1);
    if (nextVal < 0)
    {
        nextVal = 0;
    }
    else if (nextVal >= N_FX_PROGRAMS)
    {
        nextVal = N_FX_PROGRAMS-1;
    }

    presets[currentPreset].slotProgramNr[selectedSlot] = (uint8_t)nextVal;
    syncUiProgramToSelectedSlot(data);
    parametersToPreset(presets + currentPreset,fxPrograms);

    /* Trigger fade-out / reset / fade-in for the changed program */
    programsToInitialize[0] = (uint8_t)nextVal;
    programChangeState = 1;

    applyPreset(presets + currentPreset,fxPrograms);
    create(data);
}

void enterLevel7(PiPicoFxUiType*data)
{
    if (presets[currentPreset].activeSlot >= FX_PRESET_CHAIN_SLOTS)
    {
        presets[currentPreset].activeSlot = 0;
    }
    selectedSlot = presets[currentPreset].activeSlot;
    editMode = 0;
    syncUiProgramToSelectedSlot(data);

    clearCallbackAssignments();
    registerEnterButtonPressedCallback(&enterCallback);
    registerExitButtonPressedCallback(&exitCallback);
    registerRotaryCallback(&rotaryCallback);
    registerOnUpdateCallback(&update);
    registerOnCreateCallback(&create);
    create(data);
}
