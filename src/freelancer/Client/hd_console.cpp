// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll 1.24 (hd_console.h). Every command below names the DLL function it
// reproduces (console.dll is based at 0x06110000); the engine functions are the DLL's imports
// (Common.dll, Server.dll's pub::, DALib::Anim) and the Freelancer.exe addresses in its table
// at 0x0611d030. The command table (0x0611d0c0) and its lookup are FUN_06116df0; messages are
// the DLL's string table (resource ids 100..148), help its RDL pages (1000..1006).
// docs/re/hd_console.md has the recovered behaviour command by command; docs/re/deviations.md
// what the port does differently (the system switch's mechanics, "xfer").

#include "hd_console.h"

#include "character_select_equip.h" // GetPlayerEquipDescListRetail, GetPlayerShipArchetypeIdRaw
#include "chat_init.h"              // ChatAcquireEntry, G_ChatInputTarget
#include "client_gameobj.h"         // FClientGameobj
#include "client_state.h"           // QueueLoadGameCharacter (FUN_005b2c70)
#include "game_options.h"           // GetGameOption / SetGameOption (DISABLE_COLLIDE)
#include "ids_rdl.h"
#include "ids_strings.h"            // GetIdsStringW, RegisterIdsResourceModule
#include "login_mgr.h"              // G_bCommandLineG
#include "menu_universe_base.h"     // LocationExit / BaseEnter (FUN_0043b3e0 / FUN_0043b290)
#include "misc_util.h"              // FormatGroupedNumber (FUN_004779a0)
#include "menu_sound_arch.h"        // LookupClientSoundArch (FUN_0042ae40), PlayClientSoundArchIfIdle
#include "nav_bar.h"                // NavBarEnsure (FUN_00442060), NavBarLaunch (FUN_00440f94)
#include "nn_arrival_text.h"        // NnArrivalTextShowBase (FUN_0045b2d0)
#include "nn_modal.h"               // NnMainButtonOnModalOpen (FUN_0045a460, NN_MainButton::ClosePanel)
#include "nn_story_star.h"          // NnStoryStarLoadAutosave (FUN_004bc830)
#include "nn_ui_pulse.h"            // NnReputationColorArgb (FUN_0045a650), G_NnPaletteArgb_679bac
#include "player_behavior.h"        // GetPlayerObjRW (FUN_0054baf0)
#include "player_spawn.h"           // GetPlayerConnectionId
#include "space_state.h"            // SetPendingJumpSystem (DAT_0067977c)

