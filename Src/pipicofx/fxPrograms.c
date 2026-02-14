#include <stdint.h>
#include "pipicofx/fxPrograms.h"
#include "pipicofx/picofxCore.h"
#include "drivers/24lc128.h"
#include "stringFunctions.h"

typedef struct __attribute__((__packed__)) {
    uint8_t bankPos : 3;
    uint8_t bankNr : 5;
    uint8_t programNr;
    uint8_t ledColor;
    char name[24];
    uint16_t parameters[8];
    uint16_t magicNr;
} FxPresetLegacyType;

volatile uint8_t activeProgramChain[FX_PRESET_CHAIN_SLOTS] = {
    N_FX_PROGRAMS-1, N_FX_PROGRAMS-1, N_FX_PROGRAMS-1
};

FxProgramType* fxPrograms[N_FX_PROGRAMS]={
    
    &fxProgram1, // amp model
    &fxProgram9, // amp model high gain
    &fxProgram14, // eq
    &fxProgram15, // vocal processor
    &fxProgram8, // compressor
    &fxProgram2, // vibchorus
    &fxProgram11, // sine chorus
    &fxProgram6, // delay
    &fxProgram10, // reverb
    &fxProgram12, // allpass reverb
    &fxProgram13, // hadamard diffusor reverb
    &fxProgram3 // Off
    };

static inline uint32_t getPresetAddress(uint16_t presetPos)
{
    return ((uint32_t)presetPos) * ((uint32_t)FX_PRESET_STORAGE_BYTES);
}

static inline uint32_t getLegacyPresetAddress(uint16_t presetPos)
{
    return ((uint32_t)presetPos) * ((uint32_t)sizeof(FxPresetLegacyType));
}

static inline void presetSyncActiveSlotToLegacy(FxPresetType* preset)
{
    uint8_t slot = preset->activeSlot;
    if (slot >= FX_PRESET_CHAIN_SLOTS)
    {
        slot = 0;
    }
    preset->programNr = preset->slotProgramNr[slot];
    for (uint8_t c=0;c<FXPROGRAM_MAX_PARAMETERS;c++)
    {
        preset->parameters[c] = preset->slotParameters[slot][c];
    }
}

static inline void presetSyncLegacyToActiveSlot(FxPresetType* preset)
{
    uint8_t slot = preset->activeSlot;
    if (slot >= FX_PRESET_CHAIN_SLOTS)
    {
        slot = 0;
    }
    preset->slotProgramNr[slot] = preset->programNr;
    for (uint8_t c=0;c<FXPROGRAM_MAX_PARAMETERS;c++)
    {
        preset->slotParameters[slot][c] = preset->parameters[c];
    }
}

static inline void sanitizePreset(FxPresetType* preset)
{
    if (preset->activeSlot >= FX_PRESET_CHAIN_SLOTS)
    {
        preset->activeSlot = 0;
    }
    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        if (preset->slotProgramNr[s] >= N_FX_PROGRAMS)
        {
            preset->slotProgramNr[s] = N_FX_PROGRAMS-1;
        }
    }
}

static inline void updateActiveFxChainFromPreset(FxPresetType* preset)
{
    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        if (preset->slotProgramNr[s] < N_FX_PROGRAMS)
        {
            activeProgramChain[s] = preset->slotProgramNr[s];
        }
        else
        {
            activeProgramChain[s] = N_FX_PROGRAMS-1;
        }
    }
}

static void migrateLegacyPreset(FxPresetType* preset,const FxPresetLegacyType* legacy)
{
    preset->bankNr = legacy->bankNr;
    preset->bankPos = legacy->bankPos;
    preset->ledColor = legacy->ledColor;
    if (preset->ledColor == 0)
    {
        preset->ledColor = 1;
    }
    for (uint8_t c=0;c<24;c++)
    {
        preset->name[c] = legacy->name[c];
    }
    preset->activeSlot = 0;
    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        preset->slotProgramNr[s] = N_FX_PROGRAMS-1;
        for (uint8_t c=0;c<FXPROGRAM_MAX_PARAMETERS;c++)
        {
            preset->slotParameters[s][c] = 0;
        }
    }
    preset->slotProgramNr[0] = legacy->programNr;
    for (uint8_t c=0;c<FXPROGRAM_MAX_PARAMETERS;c++)
    {
        preset->slotParameters[0][c] = legacy->parameters[c];
    }
    presetSyncActiveSlotToLegacy(preset);
}

void setPresetActiveSlot(FxPresetType* preset,uint8_t slot)
{
    if (slot >= FX_PRESET_CHAIN_SLOTS)
    {
        slot = 0;
    }
    preset->activeSlot = slot;
    presetSyncActiveSlotToLegacy(preset);
}


void savePreset(FxPresetType* preset,uint16_t presetPos)
{
    uint16_t cs=0;
    uint8_t * presetArrayPtr;

    sanitizePreset(preset);
    presetSyncLegacyToActiveSlot(preset);
    presetSyncActiveSlotToLegacy(preset);

    presetArrayPtr = (uint8_t*)preset;
    for (uint8_t c=0;c<sizeof(FxPresetType)-2;c++)
    {
        cs += *(presetArrayPtr + c);
    }
    preset->magicNr = cs;
    #if defined(RP2040_FEATHER) && defined(EXTENSION_BOARD)
    uint32_t address = getPresetAddress(presetPos);
    eeprom24lc128WriteArray(address,sizeof(FxPresetType),presetArrayPtr);
    #endif
}

