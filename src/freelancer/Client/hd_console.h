// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll -- Freelancer: HD Edition's single-player command console in the chat box
// (Enter, then "help"): Jason Hood's (adoxa) console plugin 1.24 (closed source; behaviour
// from the DLL's disassembly, docs/re/hd_console.md). Its command table, the engine functions
// each command calls and the Freelancer.exe / Server.dll / Common.dll bytes it patches are
// reproduced here and at the patched sites, which all test HdConsoleEnabled().
#pragma once

#include "hd_plugins.h"

struct CCharacterSet;
namespace Archetype
{
    struct ICliObjFactory;
}

inline bool HdConsoleEnabled()
{
    static const bool bEnabled = HdPlugins::Enabled("console.dll");
    return bEnabled;
}

// Runs one chat line typed in single player (the DLL's hook of the chat echo, 0x005a6260,
// reads the line from the send buffer DAT_0066d4f2). Output goes to the chat window as an RDL
// list in the DLL's styles; an unknown or ambiguous command, or one with nothing to say, shows
// nothing.
void HdConsoleExecute(wchar_t const* Line);

// --- the DLL's hooks ---

// console.dll registers itself as an IDS resource module after freelancer.ini's [Resources]
// (its hook at 0x005b1cab; FUN_0057d800 on its own instance): its string table and the seven
// help pages. Needs console.dll in bin/ (the mod profile copies it); without it the messages
// come from a copy of the table and "help" has no pages.
void HdConsoleRegisterResources();
// The shipless placeholder ship archetype, loaded after shiparch.ini (the DLL wraps the
// import Archetype::LoadShips, 0x005c6cb8): [Ship] shipless_ship, DA_archetype
// ships\utility\csv\csv_shield.3db.
void HdConsoleLoadShiplessShip(Archetype::ICliObjFactory* Factory);
// True when Arch is the shipless placeholder: GetPlayerShipArchetypeIdRetail answers 0 for it
// to most callers (the DLL's detour of FUN_004c3e10 at 0x004c3e15, by return address).
bool HdConsoleIsShiplessArch(unsigned Arch);
// During the NavBar refresh after park / drive the DLL forces the answer (DAT_0611da1c): 0 or
// 1 while forced, -1 otherwise.
int HdConsoleForcedShipArch();
// StartServer's SetMultiPlayer call (0x005a811b, the import slot the DLL redirects to
// FUN_06117d10): entering single player applies the console's cruise constants and clears the
// chat input target; entering multiplayer restores the originals.
void HdConsoleOnSetMultiPlayer(bool bMultiPlayer);
// CLoginMgr::SelectCharacterById's SelectCharacter call (0x005ab231): the DLL loads the ships
// stored beside that character's file ("<file>.fls") first.
void HdConsoleLoadShipStore(char const* CharacterFile);
// Once per space frame (the DLL's animation thread and its launch hook at 0x062b2a81).
void HdConsoleSpaceTick();

// --- state the patched sites read ---

// "show": the visit flags of every object and zone carry bit 1 (FUN_004c4da0's tail at
// 0x004c4df1 patched to `MOV ECX,[ESP+8]; OR BYTE PTR [ECX],1; RET`).
unsigned char HdConsoleVisitBits();
// "show houses": whether the universe map draws the four House labels (0x0049e799).
bool HdConsoleHouseLabels();
// "show paths": the nav map's patrol-path band filter skipped (0x0049cd34 JNZ -> JMP).
bool HdConsolePatrolPathsUnfiltered();
// "zoom time": the nav map's zoom transition time (0x004944ac / 0x0049f2a7), Default until set.
float HdConsoleNavMapZoomSeconds(float Default);
// "ntb off": DrawContactMarkers' loop skipped (0x004ec087 JLE -> JMP); "ntb sight": the
// bracket fade constant DAT_005d954c is 1.0 (visible only under the mouse); Default otherwise.
bool HdConsoleBracketsOff();
float HdConsoleBracketFade(float Default);
