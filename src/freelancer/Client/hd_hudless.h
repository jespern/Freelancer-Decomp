// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: HUDless.dll (Freelancer: HD Edition's build, v1.02 + HD extras). A second binding of
// Minimize HUD (0x6d) or the log key (0x67) with Shift, Ctrl or Alt (the player adds it under
// Controls; key bindings match modifiers exactly) steps through "hide the HUD -> also hide the
// cursor -> show both": game options 9 (HUD icons), 10 (HUD), 0x16 (scroll bars) and 0xe (cursor).
// While hidden HD's build also stops the comm window, incoming chat, join / leave notices and
// the target wireframe. Header-only; the state is one process-wide flag.
#pragma once

#include "game_options.h"
#include "hd_plugins.h"
#include "hud_command_dispatch.h"

namespace HdHudless
{
    inline bool& HiddenFlag()
    {
        static bool bHidden = false;
        return bHidden;
    }

    inline bool Hidden()
    {
        return HiddenFlag();
    }

    // HUDless' toggle(): true when it took the key (a modifier was held).
    inline bool Toggle()
    {
        static const bool bEnabled = HdPlugins::Enabled("HUDless.dll");
        if (!bEnabled || HudCommandRecord()->Modifiers == 0)
            return false;
        int Value = 0;
        if (GetGameOption(9) == 0)
        {
            Value = GetGameOption(0xe) == 0 ? 1 : 0;
            SetGameOption(0xe, Value);
        }
        SetGameOption(9, Value);
        SetGameOption(10, Value);
        SetGameOption(0x16, Value);
        HiddenFlag() = Value == 0;
        return true;
    }
}
