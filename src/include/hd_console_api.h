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
};

extern "C" FHdConsoleServerApi const* HdConsoleServerApi();