uint8_t loadPreset(FxPresetType* preset,uint16_t presetPos)
{
    uint16_t cs=0,legacyCs=0;
    uint32_t address;
    uint8_t * presetArrayPtr, * legacyPresetArrayPtr;
    FxPresetLegacyType legacyPreset;

    #if !defined(RP2040_FEATHER) || !defined(EXTENSION_BOARD)
    (void)presetPos;
    return 1;
    #endif

    presetArrayPtr = (uint8_t*)preset;
    address = getPresetAddress(presetPos);
    eeprom24lc128ReadArray(address,sizeof(FxPresetType),presetArrayPtr);
    for (uint8_t c=0;c<sizeof(FxPresetType)-2;c++)
    {
        cs += *(presetArrayPtr + c);
    }
    if (cs==preset->magicNr)
    {
        sanitizePreset(preset);
        presetSyncActiveSlotToLegacy(preset);
        updateActiveFxChainFromPreset(preset);
        return 0;
    }

    // fallback: try loading and migrating old single-slot preset layout
    legacyPresetArrayPtr = (uint8_t*)&legacyPreset;
    address = getLegacyPresetAddress(presetPos);
    eeprom24lc128ReadArray(address,sizeof(FxPresetLegacyType),legacyPresetArrayPtr);
    for (uint8_t c=0;c<sizeof(FxPresetLegacyType)-2;c++)
    {
        legacyCs += *(legacyPresetArrayPtr + c);
    }
    if (legacyCs == legacyPreset.magicNr)
    {
        if (legacyPreset.programNr >= N_FX_PROGRAMS)
        {
            return 1;
        }

        migrateLegacyPreset(preset,&legacyPreset);
        sanitizePreset(preset);
        presetSyncActiveSlotToLegacy(preset);
        updateActiveFxChainFromPreset(preset);
        savePreset(preset,presetPos);
        return 0;
    }

    return 1;
}

void applyPreset(FxPresetType* preset,FxProgramType ** programs)
{
    uint8_t nParams;
    uint8_t programNr;

    sanitizePreset(preset);

    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        programNr = preset->slotProgramNr[s];
        if (programNr >= N_FX_PROGRAMS)
        {
            programNr = N_FX_PROGRAMS-1;
            preset->slotProgramNr[s] = programNr;
        }
        nParams = (*(programs + programNr))->nParameters;
        for (uint8_t c=0;c<nParams;c++)
        {
            if ((*(programs + programNr))->parameters[c].setParameter != 0)
            {
                (*(programs + programNr))->parameters[c].setParameter(
                    preset->slotParameters[s][c],
                    (*(programs + programNr))->data
                );
            }
            (*(programs + programNr))->parameters[c].rawValue = preset->slotParameters[s][c];
        }
    }
    presetSyncActiveSlotToLegacy(preset);
    updateActiveFxChainFromPreset(preset);
}

void parametersToPreset(FxPresetType* preset,FxProgramType ** programs)
{
    uint8_t nParams,slot;

    sanitizePreset(preset);
    slot = preset->activeSlot;
    if (preset->programNr >= N_FX_PROGRAMS)
    {
        preset->programNr = N_FX_PROGRAMS-1;
    }
    preset->slotProgramNr[slot] = preset->programNr;
    nParams = (*(programs+preset->programNr))->nParameters;
    for (uint8_t c=0;c<nParams;c++)
    {
        preset->slotParameters[slot][c] = (*(programs + preset->programNr))->parameters[c].rawValue;
    }
    for (uint8_t c=nParams;c<FXPROGRAM_MAX_PARAMETERS;c++)
    {
        preset->slotParameters[slot][c] = 0;
    }
    presetSyncActiveSlotToLegacy(preset);
    updateActiveFxChainFromPreset(preset);
}

void generateEmptyPreset(FxPresetType* preset,uint8_t bank,uint8_t pos)
{
    char nrbfr[8];

    preset->bankNr = bank;
    preset->bankPos = pos;
    preset->name[0] = 0;
    appendToString(preset->name,"B");
    UInt8ToChar(bank,nrbfr);
    appendToString(preset->name,nrbfr);
    appendToString(preset->name," P");
    UInt8ToChar(pos,nrbfr);
    appendToString(preset->name,nrbfr);
    appendToStringUntil(preset->name,"        ",8);
    preset->activeSlot = 0;
    for (uint8_t s=0;s<FX_PRESET_CHAIN_SLOTS;s++)
    {
        preset->slotProgramNr[s] = N_FX_PROGRAMS -1; // off should always be last
        for (uint8_t c=0;c< FXPROGRAM_MAX_PARAMETERS; c++)
        {
            preset->slotParameters[s][c] = 0;
        }
    }
    presetSyncActiveSlotToLegacy(preset);
    preset->ledColor = 1;

}
 