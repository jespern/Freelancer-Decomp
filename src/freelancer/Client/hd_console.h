// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll -- Freelancer: HD Edition's single-player command console in the chat box
// (Enter, then "help"). The console is Jason Hood's (adoxa) console plugin v1.24, which HD
// Edition ships; its commands are defined here from the plugin's own built-in help text and
// readme (EXE/console_readme.txt), the source not being published. Ported: the GAME, MOVE,
// HUD (nav map) and PLAYER groups, godmode, and the port's own "ship" command; see
// freelancer-mac's docs/mods.md for what is left out.
#pragma once

#include "hd_plugins.h"

inline bool HdConsoleEnabled()
{
    static const bool bEnabled = HdPlugins::Enabled("console.dll");
    return bEnabled;
}

// Runs one console line typed in single player; its output goes to the chat window.
void HdConsoleExecute(wchar_t const* Line);

// The nav-map commands' state, read by the nav map and the visit map:
// "show" / "show paths": visit bits (1 visited, 4 patrol path) every object and zone counts as
// having, OR'ed into GetPlayerVisitFlagsRetail's answer; 0 when off.
unsigned char HdConsoleVisitBits();
// "show houses": whether the universe map draws the four House labels (default on).
bool HdConsoleHouseLabels();
// "zoom time": the nav map's zoom transition time, Default (1.5 s) until set.
float HdConsoleNavMapZoomSeconds(float Default);
