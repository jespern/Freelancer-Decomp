// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Freelancer: HD Edition's console.dll calls Server.dll's pub:: functions directly. The port's
// client does not link Server, so Server exports this table by a plain C name and the client's
// console (src/freelancer/Client/hd_console.cpp) resolves it with GetProcAddress in single player.
#pragma once

#include "common_csys.h" // Matrix
#include "common_ini.h"  // Vector

struct EquipDescVector; // common_goods.h

struct FHdConsoleServerApi
{
    int (*InspectCash)(unsigned Player, int* OutCash);
    int (*AdjustCash)(unsigned Player, int Amount);
    int (*GetShip)(unsigned Player, unsigned* OutShip);
    int (*GetPlayerVibe)(unsigned Player, int* OutVibe);
    int (*GetGroup)(char const* Nickname, unsigned* OutGroup);
    int (*GetRep)(int Vibe, unsigned Group, float* OutValue);
    int (*SetRep)(int Vibe, unsigned Group, float Value);
    int (*GetLocation)(unsigned Ship, Vector* OutPos, Matrix* OutOrient);
    int (*Relocate)(unsigned Ship, unsigned System, Vector const* Pos, Matrix const* Orient);
    int (*GetSystem)(unsigned Ship, unsigned* OutSystem);
    int (*SetInvincible)(unsigned Ship, bool bInvincible);
    int (*Save)(unsigned Player, unsigned SaveType);
    // The second subset (save/load, jump/system/base, ship, costumes).
    // A named save of Player's game (PlayerData_SaveGame: Name is the file name without ".fl",
    // Description its listed text), in a base or in space; 0 on success.
    int (*SaveNamed)(unsigned Player, char const* Name, wchar_t const* Description);
    int (*GetShipArch)(unsigned Player, unsigned* OutArch);
    int (*GetBase)(unsigned Player, unsigned* OutBase);
    int (*GetPlayerSystem)(unsigned Player, unsigned* OutSystem);
    int (*ForceLand)(unsigned Player, unsigned Base);
    int (*SetShipAndLoadout)(unsigned Player, unsigned Arch, EquipDescVector const* Loadout);
    int (*GetTarget)(unsigned Ship, unsigned* OutTarget);
    int (*GetRadius)(unsigned Obj, float* OutRadius, Vector* OutCenter);
    int (*GetBasePosition)(unsigned System, unsigned Base, Vector* OutPos);
    // Which: 0 Trent, 1 monkey, 2 robot (pub::Player::SetTrent / SetMonkey / SetRobot).
    int (*SetCostume)(unsigned Player, int Which);
    // A named save that puts the player's ship at Pos / Orient in another system, for the
    // client to load: the single-player server has only the current system loaded, so a ship
    // cannot be relocated into another one live. The ship is beamed to Pos for the save and
    // back; 0 on success.
    int (*JumpSave)(unsigned Player, char const* Name, wchar_t const* Description, unsigned System,
                    Vector const* Pos, Matrix const* Orient);
};

extern "C" FHdConsoleServerApi const* HdConsoleServerApi();
