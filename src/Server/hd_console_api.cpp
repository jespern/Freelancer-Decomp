// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: the Server.dll entry points Freelancer: HD Edition's console uses (hd_console_api.h).

#include "hd_console_api.h"
#include "common_server.h"

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

    FHdConsoleServerApi const GApi = {InspectCash, AdjustCash, GetShip, GetPlayerVibe, GetGroup, GetRep,
                                      SetRep, GetLocation, Relocate, GetSystem, SetInvincible, Save};
}

extern "C" __declspec(dllexport) FHdConsoleServerApi const* HdConsoleServerApi()
{
    return &GApi;
}
