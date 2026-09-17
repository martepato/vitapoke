/* TEST ONLY (QOL_TEST=<mode> make variable, off by default; never in a release).
   Wraps Repel_UpdateSteps (called from field_control.o on every player step).
   First step: logs an evolution/friendship/item table through the real game functions,
   forces text speed FAST, and prepares the mode's scenario.
   mode 1: Repel runs out 2 steps later with 2 Repels in the bag.
   mode 2: party slot 0 becomes an Onix, a Metal Coat is added to the bag. */
#include <nitro.h>
#include <string.h>
#include "constants/heap.h"
#include "generated/items.h"
#include "generated/species.h"
#include "field/field_system.h"
#include "bag.h"
#include "game_options.h"
#include "save_player.h"
#include "item.h"
#include "party.h"
#include "pokemon.h"
#include "savedata.h"
#include "special_encounter.h"

#ifndef VITAPOKE_QOL_TEST_MODE
#define VITAPOKE_QOL_TEST_MODE 0
#endif
extern void VitaNativeMemLog(const char *fmt, ...);
BOOL __real_Repel_UpdateSteps(SaveData *saveData, FieldSystem *fieldSystem);
static int sSetup;

static void Evo(u16 species, u8 level, u8 cls, u16 param, u8 friendship)
{
    Pokemon *mon = Pokemon_New(HEAP_ID_FIELD2);
    Pokemon_InitWith(mon, species, level, 32, FALSE, 0, 0, 0);
    u32 base = Pokemon_GetValue(mon, MON_DATA_FRIENDSHIP, NULL);
    if (friendship) { u32 f = friendship; Pokemon_SetValue(mon, MON_DATA_FRIENDSHIP, &f); }
    int type = 0;
    u16 target = Pokemon_GetEvolutionTargetSpecies(NULL, mon, cls, param, &type);
    VitaNativeMemLog("[QOL-TEST] evo species=%u level=%u class=%u param=%u friendship=%u base_friendship=%u -> target=%u type=%d", species, level, cls, param, friendship, base, target, type);
    Heap_Free(mon);
}