#include "DALib.h"              // DALib::Anim
#include "common_animdb.h"      // AnimDB::Add / Rem
#include "common_archetype.h"   // Archetype::GetShip / GetSolar / LoadShips
#include "common_cobject.h"     // CShip, HdConsoleLaunchFromDockPoint
#include "common_goods.h"       // EquipDescVector, Loadout::ReadEquipLine, EngineEquipConsts
#include "common_ini.h"         // INI_Reader
#include "common_misc.h"        // SinglePlayer, CreateID, GetUserDataPath
#include "common_physys.h"      // PhySys::ANOM_LIMITS_MAX_VELOCITY
#include "common_reputation.h"  // GetCostumeDescriptions, Reputation::get_info_card
#include "common_text.h"
#include "common_universe.h"
#include "hd_console_api.h"
#include "hud_weapon_groups.h" // G_WeaponGroups (SET_WEAPON_GROUP, LAB_0053c310)

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    // ----------------------------------------------------------------------------------------
    // Server.dll (hd_console_api.h), resolved again whenever the module changes.
    // ----------------------------------------------------------------------------------------
    FHdConsoleServerApi const* ServerApi()
    {
        static HMODULE Module = nullptr;
        static FHdConsoleServerApi const* Api = nullptr;
        HMODULE const Server = GetModuleHandleA("server.dll");
        if (Server != Module || Api == nullptr)
        {
            Module = Server;
            typedef FHdConsoleServerApi const* (*FGetApi)();
            FGetApi const Get = Server != nullptr
                                    ? reinterpret_cast<FGetApi>(GetProcAddress(Server, "HdConsoleServerApi"))
                                    : nullptr;
            Api = Get != nullptr ? Get() : nullptr;
        }
        return Api;
    }

    // The DLL's player id (DAT_0611b2f8 = 1, the single player).
    unsigned const kPlayer = 1;

    // ----------------------------------------------------------------------------------------
    // Strings: console.dll's string table. Loaded from the DLL once it is registered as a
    // resource module (HdConsoleRegisterResources); this copy is the fallback.
    // ----------------------------------------------------------------------------------------
    enum : unsigned
    {
        IdsGroupTitle = 0, // 0..6: "Console Commands", "Game Commands", ...
        IdsVersion = 100,
        IdsGameNotSaved = 101,
        IdsGameSaved = 102,
        IdsQuickSave = 103,
        IdsGameNotFound = 104,
        IdsObjectNotFound = 105,
        IdsCruiseSpeed = 106,
        IdsCruiseAccel = 107,
        IdsAnimationNotFound = 108,
        IdsBaseNotFound = 109,
        IdsSystemNotFound = 110,
        IdsShips = 111,
        IdsShipNotFound = 112,
        IdsNoShipsHere = 113,
        IdsRepairDamage = 114,
        IdsParked = 115,
        IdsDriveAShip = 116,
        IdsSold = 117,
        IdsSpecifyBase = 118,
        IdsGhostOff = 119,
        IdsGhostOn = 120,
        IdsGodmodeOff = 121,
        IdsGodmodeOn = 122,
        IdsNtbOff = 123,
        IdsNtbOn = 124,
        IdsNtbSight = 125,
        IdsShowOn = 126,
        IdsShowOff = 127,
        IdsHousesOff = 128,
        IdsHousesOn = 129,
        IdsPathsUnfiltered = 130,
        IdsPathsFiltered = 131,
        IdsZoomTime = 132,
        IdsFactionNotFound = 133,
        IdsReputation = 134,
        IdsRepSaved = 135,
        IdsRepLoaded = 136,
        IdsMonkey = 137,
        IdsRobot = 138,
        IdsTrent = 139,
        IdsCostumeNotFound = 140,
        IdsItemTransfer = 141,
        IdsItemTransferHelp = 142,
        IdsClipboardUnavailable = 143,
        IdsPositionCopied = 144,
        IdsBookmarks = 145,
        IdsBookmarkSet = 146,
        IdsBookmarkNotSet = 147,
        IdsSoundNotFound = 148,
        IdsHelpPage = 1000, // 1000..1006 the RDL help pages, by group
        // Freelancer's own resources the DLL uses.
        IdsNone = 0xbce, // "None" (the empty bookmark and ship lists)
    };

    struct FConsoleString
    {
        unsigned Id;
        wchar_t const* Text;
    };
    FConsoleString const kStrings[] = {
        {0, L"Console Commands"},
        {1, L"Game Commands"},
        {2, L"Base Commands"},
        {3, L"Movement Commands"},
        {4, L"Ship Commands"},
        {5, L"HUD Commands"},
        {6, L"Player Commands"},
        {100, L"Version 1.24 (7 May, 2016)."},
        {101, L"Game NOT saved."},
        {102, L"Game saved."},
        {103, L"Quick Save%s"},
        {104, L"Game not found."},
        {105, L"Object not found."},
        {106, L"Cruise speed = %g m/s"},
        {107, L"Cruise accel time = %gs"},
        {108, L"Animation not found."},
        {109, L"Base not found."},
        {110, L"System not found."},
        {111, L"Ships"},
        {112, L"Ship not found."},
        {113, L"No ships here."},
        {114, L"Repair damage."},
        {115, L"Parked %s."},
        {116, L"Drive a ship."},
        {117, L"Sold %s for %s."},
        {118, L"Specify base."},
        {119, L"Ghost: off"},
        {120, L"Ghost: on"},
        {121, L"Godmode: off"},
        {122, L"Godmode: on"},
        {123, L"NTB: off"},
        {124, L"NTB: on"},
        {125, L"NTB: sight"},
        {126, L"Show: on"},
        {127, L"Show: off"},
        {128, L"House labels: off"},
        {129, L"House labels: on"},
        {130, L"Patrol Paths: unfiltered"},
        {131, L"Patrol Paths: filtered"},
        {132, L"Zoom time = %g"},
        {133, L"Faction not found."},
        {134, L"Reputation: %g"},
        {135, L"Rep saved."},
        {136, L"Rep loaded."},
        {137, L"Monkey."},
        {138, L"Robot."},
        {139, L"Trent."},
        {140, L"Costume not found."},
        {141, L"Item Transfer"},
        {142, L"If you want to transfer an item, select it in a list. Set the quantity that you want to "
              L"transfer and click the Transfer button."},
        {143, L"Clipboard unavailable."},
        {144, L"Position copied."},
        {145, L"Bookmarks"},
        {146, L"Bookmark set."},
        {147, L"Bookmark not set."},
        {148, L"Sound not found."},
    };

    unsigned GIdsBase = 0;    // DAT_0611d964: the DLL's ids base once registered
    bool GbResources = false; // whether console.dll's resources are available

    // The scratch the DLL formats into (DAT_0066dc60, 0x1000 wide characters).
    wchar_t GScratch[0x1000];

    // The DLL's string (its resource, else this file's copy) into Buffer.
    wchar_t const* Ids(unsigned Id, wchar_t* Buffer, int Chars)
    {
        Buffer[0] = 0;
        if (GbResources && GetIdsStringW(GIdsBase + Id, Buffer, Chars) != 0)
            return Buffer;
        for (FConsoleString const& Entry : kStrings)
            if (Entry.Id == Id)
            {
                wcsncpy(Buffer, Entry.Text, static_cast<size_t>(Chars));
                Buffer[Chars - 1] = 0;
                return Buffer;
            }
        return Buffer;
    }

    // A Freelancer resource (any registered module) into Buffer.
    wchar_t const* GameIds(unsigned Id, wchar_t* Buffer, int Chars)
    {
        Buffer[0] = 0;
        GetIdsStringW(Id, Buffer, Chars);
        return Buffer;
    }

    // ----------------------------------------------------------------------------------------
    // Output: the RDL list the DLL builds at DAT_0611d968 (FUN_0611a280 appends a node: 1 TRA,
    // 2 TEXT, 5 PARA, 6 STYLE) and hands to the chat (FUN_0046a150) when the command returns
    // true. The dispatcher opens every output with STYLE 0x3ea8 (the system-notice style);
    // "about", the lists and "rep" reset the list and choose their own.
    // ----------------------------------------------------------------------------------------
    RenderDisplayList GOut;
    std::wstring GOutText; // the plain text, for the port's log

    void OutReset()
    {
        RdlClear(GOut);
        GOutText.clear();
    }
    void OutStyle(unsigned short Style) // FUN_0611a350
    {
        StyleNode Node(Style);
        RdlAppendNode(GOut, Node.Clone());
    }
    void OutTra(unsigned Value, unsigned Mask) // FUN_0611a310
    {
        TextRenderAttributes Attrs;
        Attrs.Packed = Value;
        TRANode Node(Attrs, Mask, 0);
        RdlAppendNode(GOut, Node.Clone());
    }
    void OutPara() // FUN_0611a380
    {
        ParagraphNode Node;
        RdlAppendNode(GOut, Node.Clone());
        GOutText += L" | ";
    }
    void OutText(wchar_t const* Text) // FUN_0611a3a0
    {
        TextNode Node(Text, -1);
        RdlAppendNode(GOut, Node.Clone());
        GOutText += Text;
    }
    void OutAscii(char const* Text) // FUN_0611a3f0
    {
        mbstowcs(GScratch, Text, 0x1000);
        GScratch[0xfff] = 0;
        OutText(GScratch);
    }
    void OutIds(unsigned Id) // FUN_0611a430 (the DLL's own ids)
    {
        wchar_t Text[0x1000];
        OutText(Ids(Id, Text, 0x1000));
    }
    void OutGameIds(unsigned Id) // FUN_0611a430 with a game id
    {
        wchar_t Text[0x1000];
        OutText(GameIds(Id, Text, 0x1000));
    }
    void OutFormat(wchar_t const* Format, ...) // FUN_0611a470
    {
        va_list Args;
        va_start(Args, Format);
        _vsnwprintf(GScratch, 0x1000, Format, Args);
        va_end(Args);
        GScratch[0xfff] = 0;
        OutText(GScratch);
    }
    void OutIdsFormat(unsigned Id, ...) // FUN_0611a4b0
    {
        wchar_t Format[0x80];
        Ids(Id, Format, 0x80);
        va_list Args;
        va_start(Args, Id);
        _vsnwprintf(GScratch, 0x1000, Format, Args);
        va_end(Args);
        GScratch[0xfff] = 0;
        OutText(GScratch);
    }

    void OutShow()
    {
        std::string Log;
        for (wchar_t const C : GOutText)
            Log += static_cast<char>(C < 0x80 ? C : '?');
        fprintf(stderr, "[hd console] %s\n", Log.c_str());
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        ChatAppendRdlCopy(Entry->Body, GOut);
        ChatCommitEntries();
    }

    // ----------------------------------------------------------------------------------------
    // Arguments.
    // ----------------------------------------------------------------------------------------

    // FUN_06111060: CreateID of the argument as a narrow string.
    unsigned ArgId(wchar_t const* Text)
    {
        char Narrow[0x80];
        wcstombs(Narrow, Text, 0x80);
        Narrow[0x7f] = 0;
        return CreateID(Narrow);
    }

    // FUN_06111090: one number of a comma list. Empty (at ',' or the end) keeps *Out; a
    // number may carry a k/K suffix (x1000) and must end at a space, a comma or the end. The
    // cursor moves past the separator and the spaces after it. False when the field is not a
    // number.
    bool ParseNumber(float* Out, wchar_t const** Cursor)
    {
        wchar_t const* P = *Cursor;
        if (*P == 0)
            return true;
        if (*P != L',')
        {
            wchar_t* End = nullptr;
            double Value = wcstod(P, &End);
            if (*End == L'k' || *End == L'K')
            {
                Value *= 1000.0;
                ++End;
            }
            if (*End != L' ' && *End != L',' && *End != 0)
                return false;
            *Out = static_cast<float>(Value);
            P = End;
            *Cursor = P;
            if (*P == 0)
                return true;
        }
        ++P; // the separator
        while (*P == L' ')
            ++P;
        *Cursor = P;
        return true;
    }

    // FUN_06111120: three numbers.
    void ParseTriple(float* Out, wchar_t const** Cursor)
    {
        ParseNumber(&Out[0], Cursor);
        ParseNumber(&Out[1], Cursor);
        ParseNumber(&Out[2], Cursor);
    }

    // FUN_06112130: a trailing "*", "**" or "*N" is the match to stop at (1 without one); it
    // is cut off the argument.
    long MatchCount(wchar_t* Text)
    {
        wchar_t* const Star = wcschr(Text, L'*');
        if (Star == nullptr)
            return 1;
        long Count;
        if (Star[1] == 0)
            Count = 2;
        else if (Star[1] == L'*' && Star[2] == 0)
            Count = 3;
        else
        {
            wchar_t* End = nullptr;
            Count = wcstol(Star + 1, &End, 10);
            if (*End != 0)
                return 1;
        }
        *Star = 0;
        return Count;
    }

    // FUN_061121b0: a case-insensitive wcsstr.
    wchar_t const* FindNoCase(wchar_t const* Text, wchar_t const* Needle)
    {
        size_t const TextLen = wcslen(Text), NeedleLen = wcslen(Needle);
        if (TextLen < NeedleLen)
            return nullptr;
        for (size_t At = 0; At + NeedleLen <= TextLen; ++At)
        {
            size_t N = 0;
            while (N < NeedleLen && towlower(Text[At + N]) == towlower(Needle[N]))
                ++N;
            if (N == NeedleLen)
                return Text + At;
        }
        return nullptr;
    }

    // ----------------------------------------------------------------------------------------
    // Angles. FUN_06111150: radians to degrees rounded to two decimals (-0.005..0 -> 0,
    // <= -179.995 -> 180); FUN_06111200: a matrix to pitch, yaw, roll.
    // ----------------------------------------------------------------------------------------
    float RoundDegrees(float Radians)
    {
        float Degrees = Radians * 57.295776f;
        if (Degrees < 0.0f)
        {
            if (Degrees <= -0.005f)
            {
                if (Degrees <= -179.995f)
                    Degrees = 180.0f;
            }
            else
                Degrees = 0.0f;
        }
        double Whole = 0.0;
        double const Fraction = modf(static_cast<double>(Degrees * 100.0f), &Whole);
        if (Fraction >= 0.5)
            return static_cast<float>((static_cast<float>(Whole) + 1.0) * 0.01);
        if (Fraction <= -0.5)
            return static_cast<float>((static_cast<float>(Whole) - 1.0) * 0.01);
        return static_cast<float>(static_cast<float>(Whole) * 0.01);
    }

    void MatrixToEuler(Matrix const& M, float* Out)
    {
        float const Hyp = static_cast<float>(hypot(static_cast<double>(M.E[0][0]), static_cast<double>(M.E[1][0])));
        if (Hyp > 1.9073486e-06f)
        {
            Out[0] = RoundDegrees(static_cast<float>(atan2(M.E[2][1], M.E[2][2])));
            Out[1] = RoundDegrees(static_cast<float>(atan2(-M.E[2][0], Hyp)));
            Out[2] = RoundDegrees(static_cast<float>(atan2(M.E[1][0], M.E[0][0])));
        }
        else
        {
            Out[0] = RoundDegrees(static_cast<float>(atan2(-M.E[1][2], M.E[1][1])));
            Out[1] = RoundDegrees(static_cast<float>(atan2(-M.E[2][0], Hyp)));
            Out[2] = 0.0f;
        }
    }

    Vector MakeVector(float X, float Y, float Z)
    {
        Vector V;
        V.x = X;
        V.y = Y;
        V.z = Z;
        return V;
    }

    Matrix EulerToMatrix(float const* Euler) // EulerMatrix (Common)
    {
        return EulerMatrix(MakeVector(Euler[0], Euler[1], Euler[2]));
    }

    // ----------------------------------------------------------------------------------------
    // The player's ship.
    // ----------------------------------------------------------------------------------------
    unsigned GShipId = 0; // DAT_0611d9fc

    // FUN_061117a0: the player's ship object, 0 when docked.
    bool InSpace()
    {
        GShipId = 0;
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api != nullptr)
            Api->GetShip(kPlayer, &GShipId);
        return GShipId != 0;
    }

    // FUN_061117d0: its location.
    bool ShipLocation(Vector* OutPos, Matrix* OutOrient)
    {
        if (!InSpace())
            return false;
        ServerApi()->GetLocation(GShipId, OutPos, OutOrient);
        return true;
    }

    // The player's CShip (FUN_061123b0: GetPlayerObjRW +0x10), or null.
    CShip* PlayerCShip()
    {
        IObjRW* const Player = static_cast<IObjRW*>(GetPlayerObjRW());
        if (Player == nullptr)
            return nullptr;
        CObject* const Object = static_cast<FClientGameobj*>(Player)->GetObject();
        if (Object == nullptr || (static_cast<unsigned>(Object->ObjectClass) & 0x503u) != 0x503u)
            return nullptr;
        return static_cast<CShip*>(Object);
    }

    // ----------------------------------------------------------------------------------------
    // Bookmarks (DAT_0611d730, ten of system, position, orientation). FUN_06111800 stores
    // one; 0 is stored automatically before any movement, when the ship has moved since.
    // ----------------------------------------------------------------------------------------
    struct FBookmark
    {
        unsigned System = 0;
        Vector Pos;
        Matrix Orient;
    } GBookmarks[10];

    void SetBookmark(int Index)
    {
        Vector Pos;
        Matrix Orient;
        if (!ShipLocation(&Pos, &Orient))
            return;
        unsigned System = 0;
        ServerApi()->GetPlayerSystem(kPlayer, &System);
        Universe::ISystem const* const Info = Universe::get_system(System);
        unsigned const SystemId = Info != nullptr ? Info->GetId() : System;
        FBookmark& Mark = GBookmarks[Index];
        if (Index != 0 || Mark.System != SystemId || Mark.Pos.x != Pos.x || Mark.Pos.y != Pos.y ||
            Mark.Pos.z != Pos.z)
        {
            Mark.System = SystemId;
            Mark.Pos = Pos;
            Mark.Orient = Orient;
        }
    }

    // FUN_061118d0: bookmark 0, then the move (pub::SpaceObj::Relocate in the current system)
    // and the location read back.
    void MoveShip(Vector* Pos, Matrix* Orient)
    {
        SetBookmark(0);
        unsigned System = 0;
        ServerApi()->GetPlayerSystem(kPlayer, &System);
        ServerApi()->Relocate(GShipId, System, Pos, Orient);
        ServerApi()->GetLocation(GShipId, Pos, Orient);
    }

    // ----------------------------------------------------------------------------------------
    // Objects of a system (FUN_06112260, pub::System::EnumerateObjects with FUN_061122e0):
    // "~name" a case-insensitive substring of the nickname; else the nickname itself, then a
    // case-sensitive substring of the name; "*N" the match to stop at.
    // ----------------------------------------------------------------------------------------
    struct FObjectSearch
    {
        unsigned Result;
        wchar_t const* Text;
        unsigned Id;
        long Count;
        bool bNickname;
    };

    bool ObjectEnumerator(FHdConsoleSysObj const* Object, void* Arg)
    {
        FObjectSearch& S = *static_cast<FObjectSearch*>(Arg);
        bool bMatch;
        if (S.bNickname)
        {
            wchar_t Nickname[0x80];
            mbstowcs(Nickname, Object->Nickname, 0x80);
            Nickname[0x7f] = 0;
            bMatch = FindNoCase(Nickname, S.Text) != nullptr;
        }
        else
        {
            if (Object->Id == S.Id)
            {
                S.Result = Object->Id;
                return false;
            }
            bMatch = wcsstr(GameIds(static_cast<unsigned>(Object->IdsName), GScratch, 0x80), S.Text) != nullptr;
        }
        if (bMatch)
        {
            S.Result = Object->Id;
            if (--S.Count <= 0)
                return false;
        }
        return true;
    }

    unsigned FindObject(unsigned System, wchar_t* Text)
    {
        FObjectSearch S;
        S.Result = 0;
        S.bNickname = Text[0] == L'~';
        S.Text = S.bNickname ? Text + 1 : Text;
        S.Id = ArgId(Text);
        S.Count = MatchCount(Text);
        ServerApi()->EnumerateObjects(System, ObjectEnumerator, &S);
        if (S.Result == 0)
            OutIds(IdsObjectNotFound);
        return S.Result;
    }

    // ----------------------------------------------------------------------------------------
    // Bases and systems (FUN_06113000 / FUN_061134d0): the nickname first, then a
    // case-sensitive substring of the name; a base whose nickname starts with "intro" never
    // matches by name.
    // ----------------------------------------------------------------------------------------
    unsigned GBaseId = 0;   // DAT_0611d9b0: the "base" command's base (previous, via it)
    unsigned GSystemId = 0; // DAT_0611d998: the "system" command's system

    bool FindBase(wchar_t* Text)
    {
        GBaseId = 0;
        unsigned const Id = ArgId(Text);
        long Count = MatchCount(Text);
        for (Universe::IBase const* Base = Universe::GetFirstBase(); Base != nullptr;
             Base = Universe::GetNextBase())
        {
            if (Base->GetId() == Id)
            {
                GBaseId = Id;
                break;
            }
            GameIds(static_cast<unsigned>(Base->GetStridName()), GScratch, 0x1000);
            if (wcsstr(GScratch, Text) != nullptr && _strnicmp("intro", Base->GetNickname(), 5) != 0)
            {
                GBaseId = Base->GetId();
                if (--Count < 1)
                    break;
            }
        }
        if (GBaseId == 0)
            OutIds(IdsBaseNotFound);
        return GBaseId != 0;
    }

    bool FindSystem(wchar_t* Text)
    {
        GSystemId = 0;
        unsigned const Id = ArgId(Text);
        long Count = MatchCount(Text);
        for (Universe::ISystem const* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
        {
            if (System->GetId() == Id)
            {
                GSystemId = Id;
                break;
            }
            GameIds(static_cast<unsigned>(System->GetStridName()), GScratch, 0x1000);
            if (wcsstr(GScratch, Text) != nullptr)
            {
                GSystemId = System->GetId();
                if (--Count < 1)
                    break;
            }
        }
        if (GSystemId == 0)
            OutIds(IdsSystemNotFound);
        return GSystemId != 0;
    }

    // ----------------------------------------------------------------------------------------
    // Saves (FUN_06111570): a one-character slot is "qsave<SLOT>.fl", listed as "Quick Save
    // <SLOT>" (upper case; ':' becomes '?'); none is "qsave.fl", "Quick Save"; anything longer
    // is "<CreateID in hex>.fl", listed as the text with the separating space in front.
    // ----------------------------------------------------------------------------------------
    void SaveNames(char* File, wchar_t const* Text, wchar_t* Description)
    {
        if (Text[0] != 0 && Text[1] != 0)
        {
            sprintf(File, "%x.fl", ArgId(Text));
            wcscpy(Description, Text - 1); // the space before the argument
            return;
        }
        wchar_t Slot[2];
        Slot[0] = Text[0] == L':' ? L'?' : static_cast<wchar_t>(towupper(Text[0]));
        Slot[1] = 0;
        if (Slot[0] != 0)
            sprintf(File, "qsave%c.fl", static_cast<char>(Slot[0]));
        else
            strcpy(File, "qsave.fl");
        wchar_t Format[0x40];
        Ids(IdsQuickSave, Format, 0x40);
        wchar_t Suffix[3] = {L' ', Slot[0], 0};
        swprintf(Description, 0x100, Format, Slot[0] != 0 ? Suffix : Slot + 1);
    }

    // ----------------------------------------------------------------------------------------
    // The ship store (DAT_0611d714: a list of base, archetype, loadout, weapon groups), kept in
    // "<save>.fls" beside each save: the DLL's hooks of the save writer (0x06d4d275) and the
    // character delete (0x06d4a177) write and remove it, CLoginMgr::SelectCharacterById's
    // hook (0x005ab231) loads it.
    // ----------------------------------------------------------------------------------------
    struct FStoredShip
    {
        unsigned Base = 0;
        unsigned Arch = 0;
        EquipDescVector Equip;
        std::string WeaponGroups;
    };
    std::list<FStoredShip> GShips;
    unsigned GShiplessArch = 0; // DAT_0611d9a0

    // FUN_06115840: the file, or none when nothing is stored.
    void WriteShipStore(char const* SavePath)
    {
        char Path[0x108];
        sprintf(Path, "%ss", SavePath);
        if (GShips.empty())
        {
            DeleteFileA(Path);
            return;
        }
        FILE* const Out = fopen(Path, "w");
        if (Out == nullptr)
            return;
        for (FStoredShip const& Ship : GShips)
        {
            fprintf(Out, "[Player]\nbase = %u\nship_archetype = %u\n", Ship.Base, Ship.Arch);
            for (EquipDesc const* Item = Ship.Equip.Begin; Item != Ship.Equip.End; ++Item)
            {
                if (Item->bEquipped == 0)
                {
                    fprintf(Out, "cargo = %u, %d, , ", Item->ArchId, Item->Count);
                    if (Item->Health != 1.0f)
                        fprintf(Out, "%g", static_cast<double>(Item->Health));
                    fprintf(Out, ", %d\n", Item->bMission ? 1 : 0);
                }
                else
                    fprintf(Out, "equip = %u, %s, %g\n", Item->ArchId,
                            Item->is_internal() ? "" : Item->Hardpoint, static_cast<double>(Item->Health));
            }
            fputc('\n', Out);
            if (Ship.WeaponGroups[0] != '\n' && Ship.WeaponGroups[0] != 0)
                fprintf(Out, "%s", Ship.WeaponGroups.c_str());
        }
        fclose(Out);
    }

    // FUN_06115a80: [Player] blocks of base, ship_archetype (a number or a nickname), equip
    // and cargo lines (Loadout::ReadEquipLine / ReadCargoLine) and wg lines kept verbatim.
    void ReadShipStore(char const* Path)
    {
        GShips.clear();
        INI_Reader Reader;
        if (!Reader.open(Path, false))
            return;
        if (Reader.read_header())
        {
            do
            {
                FStoredShip Ship;
                EquipDesc Item;
                while (Reader.read_value())
                {
                    if (Reader.is_value("base"))
                    {
                        Ship.Base = static_cast<unsigned>(Reader.get_value_int(0));
                        if (Ship.Base == 0)
                            Ship.Base = CreateID(Reader.get_value_string(0));
                    }
                    else if (Reader.is_value("ship_archetype"))
                    {
                        Ship.Arch = static_cast<unsigned>(Reader.get_value_int(0));
                        if (Ship.Arch == 0)
                            Ship.Arch = CreateID(Reader.get_value_string(0));
                    }
                    else if (Reader.is_value("equip"))
                    {
                        if (Loadout::ReadEquipLine(Reader, Item))
                            Ship.Equip.add_equipment_item(Item, false);
                    }
                    else if (Reader.is_value("cargo"))
                    {
                        if (Loadout::ReadCargoLine(Reader, Item))
                            Ship.Equip.add_equipment_item(Item, false);
                    }
                    else if (Reader.is_value("wg"))
                    {
                        Ship.WeaponGroups += Reader.get_line_ptr();
                        Ship.WeaponGroups += '\n';
                    }
                }
                GShips.push_back(Ship);
            } while (Reader.read_header());
        }
        Reader.close();
    }

    void OnSaveWritten(char const* Path)
    {
        WriteShipStore(Path);
    }
    void OnSaveDeleted(char const* Path)
    {
        char Store[0x108];
        sprintf(Store, "%ss", Path);
        DeleteFileA(Store);
    }

    // FUN_06113f40: the player's current ship as a record (false in space or when shipless).
    bool CaptureShip(FStoredShip* Out)
    {
        if (InSpace())
            return false;
        FHdConsoleServerApi const* const Api = ServerApi();
        Api->GetBase(kPlayer, &Out->Base);
        Api->GetShipArch(kPlayer, &Out->Arch);
        if (Out->Arch == GShiplessArch)
            return false;
        Out->Equip = EquipDescVector(GetPlayerEquipDescListRetail());
        unsigned const Length = Api->GetWeaponGroups(kPlayer, nullptr, 0);
        Out->WeaponGroups.assign(Length, '\0');
        if (Length != 0)
            Api->GetWeaponGroups(kPlayer, &Out->WeaponGroups[0], Length + 1);
        return true;
    }

    // FUN_06113ee0: "Repair damage." unless hull and everything on it are at full health.
    bool Repaired()
    {
        if (ServerApi()->IsRepaired(kPlayer))
            return true;
        OutIds(IdsRepairDamage);
        return false;
    }

    // FUN_06114150: become the stored ship -- SetShipAndLoadout, the weapon groups on the
    // server and the client (SET_WEAPON_GROUP, LAB_0053c310).
    void DriveShip(FStoredShip const& Ship)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        Api->SetShipAndLoadout(kPlayer, Ship.Arch, &Ship.Equip);
        Api->SetWeaponGroups(kPlayer, Ship.WeaponGroups.c_str());
        G_WeaponGroups.ReadFromServer(Ship.WeaponGroups.c_str(), static_cast<int>(Ship.WeaponGroups.size()));
    }

    wchar_t const* ShipName(unsigned Arch, wchar_t* Buffer, int Chars)
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(Arch);
        Buffer[0] = 0;
        if (Ship != nullptr)
            GameIds(static_cast<unsigned>(Ship->IdsName), Buffer, Chars);
        return Buffer;
    }

    // FUN_06113c90: the list of stored ships -- all of them with their bases, or this base's.
    void ListShips(bool bAll)
    {
        unsigned Base = 0;
        ServerApi()->GetBase(kPlayer, &Base);
        OutReset();
        OutStyle(0xe784);
        OutTra(4, 4);
        OutIds(IdsShips);
        OutTra(0, 4);
        OutStyle(0x3ea8);
        bool bAny = false;
        for (FStoredShip const& Ship : GShips)
        {
            if (!bAll && Ship.Base != Base)
                continue;
            bAny = true;
            OutPara();
            wchar_t Name[0x80];
            OutText(ShipName(Ship.Arch, Name, 0x80));
            if (bAll)
            {
                OutText(L" (");
                Universe::IBase const* const Info = Universe::get_base(Ship.Base);
                if (Info != nullptr)
                    OutGameIds(static_cast<unsigned>(Info->GetStridName()));
                OutText(L")");
            }
        }
        if (!bAny)
        {
            OutPara();
            OutGameIds(IdsNone);
        }
    }

    // FUN_06113de0: the stored ship Text names (a prefix of the name, "*N") at this base; with
    // no text the only one here, else the list. end() when none.
    std::list<FStoredShip>::iterator FindStoredShip(wchar_t* Text)
    {
        ServerApi()->GetBase(kPlayer, &GBaseId);
        long Count = MatchCount(Text);
        size_t const Length = wcslen(Text);
        std::list<FStoredShip>::iterator Found = GShips.end();
        for (std::list<FStoredShip>::iterator It = GShips.begin(); It != GShips.end(); ++It)
        {
            if (It->Base != GBaseId)
                continue;
            if (Text[0] == 0)
            {
                if (Found != GShips.end())
                {
                    ListShips(false);
                    return GShips.end();
                }
                Found = It;
            }
            else
            {
                wchar_t Name[0x80];
                if (_wcsnicmp(ShipName(It->Arch, Name, 0x80), Text, Length) == 0)
                {
                    Found = It;
                    if (--Count < 1)
                        break;
                }
            }
        }
        if (Found == GShips.end())
            OutIds(Text[0] == 0 ? IdsNoShipsHere : IdsShipNotFound);
        return Found;
    }

    // FUN_061144e0: after park / drive -- the NavBar rebuilt (FUN_00442060) with
    // GetPlayerShipArchetypeId's answer forced (DAT_0611da1c: 0 parked, 1 driving; the
    // client's own copy of the archetype only changes when the server's update arrives), so
    // the launch icon follows the ship.
    int GForcedShipArch = -1;
    void RefreshBaseForShip(bool bParked)
    {
        GForcedShipArch = bParked ? 0 : 1;
        NavBarEnsure();
        GForcedShipArch = -1;
    }

    // ----------------------------------------------------------------------------------------
    // Reputation (FUN_06116230 / FUN_06116470 / the enumerators at 0x0611b38c..0x0611b3a0).
    // ----------------------------------------------------------------------------------------
    std::vector<float> GSavedReps; // DAT_0611d9c4: the reputations before the first "rep ="

    struct FGroupSearch
    {
        unsigned Result;
        wchar_t const* Text;
        size_t Length;
        long Count;
    };

    // 0x061163d0: a group whose short name starts with the text, skipping the groups whose
    // info card is 66205 (the generic one) other than group 25320.
    bool GroupEnumerator(unsigned Group, void* Arg)
    {
        FGroupSearch& S = *static_cast<FGroupSearch*>(Arg);
        unsigned ShortName = 0;
        ServerApi()->GetShortGroupName(Group, &ShortName);
        wchar_t Name[0x40];
        GameIds(ShortName, Name, 0x40);
        if (_wcsnicmp(Name, S.Text, S.Length) != 0)
            return true;
        if (Reputation::get_info_card(Group) == 0x1029d && Group != 0x62e8)
            return true;
        S.Result = Group;
        return --S.Count > 0;
    }

    // FUN_06116230: the first word is the faction -- a single letter b / k / l / r is the
    // House police (br_p_grp, ku_p_grp, li_p_grp, rh_p_grp); a nickname; else a prefix of a
    // short name. The cursor moves past it; the short name is left in the scratch. -1 and
    // "Faction not found." when none.
    unsigned FindGroup(wchar_t const** Cursor)
    {
        wchar_t const* const Start = *Cursor;
        char Name[0x80];
        int Length = 0;
        wchar_t const* P = Start;
        while (*P != 0 && *P != L' ' && Length < 0x7f)
            Name[Length++] = static_cast<char>(*P++);
        Name[Length] = 0;
        while (*P == L' ')
            ++P;
        *Cursor = P;
        if (Name[1] == 0)
        {
            switch (Name[0] | 0x20)
            {
            case 'b': strcpy(Name, "br_p_grp"); break;
            case 'k': strcpy(Name, "ku_p_grp"); break;
            case 'l': strcpy(Name, "li_p_grp"); break;
            case 'r': strcpy(Name, "rh_p_grp"); break;
            default: break;
            }
        }
        unsigned Group = 0xffffffffu;
        ServerApi()->GetGroup(Name, &Group);
        if (Group == 0xffffffffu)
        {
            FGroupSearch S;
            S.Result = 0;
            S.Text = Start;
            S.Length = static_cast<size_t>(Length);
            wchar_t Copy[0x80];
            wcsncpy(Copy, Start, static_cast<size_t>(Length));
            Copy[Length] = 0;
            S.Count = MatchCount(Copy);
            S.Length = wcslen(Copy);
            S.Text = Copy;
            ServerApi()->EnumerateGroups(GroupEnumerator, &S);
            if (S.Result == 0)
            {
                OutIds(IdsFactionNotFound);
                return 0xffffffffu;
            }
            Group = S.Result;
        }
        unsigned ShortName = 0;
        ServerApi()->GetShortGroupName(Group, &ShortName);
        GameIds(ShortName, GScratch, 0x1000);
        return Group;
    }

    // FUN_06116470: H -0.9, h -0.6, nh -0.2, n 0, nf 0.2, f 0.6, F 0.9; else a number, taken
    // as a percentage beyond +-1 and clamped to +-1. False unless the whole text is used.
    bool ParseRep(wchar_t const* Text, float* Out)
    {
        switch (Text[0])
        {
        case L'F': *Out = 0.9f; return Text[1] == 0;
        case L'H': *Out = -0.9f; return Text[1] == 0;
        case L'f': *Out = 0.6f; return Text[1] == 0;
        case L'h': *Out = -0.6f; return Text[1] == 0;
        case L'n':
            if (Text[1] == 0)
            {
                *Out = 0.0f;
                return true;
            }
            if (Text[1] == L'f')
            {
                *Out = 0.2f;
                return Text[2] == 0;
            }
            if (Text[1] == L'h')
            {
                *Out = -0.2f;
                return Text[2] == 0;
            }
            ++Text;
            break;
        default:
            break;
        }
        wchar_t* End = nullptr;
        float Value = static_cast<float>(wcstod(Text, &End));
        *Out = Value;
        if (Value >= -1.0f)
        {
            if (Value > 1.0f)
            {
                Value = (Value + 4e-06f) * 0.01f;
                *Out = Value > 1.0f ? 1.0f : Value;
            }
        }
        else
        {
            Value = (Value - 4e-06f) * 0.01f;
            *Out = Value < -1.0f ? -1.0f : Value;
        }
        return *End == 0;
    }

    struct FRepApply
    {
        int Vibe;
        float Value;
        unsigned Source;
        size_t Index;
    };
    bool RepSaveEnumerator(unsigned Group, void* Arg) // 0x06116a30
    {
        FRepApply& A = *static_cast<FRepApply*>(Arg);
        float Value = 0.0f;
        ServerApi()->GetRep(A.Vibe, Group, &Value);
        GSavedReps.push_back(Value);
        return true;
    }
    bool RepRestoreEnumerator(unsigned Group, void* Arg) // 0x06116a00
    {
        FRepApply& A = *static_cast<FRepApply*>(Arg);
        if (A.Index < GSavedReps.size())
            ServerApi()->SetRep(A.Vibe, Group, GSavedReps[A.Index]);
        ++A.Index;
        return true;
    }
    bool RepSetEnumerator(unsigned Group, void* Arg) // 0x06116c20
    {
        FRepApply& A = *static_cast<FRepApply*>(Arg);
        ServerApi()->SetRep(A.Vibe, Group, A.Value);
        return true;
    }
    bool RepCopyEnumerator(unsigned Group, void* Arg) // 0x06116c40
    {
        FRepApply& A = *static_cast<FRepApply*>(Arg);
        float Value = 0.0f;
        ServerApi()->GetRep(A.Vibe, A.Source, &Value);
        ServerApi()->SetRep(A.Vibe, Group, Value);
        return true;
    }

    // ----------------------------------------------------------------------------------------
    // State the patched sites read.
    // ----------------------------------------------------------------------------------------
    bool GbShow = false;          // 0x004c4df1 patched
    bool GbHouseLabels = true;    // 0x0049e799
    bool GbPathsUnfiltered = false; // 0x0049cd34
    float GZoomSeconds = -1.0f;   // 0x004944ac / 0x0049f2a7
    bool GbBracketsOff = false;   // 0x004ec087
    bool GbBracketsSight = false; // DAT_005d954c = 1.0

    // Cruise constants (FUN_06112b80 / FUN_06112bf0 / FUN_06117cc0): the console's values
    // (DAT_0611d9f0 / DAT_0611d984) and the originals (DAT_0611d9d4 / DAT_0611d954).
    float GCruiseSpeed = 0.0f, GCruiseAccel = 0.0f;
    float GCruiseSpeedOriginal = 0.0f, GCruiseAccelOriginal = 0.0f;
    bool GbCruiseSaved = false;

    void SaveCruiseConstants()
    {
        if (GbCruiseSaved)
            return;
        GbCruiseSaved = true;
        GCruiseSpeed = GCruiseSpeedOriginal = EngineEquipConsts::CRUISING_SPEED;
        GCruiseAccel = GCruiseAccelOriginal = EngineEquipConsts::CRUISE_ACCEL_TIME;
    }

    // The "system" command's switch (the Common.dll hook at 0x062b2a81 that arms the client's
    // pending jump when the ship launches, and the thread-free equivalent of its animation
    // thread LAB_06112c50).
    struct FLaunchSwitch
    {
        bool bPending = false;
        bool bDefaultPos = false; // DAT_0611d9e4: the launch position, not a given one
        unsigned System = 0;
        unsigned Object = 0;
        Vector Pos;
        Matrix Orient;
    } GLaunchSwitch;

    // The animation (DAT_0611d070 handle, DAT_0611d9e8 state 1 backwards / 2 done forwards /
    // 3 forwards, DAT_0611d9e0 the explicit direction, DAT_0611d9a4 the ship it plays on).
    struct FAnimation
    {
        unsigned long Handle = static_cast<unsigned long>(-1);
        int State = 0;
        int Direction = 0;
        CShip* Ship = nullptr;
    } GAnim;

    // ----------------------------------------------------------------------------------------
    // Commands. Each returns true to show the output.
    // ----------------------------------------------------------------------------------------
    typedef bool (*FCommand)(wchar_t* Args);

    // FUN_06111300
    bool CmdAbout(wchar_t*)
    {
        OutReset();
        OutStyle(0xe784);
        OutText(L"Console");
        OutStyle(0x3ea8);
        OutText(L" by ");
        OutStyle(0x66b7);
        OutText(L"Jason Hood");
        OutStyle(0x3ea8);
        OutText(L" (adoxa).");
        OutPara();
        OutIds(IdsVersion);
        return true;
    }

    // FUN_061113b0: a popup (pub::Player::PopUpDialog, buttons 8) of the group's title and
    // RDL page; the topic is matched as a prefix against the group names from "player" back
    // to "game".
    bool CmdHelp(wchar_t* Args)
    {
        static char const* const kGroups[] = {"game", "base", "move", "ship", "hud", "player"};
        int Group = -1;
        if (Args[0] != 0)
        {
            size_t const Length = wcslen(Args);
            for (Group = 5; Group >= 0; --Group)
            {
                wchar_t Name[8];
                mbstowcs(Name, kGroups[Group], 8);
                if (_wcsnicmp(Name, Args, Length) == 0)
                    break;
            }
        }
        if (!GbResources)
        {
            // Port: the pages are console.dll's resources; without the file, name the group.
            OutIds(static_cast<unsigned>(IdsGroupTitle + Group + 1));
            OutText(L": help pages need console.dll in bin/.");
            return true;
        }
        FmtStr Title;
        Title.begin_mad_lib(GIdsBase + static_cast<unsigned>(Group + 1));
        Title.end_mad_lib();
        FmtStr Text;
        Text.begin_mad_lib(GIdsBase + IdsHelpPage + static_cast<unsigned>(Group + 1));
        Text.end_mad_lib();
        ServerApi()->PopUpDialog(0, &Title, &Text, 8);
        return false;
    }

    // FUN_06111530
    bool CmdAutosave(wchar_t*)
    {
        ServerApi()->Save(kPlayer, 1);
        OutIds(IdsGameSaved);
        return true;
    }

    // FUN_06111560: FUN_004bc830.
    bool CmdAutoload(wchar_t*)
    {
        NnStoryStarLoadAutosave();
        return false;
    }

    // FUN_06111660
    bool CmdSave(wchar_t* Args)
    {
        char File[0x200];
        wchar_t Description[0x100];
        SaveNames(File, Args, Description);
        int const Result = ServerApi()->SaveNamed(kPlayer, File, Description);
        OutIds(IdsGameNotSaved + static_cast<unsigned>(Result & 0xff));
        return true;
    }

    // FUN_061116c0: "Game not found." unless the file exists in Accts\SinglePlayer, else
    // FUN_005b2c70(file, 1).
    bool CmdLoad(wchar_t* Args)
    {
        char File[0x200];
        wchar_t Description[0x100];
        SaveNames(File, Args, Description);
        char Path[0x200];
        GetUserDataPath(Path);
        strcat(Path, "\\Accts\\SinglePlayer\\");
        strcat(Path, File);
        if (GetFileAttributesA(Path) == INVALID_FILE_ATTRIBUTES)
        {
            OutIds(IdsGameNotFound);
            return true;
        }
        CHARACTER_ID CharId;
        strcpy(CharId.Buffer, File);
        QueueLoadGameCharacter(&CharId, true);
        return false;
    }

    // FUN_06116da0: a sound by nickname (FUN_0042ae40 / FUN_004285f0).
    bool CmdPlay(wchar_t* Args)
    {
        ID_String Id;
        Id.Id = ArgId(Args);
        void* const Arch = LookupClientSoundArch(&Id);
        if (Arch == nullptr)
        {
            OutIds(IdsSoundNotFound);
            return true;
        }
        PlayClientSoundArchIfIdle(Arch, nullptr, nullptr);
        return false;
    }

    // FUN_06113b40: the NavBar's launch (FUN_00440f94), in a base.
    bool CmdLaunch(wchar_t*)
    {
        unsigned Base = 0;
        ServerApi()->GetBase(kPlayer, &Base);
        if (Base != 0)
            NavBarLaunch();
        return false;
    }

    // FUN_06114530
    bool CmdPark(wchar_t*)
    {
        FStoredShip Ship;
        if (!CaptureShip(&Ship))
            return false;
        if (!Repaired())
            return true;
        NnMainButtonOnModalOpen(); // FUN_0045a460 (NN_MainButton::ClosePanel)
        GShips.push_back(Ship);
        EquipDescVector const Empty;
        ServerApi()->SetShipAndLoadout(kPlayer, GShiplessArch, &Empty);
        RefreshBaseForShip(true);
        wchar_t Name[0x80];
        OutIdsFormat(IdsParked, ShipName(Ship.Arch, Name, 0x80));
        return true;
    }

    // FUN_06114780
    bool CmdDrive(wchar_t* Args)
    {
        if (InSpace() || GShips.empty())
            return false;
        std::list<FStoredShip>::iterator const Found = FindStoredShip(Args);
        if (Found == GShips.end())
            return true;
        FStoredShip Current;
        if (CaptureShip(&Current))
        {
            if (!Repaired())
                return true;
            GShips.push_back(Current);
            wchar_t Name[0x80];
            OutIdsFormat(IdsParked, ShipName(Current.Arch, Name, 0x80));
            OutPara();
        }
        else
            RefreshBaseForShip(false);
        DriveShip(*Found);
        wchar_t Name[0x80];
        OutText(ShipName(Found->Arch, Name, 0x80));
        GShips.erase(Found);
        NnMainButtonOnModalOpen(); // FUN_0045a460 (NN_MainButton::ClosePanel)
        return true;
    }

    // FUN_061154e0: the stored ship is driven for its asset value, sold, and the previous
    // ship driven again.
    bool CmdSell(wchar_t* Args)
    {
        if (InSpace() || GShips.empty())
            return false;
        if (Args[0] == 0)
        {
            ListShips(false);
            return true;
        }
        std::list<FStoredShip>::iterator const Found = FindStoredShip(Args);
        if (Found == GShips.end())
            return true;
        if (!Repaired())
            return true;
        FStoredShip Current;
        CaptureShip(&Current);
        DriveShip(*Found);
        float Value = 0.0f;
        ServerApi()->GetAssetValue(kPlayer, &Value);
        int const Cash = static_cast<int>(Value);
        ServerApi()->AdjustCash(kPlayer, Cash);
        if (Current.Arch == GShiplessArch)
        {
            EquipDescVector const Empty;
            ServerApi()->SetShipAndLoadout(kPlayer, GShiplessArch, &Empty);
        }
        else
            DriveShip(Current);
        wchar_t Name[0x40], Amount[0x80];
        ShipName(Found->Arch, Name, 0x40);
        FormatGroupedNumber(Amount, Cash, -1);
        OutIdsFormat(IdsSold, Name, Amount);
        GShips.erase(Found);
        return true;
    }

    bool CmdBase(wchar_t* Args);
    void ChangeBaseInPlace();

    // FUN_06115770: park, then "base" to the one other base with ships stored.
    bool CmdHitch(wchar_t* Args)
    {
        if (InSpace())
            return false;
        unsigned Arch = 0;
        ServerApi()->GetShipArch(kPlayer, &Arch);
        if (Arch != GShiplessArch)
            CmdPark(Args);
        if (Args[0] != 0)
            return CmdBase(Args);
        unsigned Base = 0;
        ServerApi()->GetBase(kPlayer, &Base);
        unsigned Other = 0;
        GBaseId = 0;
        for (FStoredShip const& Ship : GShips)
        {
            if (Ship.Base == Base)
                continue;
            if (Other == 0)
                GBaseId = Other = Ship.Base;
            else if (Ship.Base != Other)
            {
                Other = 0;
                break;
            }
        }
        if (Other != 0)
        {
            ChangeBaseInPlace();
            return false;
        }
        OutIds(IdsSpecifyBase);
        return true;
    }

    // FUN_06113dc0
    bool CmdShips(wchar_t*)
    {
        if (GShips.empty())
            return false;
        ListShips(true);
        return true;
    }

    // FUN_06115160: the checks, then the dealer window rewired into an item transfer (its
    // patches of 0x00437fb5..0x00483ad0 and the result hook 0x06114d20) -- not reproduced
    // (docs/re/deviations.md HD-CONSOLE-XFER).
    bool CmdXfer(wchar_t* Args)
    {
        if (InSpace() || GShips.empty())
            return false;
        unsigned Arch = 0;
        ServerApi()->GetShipArch(kPlayer, &Arch);
        if (Arch == GShiplessArch)
        {
            if (Args[0] == 0)
            {
                ListShips(false);
                return true;
            }
            OutIds(IdsDriveAShip);
            return true;
        }
        if (!Repaired())
            return true;
        if (FindStoredShip(Args) == GShips.end())
            return true;
        OutText(L"xfer: the item transfer window is not available in this port.");
        return true;
    }

    // FUN_06112fc0: the base changed under the player -- LocationExit / BaseExit
    // (FUN_0043b3e0), BaseEnter (FUN_0043b290), the base id (FUN_004c4910) and the arrival
    // caption (FUN_0045b2d0, 2 seconds).
    void ChangeBaseInPlace()
    {
        LeaveClientLocationBase();     // FUN_0043b3e0
        EnterClientLocationBase(GBaseId); // FUN_0043b290
        SetPlayerBaseIdRetail(GBaseId); // FUN_004c4910
        NnArrivalTextShowBase(2.0f);   // FUN_0045b2d0(0x40000000)
    }

    // FUN_06113000
    bool CmdBase(wchar_t* Args)
    {
        unsigned Base = 0;
        ServerApi()->GetBase(kPlayer, &Base);
        bool const bDocked = Base != 0;
        if (bDocked && Args[0] == 0)
        {
            Universe::IBase const* const Info = Universe::get_base(Base);
            OutAscii(Info != nullptr ? Info->GetNickname() : "");
            return true;
        }
        if (Args[0] == 0)
        {
            if (GBaseId == 0)
                return false;
            Universe::IBase const* const Info = Universe::get_base(GBaseId);
            if (Info != nullptr)
                OutGameIds(static_cast<unsigned>(Info->GetStridName()));
            return true;
        }
        if (Args[0] == L'.' && Args[1] == 0)
        {
            if (bDocked)
                GBaseId = Base;
            else if (GBaseId == 0)
                return false;
        }
        else if (!FindBase(Args))
            return true;
        if (bDocked)
        {
            ChangeBaseInPlace();
            return false;
        }
        SetBookmark(0);
        ServerApi()->ForceLand(kPlayer, GBaseId);
        return false;
    }

    bool CmdSystem(wchar_t* Args);

    // FUN_06111920: "pos" -- display; "copy"; "N" / "clip" (the system command's); "=" the
    // bookmark list; "=N" set one; "x, y, z[, p, y, r]" move.
    bool CmdPos(wchar_t* Args)
    {
        Vector Pos;
        Matrix Orient;
        bool const bInSpace = ShipLocation(&Pos, &Orient);
        bool bRotated = false;
        if (Args[0] == 0)
        {
            if (!bInSpace)
                return false;
        }
        else if (wcscmp(Args, L"copy") == 0)
        {
            if (!bInSpace)
                return false;
            unsigned System = 0;
            ServerApi()->GetPlayerSystem(kPlayer, &System);
            Universe::ISystem const* const Info = Universe::get_system(System);
            float Euler[3];
            MatrixToEuler(Orient, Euler);
            char Text[0x200];
            int const Length = sprintf(Text, "%s %g, %g, %g, %g, %g, %g", Info != nullptr ? Info->GetNickname() : "",
                                       static_cast<double>(Pos.x), static_cast<double>(Pos.y),
                                       static_cast<double>(Pos.z), static_cast<double>(Euler[0]),
                                       static_cast<double>(Euler[1]), static_cast<double>(Euler[2]));
            bool bCopied = false;
            if (OpenClipboard(nullptr))
            {
                EmptyClipboard();
                HGLOBAL const Memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(Length) + 1);
                if (Memory != nullptr)
                {
                    void* const Block = GlobalLock(Memory);
                    if (Block != nullptr)
                    {
                        memcpy(Block, Text, static_cast<size_t>(Length) + 1);
                        GlobalUnlock(Memory);
                        SetClipboardData(CF_TEXT, Memory);
                        bCopied = true;
                    }
                }
                CloseClipboard();
            }
            OutIds(bCopied ? IdsPositionCopied : IdsClipboardUnavailable);
            return true;
        }
        else if ((iswdigit(Args[0]) && Args[1] == 0) || wcscmp(Args, L"clip") == 0)
            return CmdSystem(Args);
        else if (Args[0] == L'=' && Args[1] == 0)
        {
            OutReset();
            OutStyle(0xe784);
            OutTra(4, 4);
            OutIds(IdsBookmarks);
            OutTra(0, 4);
            OutStyle(0x3ea8);
            bool bAny = false;
            for (int Index = 0; Index < 10; ++Index)
            {
                FBookmark const& Mark = GBookmarks[Index];
                if (Mark.System == 0)
                    continue;
                bAny = true;
                OutPara();
                OutFormat(L"%d = ", Index);
                Universe::ISystem const* const Info = Universe::get_system(Mark.System);
                if (Info != nullptr)
                    OutGameIds(static_cast<unsigned>(Info->GetStridName()));
                OutFormat(L", %g, %g, %g", static_cast<double>(Mark.Pos.x), static_cast<double>(Mark.Pos.y),
                          static_cast<double>(Mark.Pos.z));
            }
            if (!bAny)
            {
                OutPara();
                OutGameIds(IdsNone);
            }
            return true;
        }
        else
        {
            if (!bInSpace)
                return false;
            if (Args[0] == L'=' && iswdigit(Args[1]) && Args[2] == 0)
            {
                SetBookmark(Args[1] - L'0');
                OutIds(IdsBookmarkSet);
                return true;
            }
            wchar_t const* Cursor = Args;
            float Values[3] = {Pos.x, Pos.y, Pos.z};
            ParseTriple(Values, &Cursor);
            Pos = MakeVector(Values[0], Values[1], Values[2]);
            if (*Cursor != 0)
            {
                float Euler[3];
                MatrixToEuler(Orient, Euler);
                ParseTriple(Euler, &Cursor);
                Orient = EulerToMatrix(Euler);
                bRotated = true;
            }
            MoveShip(&Pos, &Orient);
        }
        OutFormat(L"pos = %g, %g, %g", static_cast<double>(Pos.x), static_cast<double>(Pos.y),
                  static_cast<double>(Pos.z));
        if (bRotated)
        {
            float Euler[3];
            MatrixToEuler(Orient, Euler);
            OutPara();
            OutFormat(L"rotate = %g, %g, %g", static_cast<double>(Euler[0]), static_cast<double>(Euler[1]),
                      static_cast<double>(Euler[2]));
        }
        return true;
    }

    // FUN_06111d90: "posr" -- relative.
    bool CmdPosr(wchar_t* Args)
    {
        Vector Pos;
        Matrix Orient;
        if (!ShipLocation(&Pos, &Orient))
            return false;
        bool bRotated = false;
        if (Args[0] != 0)
        {
            wchar_t const* Cursor = Args;
            float Delta[3] = {0.0f, 0.0f, 0.0f};
            ParseTriple(Delta, &Cursor);
            Pos.x += Delta[0];
            Pos.y += Delta[1];
            Pos.z += Delta[2];
            if (*Cursor != 0)
            {
                float Euler[3];
                MatrixToEuler(Orient, Euler);
                float Turn[3] = {0.0f, 0.0f, 0.0f};
                ParseTriple(Turn, &Cursor);
                Euler[0] += Turn[0];
                Euler[1] += Turn[1];
                Euler[2] += Turn[2];
                Orient = EulerToMatrix(Euler);
                bRotated = true;
            }
            MoveShip(&Pos, &Orient);
        }
        OutFormat(L"pos = %g, %g, %g", static_cast<double>(Pos.x), static_cast<double>(Pos.y),
                  static_cast<double>(Pos.z));
        if (bRotated)
        {
            float Euler[3];
            MatrixToEuler(Orient, Euler);
            OutPara();
            OutFormat(L"rotate = %g, %g, %g", static_cast<double>(Euler[0]), static_cast<double>(Euler[1]),
                      static_cast<double>(Euler[2]));
        }
        return true;
    }

    // FUN_06111f50 / FUN_06112020: "rot" / "rotr".
    bool CmdRotation(wchar_t* Args, bool bRelative)
    {
        Vector Pos;
        Matrix Orient;
        if (!ShipLocation(&Pos, &Orient))
            return false;
        if (Args[0] != 0)
        {
            wchar_t const* Cursor = Args;
            float Euler[3];
            MatrixToEuler(Orient, Euler);
            if (bRelative)
            {
                float Turn[3] = {0.0f, 0.0f, 0.0f};
                ParseTriple(Turn, &Cursor);
                Euler[0] += Turn[0];
                Euler[1] += Turn[1];
                Euler[2] += Turn[2];
            }
            else
                ParseTriple(Euler, &Cursor);
            Orient = EulerToMatrix(Euler);
            MoveShip(&Pos, &Orient);
        }
        float Euler[3];
        MatrixToEuler(Orient, Euler);
        OutFormat(L"rotate = %g, %g, %g", static_cast<double>(Euler[0]), static_cast<double>(Euler[1]),
                  static_cast<double>(Euler[2]));
        return true;
    }
    bool CmdRot(wchar_t* Args)
    {
        return CmdRotation(Args, false);
    }
    bool CmdRotr(wchar_t* Args)
    {
        return CmdRotation(Args, true);
    }

    // FUN_061123d0: "jump z, x, y" in the ship's frame, or to an object: the target, a
    // nickname, a name. The spot depends on the object's type.
    bool CmdJump(wchar_t* Args)
    {
        Vector Pos;
        Matrix M;
        if (!ShipLocation(&Pos, &M))
            return false;
        FHdConsoleServerApi const* const Api = ServerApi();
        if (iswdigit(Args[0]) || Args[0] == L'-' || Args[0] == L',')
        {
            // forward z, right x, up y: pos + x * column 0 + y * column 1 - z * column 2
            wchar_t const* Cursor = Args;
            float D[3] = {0.0f, 0.0f, 0.0f};
            ParseTriple(D, &Cursor);
            Pos.x += M.E[0][0] * D[1] + M.E[0][1] * D[2] - M.E[0][2] * D[0];
            Pos.y += M.E[1][0] * D[1] + M.E[1][1] * D[2] - M.E[1][2] * D[0];
            Pos.z += M.E[2][0] * D[1] + M.E[2][1] * D[2] - M.E[2][2] * D[0];
            MoveShip(&Pos, &M);
            return false;
        }
        unsigned Object = 0;
        if (Args[0] == 0)
        {
            Api->GetTarget(GShipId, &Object);
            if (Object == 0)
                return false;
        }
        else
        {
            Api->GetPlayerSystem(kPlayer, &GSystemId);
            Object = ArgId(Args);
            unsigned ObjectSystem = 0;
            if (Api->GetSystem(Object, &ObjectSystem) < 0)
            {
                Object = FindObject(GSystemId, Args);
                if (Object == 0)
                    return true;
            }
        }
        unsigned Type = 0;
        Vector ObjPos;
        Matrix ObjM;
        float Radius;
        if (Api->GetType(Object, &Type) < 0)
        {
            // Not a server object: the client's target position, a radius of 0.3121905.
            CShip* const Ship = PlayerCShip();
            IObjRW* const Target = Ship != nullptr ? Ship->get_target() : nullptr;
            if (Target == nullptr)
                return false;
            CObject* const TargetObject = static_cast<FClientGameobj*>(Target)->GetObject();
            if (TargetObject == nullptr)
                return false;
            ObjPos = TargetObject->get_position();
            ObjM = M;
            Radius = 0.3121905f;
            Type = 0;
        }
        else
        {
            Vector Center;
            float Burn = 0.0f;
            Api->GetLocation(Object, &ObjPos, &ObjM);
            Api->GetRadius(Object, &Radius, &Center);
            Api->GetBurnRadius(Object, &Burn, &Center);
            if (Radius < Burn)
                Radius = Burn;
        }
        bool bFixture = false;
        if (Type == 0x100)
        {
            unsigned Arch = 0;
            Api->GetSolarArchetypeID(Object, &Arch);
            Archetype::Solar const* const Solar = Archetype::GetSolar(Arch);
            bFixture = Solar != nullptr && Solar->Nickname.Id == CreateID("docking_fixture");
        }
        Vector New = Pos;
        if (bFixture || Type == 0x20)
        {
            // Behind the object (its +z) by its radius, as it faces; a docking ring also 98 m
            // along its x axis.
            New.x = ObjPos.x + ObjM.E[0][2] * Radius;
            New.y = ObjPos.y + ObjM.E[1][2] * Radius;
            New.z = ObjPos.z + ObjM.E[2][2] * Radius;
            M = ObjM;
            if (!bFixture)
            {
                New.x += ObjM.E[0][0] * 98.0f;
                New.y += ObjM.E[1][0] * 98.0f;
                New.z += ObjM.E[2][0] * 98.0f;
            }
        }
        else if (Type == 0x40)
        {
            // In front of a jump gate (its -z) by 1.5 radii, as it faces.
            float const D = Radius * 1.5f;
            New.x = ObjPos.x - ObjM.E[0][2] * D;
            New.y = ObjPos.y - ObjM.E[1][2] * D;
            New.z = ObjPos.z - ObjM.E[2][2] * D;
            M = ObjM;
        }
        else if (Type == 0x80)
        {
            // A trade lane ring: its near side, facing along the lane.
            Vector Behind;
            Behind.x = ObjPos.x + ObjM.E[0][2] * Radius;
            Behind.y = ObjPos.y + ObjM.E[1][2] * Radius;
            Behind.z = ObjPos.z + ObjM.E[2][2] * Radius;
            M = ObjM;
            float const ToBehind = (Pos.x - Behind.x) * (Pos.x - Behind.x) + (Pos.y - Behind.y) * (Pos.y - Behind.y) +
                                   (Pos.z - Behind.z) * (Pos.z - Behind.z);
            float const ToObject = (Pos.x - ObjPos.x) * (Pos.x - ObjPos.x) + (Pos.y - ObjPos.y) * (Pos.y - ObjPos.y) +
                                   (Pos.z - ObjPos.z) * (Pos.z - ObjPos.z);
            if (ToBehind <= ToObject)
                New = Behind;
            else
            {
                for (int Column = 0; Column < 3; ++Column)
                {
                    M.E[0][Column] = -M.E[0][Column];
                    M.E[2][Column] = -M.E[2][Column];
                }
                New.x = ObjPos.x + M.E[0][2] * Radius;
                New.y = ObjPos.y + M.E[1][2] * Radius;
                New.z = ObjPos.z + M.E[2][2] * Radius;
            }
        }
        else
        {
            // Anything else: on the line from the object to the ship at its radius (twice it
            // for a moon, planet or sun, unless already between R+2 and 2R+2 away), facing the
            // object; a station's spot is tested for room (pub::SpaceObj::IsPosEmpty) and, when
            // there is none, the ship goes through to the far side and turns round.
            float const Dist = sqrtf((Pos.x - ObjPos.x) * (Pos.x - ObjPos.x) + (Pos.y - ObjPos.y) * (Pos.y - ObjPos.y) +
                                     (Pos.z - ObjPos.z) * (Pos.z - ObjPos.z));
            if ((Type & 7) != 0 && (Dist < Radius + 2.0f || Dist > Radius + 1.0f + Radius + 1.0f))
                Radius += Radius;
            New.x = ObjPos.x - (ObjPos.x - Pos.x) * Radius / Dist;
            New.y = ObjPos.y - (ObjPos.y - Pos.y) * Radius / Dist;
            New.z = ObjPos.z - (ObjPos.z - Pos.z) * Radius / Dist;
            M = LookMatrixYup(MakeVector(ObjPos.x - New.x, ObjPos.y - New.y, ObjPos.z - New.z));
            if (Type == 0x100)
            {
                Api->GetPlayerSystem(kPlayer, &GSystemId);
                Vector Center;
                float ShipRadius = 0.0f;
                Api->GetRadius(GShipId, &ShipRadius, &Center);
                float const Half = ShipRadius * 0.5f;
                Vector Ahead;
                Ahead.x = New.x + Half * M.E[0][2];
                Ahead.y = New.y + Half * M.E[1][2];
                Ahead.z = New.z + Half * M.E[2][2];
                bool bEmpty = true;
                Api->IsPosEmpty(GSystemId, &Ahead, Half - 1.0f, &bEmpty);
                if (!bEmpty)
                {
                    float const Through = Radius + Radius;
                    New.x -= Through * M.E[0][2];
                    New.y -= Through * M.E[1][2];
                    New.z -= Through * M.E[2][2];
                    for (int Column = 0; Column < 3; ++Column)
                    {
                        M.E[0][Column] = -M.E[0][Column];
                        M.E[2][Column] = -M.E[2][Column];
                    }
                }
            }
        }
        MoveShip(&New, &M);
        return false;
    }

    // FUN_061134d0: "system" -- display; "N" a bookmark; "clip"; "<system> [object | x, y, z
    // [, p, y, r]]". In the current system a bookmark or clipboard position is a move; any
    // other system is a switch: in space the client's pending jump, in a base a launch that
    // turns into one.
    bool CmdSystem(wchar_t* Args)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Args[0] == 0)
        {
            Api->GetPlayerSystem(kPlayer, &GSystemId);
            Universe::ISystem const* const Info = Universe::get_system(GSystemId);
            OutAscii(Info != nullptr ? Info->GetNickname() : "");
            return true;
        }
        Vector Pos;
        Matrix Orient;
        bool const bInSpace = ShipLocation(&Pos, &Orient);
        bool bDefault = false; // DAT_0611d9e4
        unsigned Object = 0;   // DAT_0611d960
        bool bBookmark = false, bClipboard = false;
        wchar_t ClipText[0x1000];
        if (iswdigit(Args[0]) && Args[1] == 0)
        {
            FBookmark const& Mark = GBookmarks[Args[0] - L'0'];
            if (Mark.System == 0)
            {
                OutIds(IdsBookmarkNotSet);
                return true;
            }
            GSystemId = Mark.System;
            Pos = Mark.Pos;
            Orient = Mark.Orient;
            bBookmark = true;
        }
        else
        {
            wchar_t const* Cursor = Args;
            if (wcscmp(Args, L"clip") == 0)
            {
                bool bRead = false;
                if (IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(nullptr))
                {
                    HANDLE const Memory = GetClipboardData(CF_TEXT);
                    char const* const Text = Memory != nullptr ? static_cast<char const*>(GlobalLock(Memory)) : nullptr;
                    if (Text != nullptr)
                    {
                        mbstowcs(ClipText, Text, 0x1000);
                        ClipText[0xfff] = 0;
                        GlobalUnlock(Memory);
                        bRead = true;
                        bClipboard = true;
                    }
                    CloseClipboard();
                }
                if (!bRead)
                {
                    OutIds(IdsClipboardUnavailable);
                    return true;
                }
                Cursor = ClipText;
            }
            // The system: the first word (63 characters at most).
            wchar_t Word[0x40];
            int Length = 0;
            while (*Cursor != 0 && *Cursor != L' ' && Length < 0x3f)
                Word[Length++] = *Cursor++;
            Word[Length] = 0;
            if (Word[0] == L'.' && Length == 1)
                Api->GetPlayerSystem(kPlayer, &GSystemId);
            else if (!FindSystem(Word))
                return true;
            while (*Cursor == L' ')
                ++Cursor;
            if (iswdigit(*Cursor) || *Cursor == L'-' || *Cursor == L',')
            {
                float Values[3] = {Pos.x, Pos.y, Pos.z};
                ParseTriple(Values, &Cursor);
                Pos = MakeVector(Values[0], Values[1], Values[2]);
                if (*Cursor == 0)
                {
                    if (!bInSpace)
                    {
                        float const Zero[3] = {0.0f, 0.0f, 0.0f};
                        Orient = EulerToMatrix(Zero);
                    }
                }
                else
                {
                    float Euler[3];
                    MatrixToEuler(Orient, Euler);
                    ParseTriple(Euler, &Cursor);
                    Orient = EulerToMatrix(Euler);
                }
            }
            else if (*Cursor == 0)
                bDefault = true;
            else
            {
                wchar_t ObjectText[0x100];
                wcsncpy(ObjectText, Cursor, 0x100);
                ObjectText[0xff] = 0;
                Object = FindObject(GSystemId, ObjectText);
                if (Object == 0)
                    return true;
            }
        }
        unsigned Current = 0;
        Api->GetPlayerSystem(kPlayer, &Current);
        if (GSystemId == Current && bInSpace && (bBookmark || bClipboard))
        {
            MoveShip(&Pos, &Orient);
            return false;
        }
        SetBookmark(0);
        Api->SetSystemSwitch(kPlayer, GSystemId, Object, &Pos, &Orient);
        if (bInSpace)
        {
            // DAT_0067977c: the client switches as after a jump; the server's patched
            // SystemSwitchOutComplete places the ship.
            SetPendingJumpSystem(&GSystemId, false);
            return false;
        }
        // From a base: the launch (FUN_06113b40), with the Common.dll hook at 0x062b2a81
        // arming the client's pending jump as the ship launches; with no position given
        // (DAT_0611d9e4) the ship keeps its launch position, taken from the dock point rather
        // than the dock mount (the launch_pos hook at 0x062aa9df).
        GLaunchSwitch.bPending = true;
        GLaunchSwitch.bDefaultPos = bDefault;
        GLaunchSwitch.System = GSystemId;
        GLaunchSwitch.Object = Object;
        GLaunchSwitch.Pos = Pos;
        GLaunchSwitch.Orient = Orient;
        HdConsoleLaunchFromDockPoint(bDefault);
        CmdLaunch(Args);
        return false;
    }

    // FUN_06112b80 / FUN_06112bf0: EngineEquipConsts::CRUISING_SPEED (and
    // PhySys::ANOM_LIMITS_MAX_VELOCITY raised to 1.5x it) / CRUISE_ACCEL_TIME.
    bool CmdCspd(wchar_t* Args)
    {
        SaveCruiseConstants();
        if (Args[0] != 0)
        {
            EngineEquipConsts::CRUISING_SPEED = static_cast<float>(wcstod(Args, nullptr));
            GCruiseSpeed = EngineEquipConsts::CRUISING_SPEED;
            if (PhySys::ANOM_LIMITS_MAX_VELOCITY < EngineEquipConsts::CRUISING_SPEED * 1.5f)
                PhySys::ANOM_LIMITS_MAX_VELOCITY = EngineEquipConsts::CRUISING_SPEED * 1.5f;
        }
        OutIdsFormat(IdsCruiseSpeed, static_cast<double>(EngineEquipConsts::CRUISING_SPEED));
        return true;
    }
    bool CmdCacc(wchar_t* Args)
    {
        SaveCruiseConstants();
        if (Args[0] != 0)
        {
            EngineEquipConsts::CRUISE_ACCEL_TIME = static_cast<float>(wcstod(Args, nullptr));
            GCruiseAccel = EngineEquipConsts::CRUISE_ACCEL_TIME;
        }
        OutIdsFormat(IdsCruiseAccel, static_cast<double>(EngineEquipConsts::CRUISE_ACCEL_TIME));
        return true;
    }

    // FUN_06112d40: "anim" -- the bay doors, or a named animation of the ship ("wings" is
    // Sc_extend wing; ">" / "<" an explicit direction) through DALib::Anim: PlayScript on the
    // archetype's script, EvalPlayback, AnimDB::Add; a second command reverses a running one.
    void* OpaqueFloat(float Value)
    {
        unsigned Bits;
        memcpy(&Bits, &Value, sizeof(Bits));
        return reinterpret_cast<void*>(static_cast<uintptr_t>(Bits));
    }

    void AnimationFinish()
    {
        AnimDB::Rem(static_cast<int>(GAnim.Handle));
        DALib::Anim->StopPlayback(GAnim.Handle);
        DALib::Anim->DestroyScript(reinterpret_cast<void*>(static_cast<uintptr_t>(GAnim.Handle)));
        GAnim.Handle = static_cast<unsigned long>(-1);
    }

    bool CmdAnim(wchar_t* Args)
    {
        CShip* const Ship = PlayerCShip();
        if (Ship == nullptr)
            return false;
        if (Args[0] == 0 || ((Args[0] == L'<' || Args[0] == L'>') && Args[1] == 0))
        {
            if (Ship->get_bay_state() > BayClosing)
                Ship->close_bay();
            else
            {
                // 0x062b2ba1 (CShip::open_bay's shield loop test) is a JMP for this call: the
                // doors open without the shields dropping (common_cship.cpp).
                HdConsoleOpenBayKeepsShields(true);
                Ship->open_bay();
                HdConsoleOpenBayKeepsShields(false);
            }
            return false;
        }
        if (GAnim.Handle != static_cast<unsigned long>(-1))
        {
            bool const bForwards = GAnim.State != 1;
            DALib::Anim->EvalPlayback(GAnim.Handle, bForwards ? 1 : 0, -2.0f, 1.0f, nullptr,
                                      OpaqueFloat(1.0f), OpaqueFloat(1.0f), nullptr);
            AnimDB::Add(static_cast<int>(GAnim.Handle));
            GAnim.State = bForwards ? 1 : 3;
            return false;
        }
        GAnim.Direction = 0;
        if (Args[0] == L'>')
        {
            GAnim.Direction = 1;
            ++Args;
        }
        else if (Args[0] == L'<')
        {
            GAnim.Direction = -1;
            ++Args;
        }
        char Name[0x80];
        if (_wcsnicmp(L"wings", Args, wcslen(Args)) == 0)
            strcpy(Name, "Sc_extend wing");
        else
        {
            wcstombs(Name, Args, 0x80);
            Name[0x7f] = 0;
        }
        Archetype::Root const* const Arch = Ship->get_archetype();
        GAnim.Handle = DALib::Anim->PlayScript(reinterpret_cast<void*>(static_cast<intptr_t>(Arch->AnimIndex)),
                                               reinterpret_cast<void*>(static_cast<intptr_t>(Ship->EngineInstanceId)),
                                               Name, nullptr, nullptr);
        if (GAnim.Handle == static_cast<unsigned long>(-1))
        {
            OutIds(IdsAnimationNotFound);
            return true;
        }
        bool const bBackwards = GAnim.Direction < 0;
        DALib::Anim->EvalPlayback(GAnim.Handle, bBackwards ? 1 : 0, bBackwards ? -1.0f : -2.0f, 1.0f, nullptr,
                                  OpaqueFloat(1.0f), OpaqueFloat(1.0f), nullptr);
        AnimDB::Add(static_cast<int>(GAnim.Handle));
        GAnim.State = bBackwards ? 1 : 3;
        GAnim.Ship = Ship;
        return false;
    }

    // LAB_06112c50, the DLL's thread (every 100 ms): a forward animation past its duration
    // ends (explicit) or waits to be reversed (state 2); a backward one at 0 ends; the thread
    // leaves when the ship changes.
    void AnimationTick()
    {
        if (GAnim.Handle == static_cast<unsigned long>(-1))
            return;
        if (PlayerCShip() != GAnim.Ship)
        {
            GAnim.Handle = static_cast<unsigned long>(-1); // port: the DLL keeps the stale handle
            return;
        }
        if (GAnim.State == 3)
        {
            float const Stored = DALib::Anim->GetStoredDuration(GAnim.Handle);
            float const Script = DALib::Anim->GetScriptDuration(reinterpret_cast<void*>(static_cast<uintptr_t>(GAnim.Handle)));
            if (Stored >= Script)
            {
                if (GAnim.Direction == 1)
                    AnimationFinish();
                else
                    GAnim.State = 2;
            }
        }
        else if (GAnim.State == 1)
        {
            if (DALib::Anim->GetStoredDuration(GAnim.Handle) <= 0.0f)
                AnimationFinish();
        }
    }

    // FUN_06113ac0: DAT_00679cc4, the DISABLE_COLLIDE game option (0x37).
    bool CmdGhost(wchar_t*)
    {
        int const Ghost = GetGameOption(kGameOptDisableCollide) ^ 1;
        SetGameOption(kGameOptDisableCollide, Ghost);
        OutIds(Ghost == 0 ? IdsGhostOff : IdsGhostOn);
        return true;
    }

    // FUN_06113b00: DAT_0067ecc0, the "+G" command-line flag CLoginMgr::Login applies.
    bool CmdGodmode(wchar_t*)
    {
        G_bCommandLineG = !G_bCommandLineG;
        OutIds(G_bCommandLineG ? IdsGodmodeOn : IdsGodmodeOff);
        return true;
    }

    // FUN_06115ed0: "ntb [on|off|sight]".
    bool CmdNtb(wchar_t* Args)
    {
        if (Args[0] != 0)
        {
            size_t const Length = wcslen(Args);
            if (_wcsnicmp(L"off", Args, Length) == 0 && Length > 1)
                GbBracketsOff = true;
            else if (_wcsicmp(L"on", Args) == 0)
            {
                GbBracketsOff = false;
                GbBracketsSight = false;
            }
            else if (_wcsnicmp(L"sight", Args, Length) == 0)
            {
                GbBracketsOff = false;
                GbBracketsSight = true;
            }
            else
                return false;
        }
        if (GbBracketsOff)
            OutIds(IdsNtbOff);
        else
            OutIds(GbBracketsSight ? IdsNtbSight : IdsNtbOn);
        return true;
    }

    // FUN_06115fc0: "show" (the visit flags), "show houses", "show paths".
    bool CmdShow(wchar_t* Args)
    {
        size_t const Length = wcslen(Args);
        if (Length == 0)
        {
            GbShow = !GbShow;
            OutIds(GbShow ? IdsShowOn : IdsShowOff);
            return true;
        }
        if (_wcsnicmp(L"houses", Args, Length) == 0)
        {
            GbHouseLabels = !GbHouseLabels;
            OutIds(GbHouseLabels ? IdsHousesOn : IdsHousesOff);
            return true;
        }
        if (_wcsnicmp(L"paths", Args, Length) != 0)
            return false;
        GbPathsUnfiltered = !GbPathsUnfiltered;
        OutIds(GbPathsUnfiltered ? IdsPathsUnfiltered : IdsPathsFiltered);
        return true;
    }

    // FUN_06116110: "zoom [time]" (0 becomes 0.001).
    bool CmdZoom(wchar_t* Args)
    {
        if (Args[0] != 0)
        {
            GZoomSeconds = static_cast<float>(wcstod(Args, nullptr));
            if (GZoomSeconds == 0.0f)
                GZoomSeconds = 0.001f;
        }
        OutIdsFormat(IdsZoomTime, static_cast<double>(GZoomSeconds >= 0.0f ? GZoomSeconds : 1.5f));
        return true;
    }

    // FUN_06116170: "cash [[+|-]amount]" (k suffix), the amount grouped (FUN_004779a0).
    bool CmdCash(wchar_t* Args)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        int Cash = 0;
        if (Args[0] != 0)
        {
            bool const bRelative = Args[0] == L'+' || Args[0] == L'-';
            wchar_t* End = nullptr;
            long Amount = wcstol(Args, &End, 10);
            if (*End == L'k' || *End == L'K')
                Amount *= 1000;
            if (!bRelative)
            {
                Api->InspectCash(kPlayer, &Cash);
                Amount -= Cash;
            }
            Api->AdjustCash(kPlayer, static_cast<int>(Amount));
        }
        Api->InspectCash(kPlayer, &Cash);
        FormatGroupedNumber(GScratch, Cash, -1);
        OutText(GScratch);
        return true;
    }

    // FUN_06116680: "rep faction [value]", "rep = [faction | value]", "rep undo".
    bool CmdRep(wchar_t* Args)
    {
        if (Args[0] == 0)
            return false;
        FHdConsoleServerApi const* const Api = ServerApi();
        FRepApply A;
        A.Vibe = 0;
        A.Value = 0.0f;
        A.Source = 0;
        A.Index = 0;
        Api->GetPlayerVibe(kPlayer, &A.Vibe);
        if (_wcsicmp(Args, L"undo") == 0)
        {
            if (GSavedReps.empty())
                return false;
            Api->EnumerateGroups(RepRestoreEnumerator, &A);
            GSavedReps.clear();
            OutIds(IdsRepLoaded);
            return true;
        }
        unsigned long Colour = 0;
        if (Args[0] == L'=' && (Args[1] == L' ' || Args[1] == 0))
        {
            bool bSaved = false;
            OutReset();
            if (GSavedReps.empty())
            {
                Api->EnumerateGroups(RepSaveEnumerator, &A);
                OutStyle(0x3ea8);
                OutIds(IdsRepSaved);
                bSaved = true;
            }
            wchar_t const* Cursor = Args + 1;
            while (*Cursor == L' ')
                ++Cursor;
            if (*Cursor == 0)
                return bSaved;
            if (ParseRep(Cursor, &A.Value))
            {
                Api->EnumerateGroups(RepSetEnumerator, &A);
                if (bSaved)
                    OutPara();
                NnReputationColorArgb(&Colour, A.Value);
                OutTra((static_cast<unsigned>(Colour) << 8) | 1, 0xffffff01);
                OutIdsFormat(IdsReputation, static_cast<double>(A.Value));
                return true;
            }
            if (bSaved)
                OutPara();
            A.Source = FindGroup(&Cursor);
            if (A.Source == 0xffffffffu)
                return true;
            Api->EnumerateGroups(RepCopyEnumerator, &A);
            Colour = G_NnPaletteArgb_679bac;
        }
        else
        {
            wchar_t const* Cursor = Args;
            unsigned const Group = FindGroup(&Cursor);
            if (Group == 0xffffffffu)
                return true;
            float Value = 0.0f;
            if (*Cursor != 0 && ParseRep(Cursor, &Value))
                Api->SetRep(A.Vibe, Group, Value);
            Api->GetRep(A.Vibe, Group, &Value);
            size_t const Length = wcslen(GScratch);
            swprintf(GScratch + Length, 0x1000 - Length, L": %g", static_cast<double>(Value));
            OutReset();
            NnReputationColorArgb(&Colour, Value);
        }
        OutTra((static_cast<unsigned>(Colour) << 8) | 1, 0xffffff01);
        OutText(GScratch);
        return true;
    }

    // FUN_06116c80 / FUN_06116cb0 / FUN_06116ce0
    bool CmdMonkey(wchar_t*)
    {
        ServerApi()->SetCostume(kPlayer, 1);
        OutIds(IdsMonkey);
        return true;
    }
    bool CmdRobot(wchar_t*)
    {
        ServerApi()->SetCostume(kPlayer, 2);
        OutIds(IdsRobot);
        return true;
    }
    bool CmdTrent(wchar_t*)
    {
        ServerApi()->SetCostume(kPlayer, 0);
        OutIds(IdsTrent);
        return true;
    }

    // FUN_06116d10: "costume character" -- the base costume (Players+0x2e4), named back with
    // its first letter in upper case.
    bool CmdCostume(wchar_t* Args)
    {
        unsigned const Id = ArgId(Args);
        if (GetCostumeDescriptions()->find_costume(Id) == nullptr)
        {
            OutIds(IdsCostumeNotFound);
            return true;
        }
        ServerApi()->SetBaseCostume(kPlayer, Id);
        OutFormat(L"%c%s.", static_cast<wchar_t>(towupper(Args[0])), Args + 1);
        return true;
    }

    // The table at 0x0611d0c0, in its order (sorted; "al", "as", "l" and "s" are the aliases
    // that make the prefixes "a", "l" and "s" unambiguous).
    struct FCommandEntry
    {
        wchar_t const* Name;
        FCommand Run;
    };
    FCommandEntry const kCommands[] = {
        {L"about", CmdAbout},   {L"anim", CmdAnim},       {L"al", CmdAutoload}, {L"as", CmdAutosave},
        {L"autoload", CmdAutoload}, {L"autosave", CmdAutosave}, {L"base", CmdBase}, {L"cacc", CmdCacc},
        {L"cash", CmdCash},     {L"costume", CmdCostume}, {L"cspd", CmdCspd},   {L"drive", CmdDrive},
        {L"ghost", CmdGhost},   {L"godmode", CmdGodmode}, {L"help", CmdHelp},   {L"hitch", CmdHitch},
        {L"jump", CmdJump},     {L"l", CmdLaunch},        {L"launch", CmdLaunch}, {L"load", CmdLoad},
        {L"monkey", CmdMonkey}, {L"ntb", CmdNtb},         {L"park", CmdPark},   {L"play", CmdPlay},
        {L"pos", CmdPos},       {L"posr", CmdPosr},       {L"rep", CmdRep},     {L"robot", CmdRobot},
        {L"rot", CmdRot},       {L"rotr", CmdRotr},       {L"s", CmdSystem},    {L"save", CmdSave},
        {L"sell", CmdSell},     {L"ships", CmdShips},     {L"show", CmdShow},   {L"system", CmdSystem},
        {L"trent", CmdTrent},   {L"xfer", CmdXfer},       {L"zoom", CmdZoom},
    };
}

