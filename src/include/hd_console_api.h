// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Freelancer: HD Edition's console.dll (Jason Hood's single-player console 1.24) imports
// Server.dll's pub:: functions directly and patches three of its functions in place. The port's
// client does not link Server, so Server exports this table by a plain C name and the client's
// console (src/freelancer/Client/hd_console.cpp) resolves it with GetProcAddress in single
// player. Every entry names the import or patch it stands for.
#pragma once

#include "common_csys.h" // Matrix
#include "common_ini.h"  // Vector

struct EquipDescVector; // common_goods.h
struct FmtStr;          // common_text.h

// One object of pub::System::EnumerateObjects (its FSysObj record, the fields console.dll reads).
struct FHdConsoleSysObj
{
    unsigned Id;          // CreateID(Nickname)
    char const* Nickname; // FSysObj +0x00
    int IdsName;          // FSysObj +0x30
    unsigned ArchetypeId; // FSysObj +0x2c
};
// Returns false to stop the enumeration (SysObjEnumerator slot 0 / Reputation::Enumerator slot 0).
typedef bool (*FHdConsoleSysObjFn)(FHdConsoleSysObj const* Object, void* Arg);
typedef bool (*FHdConsoleGroupFn)(unsigned Group, void* Arg);
typedef void (*FHdConsoleSaveFileFn)(char const* Path);

struct FHdConsoleServerApi
{
    // --- pub::Player ---
    int (*InspectCash)(unsigned Player, int* OutCash);
    int (*AdjustCash)(unsigned Player, int Amount);
    int (*GetShip)(unsigned Player, unsigned* OutShip);
    int (*GetShipArch)(unsigned Player, unsigned* OutArch); // pub::Player::GetShipID
    int (*GetBase)(unsigned Player, unsigned* OutBase);
    int (*GetPlayerSystem)(unsigned Player, unsigned* OutSystem);
    int (*GetPlayerVibe)(unsigned Player, int* OutVibe); // pub::Player::GetRep
    int (*GetAssetValue)(unsigned Player, float* OutValue);
    int (*ForceLand)(unsigned Player, unsigned Base);
    int (*SetShipAndLoadout)(unsigned Player, unsigned Arch, EquipDescVector const* Loadout);
    // Which: 0 Trent, 1 monkey, 2 robot (pub::Player::SetTrent / SetMonkey / SetRobot).
    int (*SetCostume)(unsigned Player, int Which);
    int (*PopUpDialog)(unsigned Player, FmtStr const* Title, FmtStr const* Text, unsigned Buttons);
    // CostumeDescriptions::get_costume(Costume, Players[1]+0x2e4): the base costume (FUN_06116d10).
    void (*SetBaseCostume)(unsigned Player, unsigned Costume);
    // --- pub::Reputation ---
    int (*GetGroup)(char const* Nickname, unsigned* OutGroup); // GetReputationGroup
    int (*GetRep)(int Vibe, unsigned Group, float* OutValue);
    int (*SetRep)(int Vibe, unsigned Group, float Value);
    int (*EnumerateGroups)(FHdConsoleGroupFn Fn, void* Arg);
    int (*GetShortGroupName)(unsigned Group, unsigned* OutIds);
    // --- pub::SpaceObj ---
    int (*GetLocation)(unsigned Obj, Vector* OutPos, Matrix* OutOrient);
    int (*Relocate)(unsigned Ship, unsigned System, Vector const* Pos, Matrix const* Orient);
    int (*GetSystem)(unsigned Obj, unsigned* OutSystem);
    int (*GetTarget)(unsigned Ship, unsigned* OutTarget);
    int (*GetType)(unsigned Obj, unsigned* OutType);
    int (*GetRadius)(unsigned Obj, float* OutRadius, Vector* OutCenter);
    int (*GetBurnRadius)(unsigned Obj, float* OutRadius, Vector* OutCenter);
    int (*GetSolarArchetypeID)(unsigned Obj, unsigned* OutArch);
    int (*IsPosEmpty)(unsigned System, Vector const* Pos, float Radius, bool* OutEmpty);
    // --- pub::System ---
    int (*EnumerateObjects)(unsigned System, FHdConsoleSysObjFn Fn, void* Arg);
    // --- pub::Save and the save-game writer ---
    int (*Save)(unsigned Player, unsigned SaveType);
    // The named save console.dll reaches through pub::Save - 0x127a0 (Server.dll's FUN_06d4c800,
    // PlayerData_SaveGame): Name is the file name with ".fl", Description its listed text.
    // Returns 1 when saved, 0 when not (the DLL adds the result to IDS 101).
    int (*SaveNamed)(unsigned Player, char const* Name, wchar_t const* Description);
    // --- what console.dll reads off PlayerData directly ---
    // Players[1]+0x268 == 1.0 and every collision group (+0x270 list) and equipment item
    // (+0x27c list) at health 1.0 (FUN_06113ee0, the "Repair damage." test).
    bool (*IsRepaired)(unsigned Player);
    // Players[1]+0x380, the weapon-group lines the client sent (SET_WEAPON_GROUP). Returns the
    // length; Buffer may be null.
    unsigned (*GetWeaponGroups)(unsigned Player, char* Buffer, unsigned Size);
    void (*SetWeaponGroups)(unsigned Player, char const* Text);
    // --- the patches ---
    // console.dll's "system" command patches IServerImpl::SystemSwitchOutComplete (0x06cef600:
    // 0x06cef6d7 je -> jmp, 0x06cef719 mov ebx,<system>, 0x06cef85d the arrival call, 0x06cef866
    // the branch, 0x06cef8b0.. the default position and orientation, 0x06cef988, 0x06cef9c8 the
    // autosave) so that Player's next switch goes to System: at Pos / Orient (Object 0), or at
    // console.dll's standoff from Object (0x061133a0), without the autosave. One shot.
    void (*SetSystemSwitch)(unsigned Player, unsigned System, unsigned Object, Vector const* Pos,
                            Matrix const* Orient);
    // console.dll's ship store file "<save>.fls": written beside every save the server writes
    // (its call at 0x06d4d275 in FUN_06d4ccd0) and deleted with the character's file
    // (0x06d4a177 in FUN_06d49f60). The hooks get the save file's path.
    void (*SetSaveFileHooks)(FHdConsoleSaveFileFn OnWritten, FHdConsoleSaveFileFn OnDeleted);
};

extern "C" FHdConsoleServerApi const* HdConsoleServerApi();

// Server-internal (src/Server/hd_console_api.cpp): the state behind the patches, read where
// the patched code was.
bool HdConsoleTakeSystemSwitch(unsigned Player, unsigned* OutSystem, unsigned* OutObject, Vector* OutPos,
                               Matrix* OutOrient);
void HdConsoleSystemSwitchStandoff(unsigned Object, Vector* OutPos, Matrix* OutOrient);
void HdConsoleOnSaveWritten(char const* Path);
void HdConsoleOnSaveDeleted(char const* Path);
