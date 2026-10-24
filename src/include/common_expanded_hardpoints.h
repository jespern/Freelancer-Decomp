// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements the expanded_hardpoints module of FLUF, the Freelancer Unified
// Framework (TheStarport, https://codeberg.org/TheStarport/FLUF), as Discovery Freelancer ships
// it, in the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Hardpoint subtypes: equipment classes beyond the hp_type ones, from three keys in the data.
//
//   equipment   hp_subtype = mine_fighter[, ...]              the item's subtypes
//   [Ship]      hp_subtype = mine_fighter, HpMine01[, ...]    hardpoints that take a subtype
//               internal_hp_subtype = player_armor_fighter    subtypes its internal equipment
//                                                             (armour, ...) may have
//               hp_info = 7606, HpDecor01[, ...]              the caption ids those hardpoints
//                                                             show in the equipment screen
//
// An item without a subtype follows the retail rules. One with a subtype fits a ship only through
// a ship line of the same subtype (a hardpoint item) or the ship's internal_hp_subtype (internal
// equipment). FLUF also makes goods priced 0 part of the ship: they cannot be sold, unmounted or
// moved to a new ship. The client side is the equipment screens (nn_inventory, nn_dealer,
// ship_view_control, nn_help); the server side is PlayerData's add-item path (server_classes).
//
// A game folder turns it on with the [Libraries] line "expanded_hardpoints.dll" in DACOM.INI (the
// port's built-in plugins, hd_plugins.h; Discovery's profile writes it). Without it every query
// below answers as retail would.
#pragma once

#include "common_exports.h"

class EquipDescList;

namespace ExpandedHardpoints
{
    // Whether this game folder enabled the module.
    COMMON_API bool Enabled();

    // DataConfig's [Data] equipment / ships files, as it loads them (common_dataconfig.cpp). The
    // subtype keys are read from these, on first use.
    COMMON_API void NoteDataFile(bool bShips, char const* Path);

    // Whether ArchId may go on ShipArchId (FLUF IsValidSubclassItem). Items without a subtype
    // always may. With List, a hardpoint item also needs one of its subtype hardpoints free in it.
    COMMON_API bool IsValidItem(unsigned ArchId, unsigned ShipArchId, EquipDescList const* List);

    // The internal-equipment half alone: ArchId without a subtype, or one of ShipArchId's
    // internal_hp_subtype values among its subtypes.
    COMMON_API bool IsValidInternal(unsigned ShipArchId, unsigned ArchId);

    // The equipment screen's hardpoint for ArchId on ShipArchId when both have subtypes: the first
    // of the ship's hardpoints (in the model's order, Count of them, Name(Index) / bFree(Index))
    // whose name one of the item's subtypes lists. Null when the subtypes do not decide it -- the
    // caller then searches as retail does.
    COMMON_API char const* FindSubtypeHardpoint(unsigned ArchId, unsigned ShipArchId, int Count,
                                                char const* (*Name)(void* Context, int Index),
                                                bool (*bFree)(void* Context, int Index),
                                                void* Context);

    // The caption ids the ship's hp_info gives Hardpoint, 0 for none.
    COMMON_API unsigned HardpointInfoIds(unsigned ShipArchId, char const* Hardpoint);

    // The server's choice for an item added on Hardpoint (FLUF's Server.dll patch of
    // PlayerData's add-item): Hardpoint itself, another free subtype hardpoint of the ship, or
    // the cargo bay with *bUnequip set. Returns null to leave the item's hardpoint untouched (an
    // internal item the ship's internal_hp_subtype refuses; it is unequipped too).
    COMMON_API char const* ServerHardpoint(unsigned ArchId, unsigned ShipArchId, char const* Hardpoint,
                                           EquipDescList const* List, bool* bUnequip);

    // Whether ArchId's good is priced 0 (or has no good): FLUF's mark of equipment that is part of
    // the ship.
    COMMON_API bool IsBuiltIn(unsigned ArchId);
}