BOOL __wrap_Repel_UpdateSteps(SaveData *saveData, FieldSystem *fieldSystem)
{
    Bag *bag = SaveData_GetBag(saveData);
    u8 *steps = SpecialEncounter_GetRepelSteps(SaveData_GetSpecialEncounters(saveData));

    if (!sSetup) {
        sSetup = 1;
        Options *options = SaveData_GetOptions(saveData);
        VitaNativeMemLog("[QOL-TEST] setup mode=%d saved_text_speed=%d", VITAPOKE_QOL_TEST_MODE, Options_TextSpeed(options));
#if VITAPOKE_QOL_TEST_MODE == 4
        Options_SetTextSpeed(options, OPTIONS_TEXT_SPEED_NORMAL);
#elif VITAPOKE_QOL_TEST_MODE == 5
        Options_SetTextSpeed(options, OPTIONS_TEXT_SPEED_SLOW);
#else
        Options_SetTextSpeed(options, OPTIONS_TEXT_SPEED_FAST);
#endif
        /* class 0 = level, 1 = trade, 3 = item */
        Evo(SPECIES_KADABRA, 35, 0, 0, 0); Evo(SPECIES_KADABRA, 36, 0, 0, 0); Evo(SPECIES_KADABRA, 50, 1, 0, 0);
        Evo(SPECIES_MACHOKE, 36, 0, 0, 0); Evo(SPECIES_GRAVELER, 36, 0, 0, 0); Evo(SPECIES_HAUNTER, 36, 0, 0, 0);
        Evo(SPECIES_ONIX, 20, 3, ITEM_METAL_COAT, 0); Evo(SPECIES_SCYTHER, 20, 3, ITEM_METAL_COAT, 0);
        Evo(SPECIES_SEADRA, 40, 3, ITEM_DRAGON_SCALE, 0); Evo(SPECIES_SLOWPOKE, 20, 3, ITEM_KINGS_ROCK, 0);
        Evo(SPECIES_POLIWHIRL, 30, 3, ITEM_KINGS_ROCK, 0); Evo(SPECIES_PORYGON, 20, 3, ITEM_UPGRADE, 0);
        Evo(SPECIES_PORYGON2, 30, 3, ITEM_DUBIOUS_DISC, 0); Evo(SPECIES_RHYDON, 45, 3, ITEM_PROTECTOR, 0);
        Evo(SPECIES_ELECTABUZZ, 30, 3, ITEM_ELECTIRIZER, 0); Evo(SPECIES_MAGMAR, 30, 3, ITEM_MAGMARIZER, 0);
        Evo(SPECIES_DUSCLOPS, 40, 3, ITEM_REAPER_CLOTH, 0); Evo(SPECIES_CLAMPERL, 20, 3, ITEM_DEEPSEATOOTH, 0);
        Evo(SPECIES_CLAMPERL, 20, 3, ITEM_DEEPSEASCALE, 0); Evo(SPECIES_ONIX, 20, 3, ITEM_FIRE_STONE, 0);
        Evo(SPECIES_EEVEE, 20, 3, ITEM_SUN_STONE, 0); Evo(SPECIES_EEVEE, 20, 3, ITEM_MOON_STONE, 0);
        Evo(SPECIES_EEVEE, 20, 3, ITEM_LEAF_STONE, 0); Evo(SPECIES_EEVEE, 20, 3, ITEM_FIRE_STONE, 0);
        Evo(SPECIES_EEVEE, 20, 0, 0, 255);
        Evo(SPECIES_BUDEW, 10, 0, 0, 220); Evo(SPECIES_BUDEW, 10, 0, 0, 219); Evo(SPECIES_CHINGLING, 10, 0, 0, 220);
        Evo(SPECIES_RIOLU, 10, 0, 0, 220); Evo(SPECIES_PICHU, 5, 0, 0, 220); Evo(SPECIES_GOLBAT, 30, 0, 0, 220);
        Evo(SPECIES_CLEFFA, 5, 0, 0, 0); Evo(SPECIES_IGGLYBUFF, 5, 0, 0, 0); Evo(SPECIES_TOGEPI, 5, 0, 0, 0);
        Evo(SPECIES_AZURILL, 5, 0, 0, 0); Evo(SPECIES_HAPPINY, 5, 0, 0, 0); Evo(SPECIES_PIKACHU, 5, 0, 0, 0);
        const u16 items[] = { ITEM_METAL_COAT, ITEM_DEEPSEASCALE, ITEM_FIRE_STONE, ITEM_LEFTOVERS, ITEM_EVERSTONE };
        for (int i = 0; i < 5; i++)
            VitaNativeMemLog("[QOL-TEST] item=%u field_use=%d party_use=%d evolve=%d hold_effect=%d", items[i],
                Item_LoadParam(items[i], ITEM_PARAM_FIELD_USE_FUNC, HEAP_ID_FIELD2), Item_LoadParam(items[i], ITEM_PARAM_PARTY_USE, HEAP_ID_FIELD2),
                Item_LoadParam(items[i], ITEM_PARAM_EVOLVE, HEAP_ID_FIELD2), Item_LoadParam(items[i], ITEM_PARAM_HOLD_EFFECT, HEAP_ID_FIELD2));
#if VITAPOKE_QOL_TEST_MODE >= 3
        *steps = 200; /* text tests: no wild encounter before the save dialog */
#endif
#if VITAPOKE_QOL_TEST_MODE == 1
        *steps = 3;
        Bag_TryAddItem(bag, ITEM_REPEL, 2, HEAP_ID_FIELD2);
#elif VITAPOKE_QOL_TEST_MODE == 2
        Pokemon *mon = Party_GetPokemonBySlotIndex(SaveData_GetParty(saveData), 0);
        Pokemon_InitWith(mon, SPECIES_ONIX, 20, 32, FALSE, 0, 0, 0);
        /* real OT name/ID/met data through the game's own catch path (Summary/evolution screens assert on a blank OT) */
        Pokemon_SetCatchData(mon, SaveData_GetTrainerInfo(saveData), ITEM_POKE_BALL, 0, 0, HEAP_ID_FIELD2);
        Pokemon_CalcLevelAndStats(mon);
        Bag_TryAddItem(bag, ITEM_METAL_COAT, 1, HEAP_ID_FIELD2);
        *steps = 200; /* Repel so the lake wilds (Lv2-6) cannot interrupt the menu route */
#endif
    }

    BOOL r = __real_Repel_UpdateSteps(saveData, fieldSystem);
    VitaNativeMemLog("[QOL-TEST] step repel_steps=%u bag_repel=%u bag_metal_coat=%u started=%d slot0_species=%u", *steps,
        Bag_GetItemQuantity(bag, ITEM_REPEL, HEAP_ID_FIELD2), Bag_GetItemQuantity(bag, ITEM_METAL_COAT, HEAP_ID_FIELD2), r,
        Pokemon_GetValue(Party_GetPokemonBySlotIndex(SaveData_GetParty(saveData), 0), MON_DATA_SPECIES, NULL));
    return r;
}