// ----------------------------------------------------------------------------------------------
// The hooks.
// ----------------------------------------------------------------------------------------------

void HdConsoleRegisterResources()
{
    if (!HdConsoleEnabled())
        return;
    HMODULE const Module = LoadLibraryExA("console.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (Module == nullptr)
    {
        fprintf(stderr, "[hd console] console.dll not in bin/: messages from the built-in copy, no help pages\n");
        return;
    }
    GIdsBase = RegisterIdsResourceModule(Module); // FUN_0057d800: (count of modules) << 16
    GbResources = true;
    ServerApi(); // early, so the save hooks are in place before the first save
}

void HdConsoleLoadShiplessShip(Archetype::ICliObjFactory* Factory)
{
    if (!HdConsoleEnabled())
        return;
    GShiplessArch = CreateID("shipless_ship");
    char Path[0x100];
    if (GetTempPathA(0xf6, Path) == 0)
        return;
    strcat(Path, "shipless.ini");
    FILE* const Out = fopen(Path, "wb");
    if (Out == nullptr)
        return;
    static char const kShipless[] =
        "[Ship]\nnickname = shipless_ship\nDA_archetype = ships\\utility\\csv\\csv_shield.3db\n";
    fwrite(kShipless, 1, sizeof(kShipless) - 1, Out);
    fclose(Out);
    Archetype::LoadShips(Path, false, Factory);
    DeleteFileA(Path);
    SaveCruiseConstants();
}

bool HdConsoleIsShiplessArch(unsigned Arch)
{
    return GShiplessArch != 0 && Arch == GShiplessArch;
}

int HdConsoleForcedShipArch()
{
    return GForcedShipArch;
}

void HdConsoleOnSetMultiPlayer(bool bMultiPlayer)
{
    if (!HdConsoleEnabled())
        return;
    SaveCruiseConstants();
    if (!bMultiPlayer)
    {
        EngineEquipConsts::CRUISING_SPEED = GCruiseSpeed;
        EngineEquipConsts::CRUISE_ACCEL_TIME = GCruiseAccel;
        G_ChatInputTarget = CHAT_ID(0); // DAT_0066d3fc: the Console chat channel
        return;
    }
    EngineEquipConsts::CRUISING_SPEED = GCruiseSpeedOriginal;
    EngineEquipConsts::CRUISE_ACCEL_TIME = GCruiseAccelOriginal;
}

void HdConsoleLoadShipStore(char const* CharacterFile)
{
    if (!HdConsoleEnabled())
        return;
    char Path[0x300];
    GetUserDataPath(Path);
    sprintf(Path + strlen(Path), "\\Accts\\SinglePlayer\\%ss", CharacterFile);
    ReadShipStore(Path);
    FHdConsoleServerApi const* const Api = ServerApi();
    if (Api != nullptr)
        Api->SetSaveFileHooks(OnSaveWritten, OnSaveDeleted);
}

void HdConsoleSpaceTick()
{
    if (!HdConsoleEnabled())
        return;
    AnimationTick();
    // Port: the DLL arms the switch inside CShip::launch; the port waits for the launch action
    // to finish first (a ship switching systems with its LaunchAction alive keeps the base's
    // CSolar past its system's physics, which faults at the next teardown -- deviations.md
    // HD-CONSOLE-SWITCH).
    CShip* const Launched = GLaunchSwitch.bPending ? PlayerCShip() : nullptr;
    if (Launched != nullptr && Launched->Actions.get_action_count() == 0 && InSpace())
    {
        GLaunchSwitch.bPending = false;
        HdConsoleLaunchFromDockPoint(false);
        if (GLaunchSwitch.bDefaultPos)
        {
            // FUN_06113270: the launched ship's own position and orientation become the
            // switch's.
            Vector Pos;
            Matrix Orient;
            ServerApi()->GetLocation(GShipId, &Pos, &Orient);
            ServerApi()->SetSystemSwitch(kPlayer, GLaunchSwitch.System, 0, &Pos, &Orient);
        }
        SetPendingJumpSystem(&GLaunchSwitch.System, false);
    }
}

unsigned char HdConsoleVisitBits()
{
    return GbShow ? 1 : 0;
}

bool HdConsoleHouseLabels()
{
    return GbHouseLabels;
}

bool HdConsolePatrolPathsUnfiltered()
{
    return GbPathsUnfiltered;
}

float HdConsoleNavMapZoomSeconds(float Default)
{
    return GZoomSeconds >= 0.0f ? GZoomSeconds : Default;
}

bool HdConsoleBracketsOff()
{
    return GbBracketsOff;
}

float HdConsoleBracketFade(float Default)
{
    return GbBracketsSight ? 1.0f : Default;
}

// FUN_06116df0: single player only; the first word (to a space) in lower case is looked up
// in the table -- an entry of the same name, else the one entry it is a prefix of; the handler
// gets the text after the spaces that follow. Nothing matched, two prefix matches or a handler
// with nothing to say: the line is dropped.
void HdConsoleExecute(wchar_t const* Line)
{
    if (!SinglePlayer() || ServerApi() == nullptr)
        return;
    fprintf(stderr, "[hd console] > %ls\n", Line);
    wchar_t Text[0x400];
    wcsncpy(Text, Line, 0x400);
    Text[0x3ff] = 0;
    size_t Length = 0;
    while (Text[Length] != 0 && Text[Length] != L' ')
        ++Length;
    wchar_t const First = static_cast<wchar_t>(towlower(Text[0]));
    int Match = -1;
    size_t const Count = sizeof(kCommands) / sizeof(kCommands[0]);
    for (size_t Index = 0; Index < Count; ++Index)
    {
        FCommandEntry const& Entry = kCommands[Index];
        if (Entry.Name[0] < First)
            continue;
        if (Entry.Name[0] != First)
            break;
        size_t const NameLength = wcslen(Entry.Name);
        if (Length > NameLength || _wcsnicmp(Entry.Name, Text, Length) != 0)
            continue;
        if (Length == NameLength)
        {
            Match = static_cast<int>(Index);
            break;
        }
        if (Match != -1)
            return; // ambiguous
        Match = static_cast<int>(Index);
    }
    if (Match == -1)
        return;
    wchar_t* Args = Text + Length;
    while (*Args == L' ')
        ++Args;
    OutReset();
    OutStyle(0x3ea8);
    if (kCommands[Match].Run(Args))
        OutShow();
}
