// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#include "globals.h"
#include "hd_plugins.h"
#include "input_manager.h"
#include "menu_keymap_config.h" // KeyMapRouteInputMessage (FUN_00577850)

#include "imeui.h"

#include <windows.h>

// FUN_005b0b60 (.text:005b0b60-005b0c20), cdecl(int* ExitCode): drain the queue with
// PeekMessage(PM_NOREMOVE) + GetMessage. A hot key IMEFocus claims, or a message the menu
// accelerator router (FUN_00577850) consumes, skips Translate/Dispatch. WM_QUIT stores its
// wParam through ExitCode (no null check, 0x005b0c18) and returns false.
namespace
{
    // HD: FLSharp quit_message_fix (exit.cpp) -- a WM_QUIT was retrieved by some pump.
    bool G_bQuitRetrieved = false;
}

bool PumpMessages(int* exit_code)
{
    // HD: FLSharp quit_message_fix — once any pump has taken the WM_QUIT, every later call
    // answers false too, so a nested pump (NN_Error, NN_Prompt) cannot swallow the quit.
    static const bool bLatchQuit = HdPlugins::FlSharp("quit_message_fix");

    // 0x005b0b69: FUN_0041ef20 drains the DirectInput mouse queue first.
    MouseDispatchEvents();
    MSG Msg;
    while (PeekMessageA(&Msg, nullptr, 0, 0, PM_NOREMOVE))
    {
        if (!GetMessageA(&Msg, nullptr, 0, 0))
        {
            *exit_code = static_cast<int>(Msg.wParam);
            G_bQuitRetrieved = true;
            return false;
        }

        IMEFocus* const Ime = IMEFocus::GetSingleton();
        if ((Ime == nullptr || !Ime->IgnoreHotKey(&Msg)) &&
            !KeyMapRouteInputMessage(Msg.message, static_cast<unsigned>(Msg.wParam),
                                     static_cast<unsigned>(Msg.lParam)))
        {
            TranslateMessage(&Msg);
            DispatchMessageA(&Msg);
        }
    }
    return !(bLatchQuit && G_bQuitRetrieved);
}
