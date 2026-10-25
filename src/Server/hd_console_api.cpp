// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: the Server.dll entry points Freelancer: HD Edition's console uses (hd_console_api.h).

#include "hd_console_api.h"
#include "common_server.h"
#include "server_playerdb.h"  // Players
#include "server_savegame.h"  // PlayerData_SaveGame

// pub:: functions server_pub.cpp defines without a header declaration (exported by name in
// retail; Content.dll and plugins call them).
namespace pub
{
    namespace Player
    {
        SERVER_API int __cdecl GetBase(const unsigned int& player_id, unsigned int& out_base_id);
        SERVER_API int __cdecl GetSystem(const unsigned int& player_id, unsigned int& out_system_id);
        SERVER_API int __cdecl SetMonkey(unsigned int player_id);
        SERVER_API int __cdecl SetRobot(unsigned int player_id);
        SERVER_API int __cdecl SetTrent(unsigned int player_id);
    }
}

namespace
{
    int InspectCash(unsigned Player, int* OutCash)
    {
        return pub::Player::InspectCash(Player, *OutCash);
    }
    int AdjustCash(unsigned Player, int Amount)
    {
        return pub::Player::AdjustCash(Player, Amount);
    }
    int GetShip(unsigned Player, unsigned* OutShip)
    {
        return pub::Player::GetShip(Player, *OutShip);
    }
    int GetPlayerVibe(unsigned Player, int* OutVibe)
    {
        return pub::Player::GetRep(Player, *OutVibe);
    }
    int GetGroup(char const* Nickname, unsigned* OutGroup)
    {
        return pub::Reputation::GetReputationGroup(*OutGroup, Nickname);
    }
    int GetRep(int Vibe, unsigned Group, float* OutValue)
    {
        return pub::Reputation::GetReputation(Vibe, Group, *OutValue);
    }
    int SetRep(int Vibe, unsigned Group, float Value)
    {
        return pub::Reputation::SetReputation(Vibe, Group, Value);
    }
    int GetLocation(unsigned Ship, Vector* OutPos, Matrix* OutOrient)
    {
        return pub::SpaceObj::GetLocation(Ship, *OutPos, *OutOrient);
    }
    int Relocate(unsigned Ship, unsigned System, Vector const* Pos, Matrix const* Orient)
    {
        return pub::SpaceObj::Relocate(Ship, System, *Pos, *Orient);
    }
    int GetSystem(unsigned Ship, unsigned* OutSystem)
    {
        return pub::SpaceObj::GetSystem(Ship, *OutSystem);
    }
    int SetInvincible(unsigned Ship, bool bInvincible)
    {
        return pub::SpaceObj::SetInvincible(Ship, bInvincible, false, 0.0f);
    }
    int Save(unsigned Player, unsigned SaveType)
    {
        return pub::Save(Player, SaveType);
    }
    int SaveNamed(unsigned Player, char const* Name, wchar_t const* Description)
    {
        if (!SinglePlayer() || !Players.is_valid(Player))
            return -2;
        return PlayerData_SaveGame(Players[Player], Name, Description, 0) ? 0 : -1;
    }
    int GetShipArch(unsigned Player, unsigned* OutArch)
    {
        return pub::Player::GetShipID(Player, *OutArch);
    }
    int GetBase(unsigned Player, unsigned* OutBase)
    {
        return pub::Player::GetBase(Player, *OutBase);
    }
    int GetPlayerSystem(unsigned Player, unsigned* OutSystem)
    {
        return pub::Player::GetSystem(Player, *OutSystem);
    }
    int ForceLand(unsigned Player, unsigned Base)
    {
        return pub::Player::ForceLand(Player, Base);
    }
    int SetShipAndLoadout(unsigned Player, unsigned Arch, EquipDescVector const* Loadout)
    {
        return pub::Player::SetShipAndLoadout(Player, Arch, *Loadout);
    }
    int GetTarget(unsigned Ship, unsigned* OutTarget)
    {
        return pub::SpaceObj::GetTarget(Ship, *OutTarget);
    }
    int GetRadius(unsigned Obj, float* OutRadius, Vector* OutCenter)
    {
        return pub::SpaceObj::GetRadius(Obj, *OutRadius, *OutCenter);
    }
    int GetBasePosition(unsigned System, unsigned Base, Vector* OutPos)
    {
        return pub::GF::GetBasePosition(System, Base, *OutPos);
    }
    int SetCostume(unsigned Player, int Which)
    {
        if (Which == 1)
            return pub::Player::SetMonkey(Player);
        if (Which == 2)
            return pub::Player::SetRobot(Player);
        return pub::Player::SetTrent(Player);
    }

    int JumpSave(unsigned Player, char const* Name, wchar_t const* Description, unsigned System,
                 Vector const* Pos, Matrix const* Orient)
    {
        if (!SinglePlayer() || !Players.is_valid(Player))
            return -2;
        PlayerData& Slot = Players[Player];
        unsigned Ship = 0;
        if (pub::Player::GetShip(Player, Ship) != 0 || Ship == 0)
            return -2;
        Vector OldPos;
        Matrix OldOrient;
        unsigned OldSystem = 0;
        if (pub::SpaceObj::GetLocation(Ship, OldPos, OldOrient) != 0 ||
            pub::SpaceObj::GetSystem(Ship, OldSystem) != 0)
            return -2;
        pub::SpaceObj::Relocate(Ship, OldSystem, *Pos, *Orient); // the same system: a beam
        unsigned const SavedSystem = Slot.system_id;
        Slot.system_id = System;
        bool const bSaved = PlayerData_SaveGame(Slot, Name, Description, 0);
        Slot.system_id = SavedSystem;
        pub::SpaceObj::Relocate(Ship, OldSystem, OldPos, OldOrient);
        return bSaved ? 0 : -1;
    }

    FHdConsoleServerApi const GApi = {InspectCash,  AdjustCash,     GetShip,         GetPlayerVibe,
                                      GetGroup,     GetRep,         SetRep,          GetLocation,
                                      Relocate,     GetSystem,      SetInvincible,   Save,
                                      SaveNamed,    GetShipArch,    GetBase,         GetPlayerSystem,
                                      ForceLand,    SetShipAndLoadout, GetTarget,    GetRadius,
                                      GetBasePosition, SetCostume, JumpSave};
}

extern "C" __declspec(dllexport) FHdConsoleServerApi const* HdConsoleServerApi()
{
    return &GApi;
}
