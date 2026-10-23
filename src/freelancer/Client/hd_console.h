// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll -- Freelancer: HD Edition's single-player command console in the chat box
// (Enter, then "help"). A first subset of its commands; see freelancer-mac's docs/mods.md.
#pragma once

#include "hd_plugins.h"

inline bool HdConsoleEnabled()
{
    static const bool bEnabled = HdPlugins::Enabled("console.dll");
    return bEnabled;
}

// Runs one console line typed in single player; its output goes to the chat window.
void HdConsoleExecute(wchar_t const* Line);
