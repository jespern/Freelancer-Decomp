// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll's single-player commands (hd_console.h). The plugin's help text (its groups
// GAME, BASE, MOVE, SHIP, HUD, PLAYER) and readme are the reference for each command's
// behaviour; the "ship" command and the "jump <system>[,<base>]" form are the port's additions.

#include "hd_console.h"

#include "character_select_equip.h" // GetPlayerBaseIdRetail, GetPlayerEquipDescListRetail
#include "chat_init.h"
#include "client_state.h" // QueueLoadGameCharacter
#include "ids_rdl.h"
#include "ids_strings.h" // GetIdsStringW
#include "login_mgr.h"   // G_LoginMgr (the save list), G_Server
#include "menu_universe_base.h" // ChangeClientLocation
#include "nn_inventory.h"       // ReqPlayer*Retail (the ship dealer's requests)
#include "player_spawn.h"       // GetPlayerConnectionId (DAT_00673344)

#include "common_archetype.h" // Archetype::GetShip / GetSolar
#include "common_goods.h"     // GoodList, BaseDataList, EquipDesc
#include "common_misc.h"      // SinglePlayer, CreateID
#include "common_server_types.h" // CHARACTER_ID
#include "common_text.h"
#include "common_universe.h"
#include "hd_console_api.h"
#include "server_characterset.h" // CCharacterSet::CharacterInfo

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    // Server.dll's pub:: calls (the client does not link Server), resolved again whenever the
    // module changes: loading a game reconnects, and the single-player server is reloaded.
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

    void Print(char const* Format, ...)
    {
        char Narrow[512];
        va_list Args;
        va_start(Args, Format);
        vsnprintf(Narrow, sizeof(Narrow), Format, Args);
        va_end(Args);
        fprintf(stderr, "[hd console] %s\n", Narrow); // the port's log, for scripted runs
        wchar_t Wide[512];
        int Length = 0;
        for (; Narrow[Length] != '\0' && Length < 511; ++Length)
            Wide[Length] = static_cast<wchar_t>(static_cast<unsigned char>(Narrow[Length]));
        Wide[Length] = 0;
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        TextNode Text(Wide, -1);
        RdlAppendNode(Entry->Body, Text.Clone());
        ChatCommitEntries();
    }

    unsigned Player()
    {
        return GetPlayerConnectionId();
    }

    // The player's ship in space, or 0 (docked / not in a game).
    unsigned PlayerShipQuiet()
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || Api->GetShip(Player(), &Ship) != 0)
            return 0;
        return Ship;
    }

    bool PlayerShip(unsigned* OutShip)
    {
        *OutShip = PlayerShipQuiet();
        if (*OutShip == 0)
        {
            Print("Not in space.");
            return false;
        }
        return true;
    }

    // A name resource as ASCII (the names the console matches are plain text).
    std::string IdsText(int Ids)
    {
        wchar_t Wide[256];
        Wide[0] = 0;
        if (Ids > 0)
            GetIdsStringW(static_cast<unsigned>(Ids), Wide, 256);
        std::string Text;
        for (wchar_t const* P = Wide; *P != 0; ++P)
            Text += static_cast<char>(*P < 0x80 ? *P : '?');
        return Text;
    }

    // HD: "Numbers may be suffixed with k or K to multiply by 1000."
    bool ParseNumber(std::string const& Text, float* Out)
    {
        char* End = nullptr;
        double Value = strtod(Text.c_str(), &End);
        if (End == Text.c_str())
            return false;
        if (*End == 'k' || *End == 'K')
        {
            Value *= 1000.0;
            ++End;
        }
        if (*End != 0)
            return false;
        *Out = static_cast<float>(Value);
        return true;
    }

    // "x, y, z" with any number optional ("Individual numbers are optional, defaulting to the
    // current value or 0"). Fields left empty keep *Out's value.
    bool ParseTriple(std::string const& Text, float* Out)
    {
        size_t Pos = 0;
        for (int Index = 0; Index < 3; ++Index)
        {
            size_t const Comma = Text.find(',', Pos);
            std::string Field = Text.substr(Pos, Comma == std::string::npos ? std::string::npos : Comma - Pos);
            size_t const From = Field.find_first_not_of(" \t");
            size_t const To = Field.find_last_not_of(" \t");
            Field = From == std::string::npos ? std::string() : Field.substr(From, To - From + 1);
            if (!Field.empty() && !ParseNumber(Field, &Out[Index]))
                return false;
            if (Comma == std::string::npos)
                return Index > 0 || !Field.empty();
            Pos = Comma + 1;
        }
        return true;
    }

    // HD: "The 'jump' and 'system' commands will treat an initial digit, minus sign or comma as
    // a position; anything else is taken as an object."
    bool LooksLikePosition(std::string const& Text)
    {
        return !Text.empty() && (isdigit(static_cast<unsigned char>(Text[0])) || Text[0] == '-' || Text[0] == ',');
    }

    bool IsSubstring(std::string const& Name, std::string const& Needle)
    {
        return !Needle.empty() && Name.find(Needle) != std::string::npos;
    }

    // --- The universe -----------------------------------------------------------------------

    // HD: "The object, base or system is tested as a nickname first, then as a case sensitive
    // substring of the name (prefix object with ~ to test nickname)."
    unsigned ResolveSystem(std::string Name, unsigned Current)
    {
        if (Name == ".")
            return Current;
        bool const bNicknameOnly = !Name.empty() && Name[0] == '~';
        if (bNicknameOnly)
            Name.erase(0, 1);
        unsigned const Id = Universe::get_system_id(Name.c_str());
        if ((Id & 0x3fffffff) != 0 && Universe::get_system(Id) != nullptr)
            return Id;
        if (bNicknameOnly)
            return 0;
        for (Universe::ISystem const* System = Universe::GetFirstSystem(); System != nullptr;
             System = Universe::GetNextSystem())
            if (IsSubstring(IdsText(System->GetStridName()), Name))
                return System->NicknameId;
        return 0;
    }

    Universe::IBase const* ResolveBase(std::string Name)
    {
        bool const bNicknameOnly = !Name.empty() && Name[0] == '~';
        if (bNicknameOnly)
            Name.erase(0, 1);
        unsigned const Id = Universe::get_base_id(Name.c_str());
        if ((Id & 0x3fffffff) != 0)
        {
            Universe::IBase const* const Base = Universe::get_base(Id);
            if (Base != nullptr)
                return Base;
        }
        if (bNicknameOnly)
            return nullptr;
        for (Universe::IBase const* Base = Universe::GetFirstBase(); Base != nullptr;
             Base = Universe::GetNextBase())
            if (IsSubstring(IdsText(Base->StridName), Name))
                return Base;
        return nullptr;
    }

    void ClearSystemObject(Universe::FSystemObject* Object)
    {
        Object->Nickname.Length = 0;
        Object->Nickname.Data[0] = 0;
        Object->Reputation.Length = 0;
        Object->Reputation.Data[0] = 0;
        Object->NextRing.Length = 0;
        Object->NextRing.Data[0] = 0;
        Object->PrevRing.Length = 0;
        Object->PrevRing.Data[0] = 0;
    }

    // An [Object] of a system by nickname, then by a substring of its name.
    bool ResolveObject(unsigned SystemId, std::string Name, Universe::FSystemObject* Out)
    {
        Universe::ISystem const* const System = Universe::get_system(SystemId);
        if (System == nullptr || Name.empty())
            return false;
        bool const bNicknameOnly = Name[0] == '~';
        if (bNicknameOnly)
            Name.erase(0, 1);
        int const Count = System->GetObjectCount();
        for (int Pass = 0; Pass < (bNicknameOnly ? 1 : 2); ++Pass)
            for (int Index = 0; Index < Count; ++Index)
            {
                ClearSystemObject(Out);
                if (!System->GetSystemObject(static_cast<unsigned>(Index), Out))
                    continue;
                if (Pass == 0 ? _stricmp(Out->Nickname.Data, Name.c_str()) == 0
                              : IsSubstring(IdsText(Out->IdsName), Name))
                    return true;
            }
        return false;
    }

    // A place beside a solar: its radius (the live object's when it is in the player's system,
    // else the archetype's solar_radius) plus a margin, along +z.
    Vector BesideObject(Universe::FSystemObject const& Object, bool bLive)
    {
        float Radius = 0.0f;
        Vector Center = Object.Pos;
        FHdConsoleServerApi const* const Api = ServerApi();
        if (!bLive || Api == nullptr ||
            Api->GetRadius(CreateID(Object.Nickname.Data), &Radius, &Center) != 0 || Radius <= 0.0f)
        {
            Archetype::Solar const* const Arch = Archetype::GetSolar(Object.ArchetypeId);
            Radius = Arch != nullptr && Arch->SolarRadius > 0.0f ? Arch->SolarRadius : 500.0f;
            Center = Object.Pos;
        }
        Vector Pos = Center;
        Pos.z += Radius * 1.5f + 500.0f;
        return Pos;
    }

    // Where "system X" puts the ship when no object or position is given: beside the system's
    // first base (an object with a base), else at the origin.
    Vector DefaultSystemPosition(unsigned SystemId)
    {
        Universe::FSystemObject Object;
        Universe::ISystem const* const System = Universe::get_system(SystemId);
        int const Count = System != nullptr ? System->GetObjectCount() : 0;
        for (int Index = 0; Index < Count; ++Index)
        {
            ClearSystemObject(&Object);
            if (System->GetSystemObject(static_cast<unsigned>(Index), &Object) && Object.BaseId != 0)
                return BesideObject(Object, false);
        }
        Vector Origin;
        Origin.x = Origin.y = Origin.z = 0.0f;
        return Origin;
    }

    CCharacterSet::CharacterInfo const* FindSave(char const* FileName);

    // Moves the player's ship, within its system (a beam; the console's readme: no autosave, no
    // test of the destination) or into another: the single-player server has only the current
    // system loaded, so the port writes a save placed in the new system and loads it.
    bool MoveShip(unsigned Ship, unsigned SystemId, Vector const& Pos)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        Vector Current;
        Matrix Orient;
        unsigned CurrentSystem = 0;
        if (Api == nullptr || Api->GetLocation(Ship, &Current, &Orient) != 0 ||
            Api->GetSystem(Ship, &CurrentSystem) != 0)
            return false;
        if (SystemId == CurrentSystem)
            return Api->Relocate(Ship, SystemId, &Pos, &Orient) == 0;
        if (Api->JumpSave(Player(), "qsavejump", L"Quick Save (jump)", SystemId, &Pos, &Orient) != 0)
            return false;
        CCharacterSet::CharacterInfo const* const Info = FindSave("qsavejump.fl");
        if (Info == nullptr)
            return false;
        QueueLoadGameCharacter(&Info->Id, false);
        return true;
    }

    // --- Saves ------------------------------------------------------------------------------

    // HD: "Slot is a single character and will be listed as 'Quick Save slot'; it is converted
    // to upper case and appended to the file name ... Described games will be listed with a
    // leading space." The port files a described game as qs<hash of the description>.
    void SaveNames(std::string const& Arg, std::string* OutFile, std::wstring* OutDescription)
    {
        if (Arg.size() <= 1)
        {
            char const Slot = Arg.empty() ? 0 : static_cast<char>(toupper(static_cast<unsigned char>(Arg[0])));
            *OutFile = "qsave";
            *OutDescription = L"Quick Save";
            if (Slot != 0)
            {
                *OutFile += Slot;
                *OutDescription += L' ';
                *OutDescription += static_cast<wchar_t>(Slot);
            }
            return;
        }
        std::string Lower = Arg;
        for (char& C : Lower)
            C = static_cast<char>(tolower(static_cast<unsigned char>(C)));
        char File[32];
        snprintf(File, sizeof(File), "qs%08x", CreateID(Lower.c_str()));
        *OutFile = File;
        *OutDescription = L" ";
        for (char const C : Arg)
            *OutDescription += static_cast<wchar_t>(static_cast<unsigned char>(C));
    }

    // The saved game of that file name ("x.fl"), from the server's list; null when none.
    CCharacterSet::CharacterInfo const* FindSave(char const* FileName)
    {
        if (G_LoginMgr.RequestCharacterInfo() != 1)
            return nullptr;
        for (int Index = 0;; ++Index)
        {
            CCharacterSet::CharacterInfo const* const Info = G_LoginMgr.CharacterAt(Index);
            if (Info == nullptr)
                return nullptr;
            if (_stricmp(Info->Id.Buffer, FileName) == 0)
                return Info;
        }
    }

    // The newest of the console's own saves (qsave*.fl, qs*.fl).
    CCharacterSet::CharacterInfo const* NewestConsoleSave()
    {
        if (G_LoginMgr.RequestCharacterInfo() != 1)
            return nullptr;
        CCharacterSet::CharacterInfo const* Newest = nullptr;
        for (int Index = 0;; ++Index)
        {
            CCharacterSet::CharacterInfo const* const Info = G_LoginMgr.CharacterAt(Index);
            if (Info == nullptr)
                return Newest;
            if (_strnicmp(Info->Id.Buffer, "qs", 2) != 0)
                continue;
            if (Newest == nullptr || Info->TimestampHigh > Newest->TimestampHigh ||
                (Info->TimestampHigh == Newest->TimestampHigh && Info->TimestampLow > Newest->TimestampLow))
                Newest = Info;
        }
    }

    bool SaveGame(std::string const& Arg)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return false;
        std::string File;
        std::wstring Description;
        SaveNames(Arg, &File, &Description);
        return Api->SaveNamed(Player(), File.c_str(), Description.c_str()) == 0;
    }

    bool LoadGame(std::string const& Arg)
    {
        CCharacterSet::CharacterInfo const* Info = nullptr;
        if (Arg.empty())
            Info = NewestConsoleSave();
        else
        {
            std::string File;
            std::wstring Description;
            SaveNames(Arg, &File, &Description);
            File += ".fl";
            Info = FindSave(File.c_str());
        }
        if (Info == nullptr)
            return false;
        QueueLoadGameCharacter(&Info->Id, Info->bIsAutoSave != 0);
        return true;
    }

    // --- Ships ------------------------------------------------------------------------------

    // The ship archetype a name stands for: a ship nickname, a hull good or a ship package.
    unsigned ResolveShipArch(std::string const& Name, GoodInfo const** OutPackage)
    {
        *OutPackage = nullptr;
        unsigned const Id = CreateID(Name.c_str());
        unsigned Arch = Archetype::GetShip(Id) != nullptr ? Id : 0;
        GoodInfo const* const Good = GoodList::find_by_nickname(Name.c_str());
        if (Arch == 0 && Good != nullptr)
        {
            if (Good->Category == GoodCategoryShipHull)
                Arch = Good->ArchId;
            else if (Good->Category == GoodCategoryShip)
            {
                GoodInfo const* const Hull = GoodList::find_by_id(Good->HullGoodId);
                if (Hull != nullptr)
                    Arch = Hull->ArchId;
                *OutPackage = Good;
            }
        }
        if (Arch == 0)
            return 0;
        if (*OutPackage == nullptr)
        {
            // The dealer package of that hull, for its addons (power plant, engine, ...).
            GoodInfo const* const Hull = GoodList_get()->find_by_ship_arch(Arch);
            std::list<GoodInfo*> const* const Goods = GoodList_get()->get_list();
            if (Hull != nullptr && Goods != nullptr)
                for (GoodInfo const* Candidate : *Goods)
                    if (Candidate->Category == GoodCategoryShip && Candidate->HullGoodId == Hull->Id)
                    {
                        *OutPackage = Candidate;
                        break;
                    }
        }
        return Arch;
    }

    bool ShipHasHardpoint(Archetype::Ship const* Ship, char const* Hardpoint)
    {
        if (Hardpoint == nullptr || Hardpoint[0] == 0)
            return false;
        for (Archetype::LegalHpTypes const& Type : Ship->LegalHps)
            for (CacheString const& Hp : Type.Hardpoints)
                if (Hp.Value != nullptr && _stricmp(Hp.Value, Hardpoint) == 0)
                    return true;
        return false;
    }

    // The loadout of a new ship: the package's addons, then what the player has -- cargo and
    // unmounted items as they are, mounted items on a hardpoint the new ship has (and the
    // addons left free), the rest unmounted into the hold.
    void BuildLoadout(unsigned Arch, GoodInfo const* Package, EquipDescList const& Current, EquipDescList* Out)
    {
        Archetype::Ship const* const Ship = Archetype::GetShip(Arch);
        std::vector<std::string> Taken;
        if (Package != nullptr)
            for (EquipDesc const& Addon : Package->AddonLow)
            {
                EquipDesc Copy(Addon);
                Copy.set_id(SUBOBJ_ID_NONE);
                Out->add_equipment_item(Copy, false);
                if (Copy.Hardpoint != nullptr)
                    Taken.push_back(Copy.Hardpoint);
            }
        for (EquipDesc const& Item : Current.Items)
        {
            EquipDesc Copy(Item);
            Copy.set_id(SUBOBJ_ID_NONE);
            if (Copy.is_equipped() && !Copy.is_internal())
            {
                bool bFree = Ship != nullptr && ShipHasHardpoint(Ship, Copy.Hardpoint);
                for (std::string const& Hp : Taken)
                    if (bFree && _stricmp(Hp.c_str(), Copy.Hardpoint) == 0)
                        bFree = false;
                if (bFree)
                    Taken.push_back(Copy.Hardpoint);
                else
                {
                    Copy.set_equipped(0);
                    Copy.make_internal();
                }
            }
            else if (Copy.is_equipped() && Copy.is_internal() && Package != nullptr)
            {
                // Internal equipment the package already provides (a power plant, say) would
                // double up: carry the old one in the hold.
                bool bDuplicate = false;
                for (EquipDesc const& Addon : Package->AddonLow)
                    if (Addon.is_internal() && Archetype::GetEquipment(Addon.ArchId) != nullptr &&
                        Archetype::GetEquipment(Copy.ArchId) != nullptr &&
                        Archetype::GetEquipment(Addon.ArchId)->get_class_type() ==
                            Archetype::GetEquipment(Copy.ArchId)->get_class_type())
                        bDuplicate = true;
                if (bDuplicate)
                    Copy.set_equipped(0);
            }
            Out->add_equipment_item(Copy, false);
        }
    }

    // --- Reputation -------------------------------------------------------------------------

    // A reputation value: -1..1, a percentage, or HD's letters H h nh n nf f F.
    bool ParseRep(char const* Text, float* Out)
    {
        static struct { char const* Word; float Value; } const kWords[] = {
            {"H", -0.9f}, {"h", -0.6f}, {"nh", -0.2f}, {"n", 0.0f}, {"nf", 0.2f}, {"f", 0.6f}, {"F", 0.9f}};
        for (auto const& Word : kWords)
            if (strcmp(Text, Word.Word) == 0)
            {
                *Out = Word.Value;
                return true;
            }
        char* End = nullptr;
        double Value = strtod(Text, &End);
        if (End == Text)
            return false;
        if (*End == '%')
            Value /= 100.0;
        if (Value < -1.0)
            Value = -1.0;
        if (Value > 1.0)
            Value = 1.0;
        *Out = static_cast<float>(Value);
        return true;
    }

    // --- The nav-map state ------------------------------------------------------------------

    unsigned char G_VisitBits = 0;
    bool G_bHouseLabels = true;
    float G_ZoomSeconds = -1.0f;

    // --- Commands ---------------------------------------------------------------------------

    typedef void (*FCommand)(std::vector<std::string> const& Args, std::string const& Rest);

    void CmdHelp(std::vector<std::string> const&, std::string const&);

    void CmdCash(std::vector<std::string> const& Args, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        int Cash = 0;
        Api->InspectCash(Player(), &Cash);
        if (!Args.empty())
        {
            char const* const Text = Args[0].c_str();
            long const Amount = strtol(Text, nullptr, 10);
            int const Delta = (Text[0] == '+' || Text[0] == '-') ? static_cast<int>(Amount)
                                                                 : static_cast<int>(Amount) - Cash;
            Api->AdjustCash(Player(), Delta);
            Api->InspectCash(Player(), &Cash);
        }
        Print("Cash: %d", Cash);
    }

    void CmdRep(std::vector<std::string> const& Args, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        if (Args.empty())
        {
            Print("rep faction [value]");
            return;
        }
        int Vibe = 0;
        if (Api->GetPlayerVibe(Player(), &Vibe) != 0)
            return;
        unsigned Group = 0;
        std::string Nickname = Args[0];
        if (Api->GetGroup(Nickname.c_str(), &Group) != 0 || Group == 0)
        {
            Nickname += "_grp";
            if (Api->GetGroup(Nickname.c_str(), &Group) != 0 || Group == 0)
            {
                Print("Faction not found.");
                return;
            }
        }
        float Value = 0.0f;
        if (Args.size() > 1)
        {
            if (!ParseRep(Args[1].c_str(), &Value))
            {
                Print("Bad value %s.", Args[1].c_str());
                return;
            }
            Api->SetRep(Vibe, Group, Value);
        }
        Api->GetRep(Vibe, Group, &Value);
        Print("%s: %.2f", Nickname.c_str(), Value);
    }

    void CmdPos(std::vector<std::string> const&, std::string const& Rest)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        Vector Position;
        Matrix Orientation;
        Api->GetLocation(Ship, &Position, &Orientation);
        if (!Rest.empty())
        {
            float Values[3] = {Position.x, Position.y, Position.z};
            if (!ParseTriple(Rest, Values))
            {
                Print("pos [x, y, z]");
                return;
            }
            unsigned System = 0;
            Api->GetSystem(Ship, &System);
            Position.x = Values[0];
            Position.y = Values[1];
            Position.z = Values[2];
            Api->Relocate(Ship, System, &Position, &Orientation);
        }
        Print("Position: %.0f, %.0f, %.0f", Position.x, Position.y, Position.z);
    }

    // HD: "system [system [object | pos [, rot]]] -- jump to a system or display current
    // system's nickname". The port keeps the ship's orientation.
    void CmdSystem(std::vector<std::string> const& Args, std::string const& Rest)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr)
            return;
        if (Args.empty())
        {
            unsigned System = 0;
            if (PlayerShipQuiet() != 0)
                Api->GetSystem(PlayerShipQuiet(), &System);
            else
                Api->GetPlayerSystem(Player(), &System);
            Universe::ISystem const* const Info = Universe::get_system(System);
            Print("System: %s", Info != nullptr ? Info->GetNickname() : "?");
            return;
        }
        if (!PlayerShip(&Ship))
            return;
        unsigned Current = 0;
        Api->GetSystem(Ship, &Current);
        unsigned const Target = ResolveSystem(Args[0], Current);
        if (Target == 0)
        {
            Print("System not found.");
            return;
        }
        // What follows the system: a position, an object of that system, or nothing.
        size_t const After = Rest.find(Args[0]) + Args[0].size();
        std::string Where = Rest.substr(After);
        size_t const From = Where.find_first_not_of(" \t");
        Where = From == std::string::npos ? std::string() : Where.substr(From);
        Vector Pos;
        if (Where.empty())
            Pos = DefaultSystemPosition(Target);
        else if (LooksLikePosition(Where))
        {
            float Values[3] = {0.0f, 0.0f, 0.0f};
            if (!ParseTriple(Where, Values))
            {
                Print("system [system [object | x, y, z]]");
                return;
            }
            Pos.x = Values[0];
            Pos.y = Values[1];
            Pos.z = Values[2];
        }
        else
        {
            Universe::FSystemObject Object;
            if (!ResolveObject(Target, Where, &Object))
            {
                Print("Object not found.");
                return;
            }
            Pos = BesideObject(Object, Target == Current);
        }
        if (!MoveShip(Ship, Target, Pos))
            Print("System NOT changed.");
        Universe::ISystem const* const Info = Universe::get_system(Target);
        Print("System: %s", Info != nullptr ? Info->GetNickname() : "?");
    }

    // HD: "jump z, x, y -- move forwards/backwards, right/left, up/down" and "jump [object] --
    // move to the (targetted) object". Port: "jump <system>[,<base>]" when the name is no object
    // of this system -- the system's default place, or docked at that base.
    void CmdJump(std::vector<std::string> const& Args, std::string const& Rest)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        Vector Position;
        Matrix Orientation;
        unsigned Current = 0;
        Api->GetLocation(Ship, &Position, &Orientation);
        Api->GetSystem(Ship, &Current);

        if (LooksLikePosition(Rest))
        {
            float Values[3] = {0.0f, 0.0f, 0.0f}; // z (forward), x (right), y (up)
            if (!ParseTriple(Rest, Values))
            {
                Print("jump z, x, y");
                return;
            }
            // Ships face -z of their frame: forward is -column 2, right column 0, up column 1.
            Vector Local;
            Local.x = Values[1];
            Local.y = Values[2];
            Local.z = -Values[0];
            Csys Frame;
            Frame.init(Position, Orientation);
            Vector const Pos = Frame.trans_from_frame(Local);
            Api->Relocate(Ship, Current, &Pos, &Orientation);
            Print("Position: %.0f, %.0f, %.0f", Pos.x, Pos.y, Pos.z);
            return;
        }

        if (Args.empty())
        {
            unsigned Target = 0;
            Vector TargetPos;
            Matrix TargetOrient;
            if (Api->GetTarget(Ship, &Target) != 0 || Target == 0 ||
                Api->GetLocation(Target, &TargetPos, &TargetOrient) != 0)
            {
                Print("Object not found.");
                return;
            }
            float Radius = 0.0f;
            Vector Center = TargetPos;
            if (Api->GetRadius(Target, &Radius, &Center) != 0)
                Radius = 0.0f;
            Vector Pos = Center;
            Pos.z += Radius * 1.5f + 500.0f;
            Api->Relocate(Ship, Current, &Pos, &Orientation);
            Print("Position: %.0f, %.0f, %.0f", Pos.x, Pos.y, Pos.z);
            return;
        }

        Universe::FSystemObject Object;
        if (ResolveObject(Current, Args[0], &Object))
        {
            Vector const Pos = BesideObject(Object, true);
            Api->Relocate(Ship, Current, &Pos, &Orientation);
            Print("Position: %.0f, %.0f, %.0f", Pos.x, Pos.y, Pos.z);
            return;
        }

        // Port: a system, with an optional base after a comma.
        std::string SystemName = Rest;
        std::string BaseName;
        size_t const Comma = Rest.find(',');
        if (Comma != std::string::npos)
        {
            SystemName = Rest.substr(0, Comma);
            BaseName = Rest.substr(Comma + 1);
            size_t const From = BaseName.find_first_not_of(" \t");
            BaseName = From == std::string::npos ? std::string() : BaseName.substr(From);
            size_t const To = SystemName.find_last_not_of(" \t");
            SystemName = To == std::string::npos ? std::string() : SystemName.substr(0, To + 1);
        }
        unsigned const Target = ResolveSystem(SystemName, Current);
        if (Target == 0)
        {
            Print("Object not found.");
            return;
        }
        Universe::IBase const* Base = nullptr;
        if (!BaseName.empty())
        {
            Base = ResolveBase(BaseName);
            if (Base == nullptr || Base->SystemId != Target)
            {
                Print("Base not found.");
                return;
            }
        }
        if (Base != nullptr)
        {
            // Landing works across systems (IServer::Dock's second half), without a move.
            Api->ForceLand(Player(), Base->NicknameId);
            Print("Docking at %s.", Base->GetNickname());
            return;
        }
        if (!MoveShip(Ship, Target, DefaultSystemPosition(Target)))
        {
            Print("System NOT changed.");
            return;
        }
        Universe::ISystem const* const Info = Universe::get_system(Target);
        Print("System: %s", Info != nullptr ? Info->GetNickname() : "?");
    }

    // HD: "base -- display base's nickname" and "base base -- go to a base". In space the ship
    // lands there (across systems too); docked, the base changes under the player as the
    // universe menu's base change does.
    void CmdBase(std::vector<std::string> const& Args, std::string const& Rest)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        if (Args.empty())
        {
            unsigned BaseId = GetPlayerBaseIdRetail();
            if ((BaseId & 0x3fffffff) == 0)
                Api->GetBase(Player(), &BaseId);
            Universe::IBase const* const Base = (BaseId & 0x3fffffff) != 0 ? Universe::get_base(BaseId) : nullptr;
            Print("Base: %s", Base != nullptr ? Base->GetNickname() : "none (in space)");
            return;
        }
        Universe::IBase const* const Base = ResolveBase(Rest);
        if (Base == nullptr)
        {
            Print("Base not found.");
            return;
        }
        unsigned const Ship = PlayerShipQuiet();
        if (Ship != 0)
        {
            Api->ForceLand(Player(), Base->NicknameId);
            Print("Docking at %s.", Base->GetNickname());
            return;
        }
        if ((GetPlayerBaseIdRetail() & 0x3fffffff) == 0)
        {
            Print("Not in a base.");
            return;
        }
        if (Base->NicknameId == GetPlayerBaseIdRetail())
        {
            Print("Base: %s", Base->GetNickname());
            return;
        }
        BaseData const* const Data = BaseDataList_get()->get_base_data(Base->NicknameId);
        if (Data == nullptr || Data->StartRoomId == 0)
        {
            Print("Base has no rooms.");
            return;
        }
        ChangeClientLocation(Data->StartRoomId);
        Print("Base: %s", Base->GetNickname());
    }

    void CmdGodmode(std::vector<std::string> const&, std::string const&)
    {
        static bool bGod = false;
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        bGod = !bGod;
        Api->SetInvincible(Ship, bGod);
        Print("Godmode: %s", bGod ? "on" : "off");
    }

    void CmdSave(std::vector<std::string> const&, std::string const& Rest)
    {
        std::string Arg = Rest;
        size_t const To = Arg.find_last_not_of(" \t");
        Arg = To == std::string::npos ? std::string() : Arg.substr(0, To + 1);
        Print(SaveGame(Arg) ? "Game saved." : "Game NOT saved.");
    }

    void CmdLoad(std::vector<std::string> const&, std::string const& Rest)
    {
        std::string Arg = Rest;
        size_t const To = Arg.find_last_not_of(" \t");
        Arg = To == std::string::npos ? std::string() : Arg.substr(0, To + 1);
        if (!LoadGame(Arg))
            Print("Game not found.");
    }

    void CmdAutosave(std::vector<std::string> const&, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        Api->Save(Player(), 1);
        Print("Game saved.");
    }

    void CmdAutoload(std::vector<std::string> const&, std::string const&)
    {
        CCharacterSet::CharacterInfo const* const Info = FindSave("AutoSave.fl");
        if (Info == nullptr)
        {
            Print("Game not found.");
            return;
        }
        QueueLoadGameCharacter(&Info->Id, true);
    }

    // Port: "ship archetype" -- the player's ship becomes that archetype (a ship nickname, hull
    // good or package), with the package's addons and the player's items. Docked it is the
    // dealer's own change (seen at launch); in space the game is saved and reloaded in the new
    // ship where it was.
    void CmdShip(std::vector<std::string> const& Args, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        if (Args.empty())
        {
            Archetype::Ship const* const Ship = Archetype::GetShip(GetPlayerShipArchetypeIdRetail());
            Print("Ship: %s", Ship != nullptr ? IdsText(Ship->IdsName).c_str() : "?");
            return;
        }
        GoodInfo const* Package = nullptr;
        unsigned const Arch = ResolveShipArch(Args[0], &Package);
        if (Arch == 0)
        {
            Print("Ship not found.");
            return;
        }
        EquipDescList Loadout;
        BuildLoadout(Arch, Package, GetPlayerEquipDescListRetail(), &Loadout);

        unsigned const ShipObj = PlayerShipQuiet();
        if (ShipObj == 0 && (GetPlayerBaseIdRetail() & 0x3fffffff) != 0)
        {
            // The dealer's sequence (nn_dealer.cpp): collision groups whole, the loadout, the
            // hull, full health; the weapon groups dropped.
            std::list<CollisionGroupDesc> Groups = GetPlayerCollisionGroupListRetail();
            for (CollisionGroupDesc& Group : Groups)
                Group.Health = 1.0f;
            ReqPlayerCollisionGroupsRetail(&Groups);
            ReqPlayerEquipmentRetail(&Loadout);
            ReqPlayerShipArchRetail(Arch);
            ReqPlayerHullStatusRetail(1.0f);
            Print("Ship: %s (at launch).", IdsText(Archetype::GetShip(Arch)->IdsName).c_str());
            return;
        }
        if (ShipObj == 0)
        {
            Print("Not in a game.");
            return;
        }
        EquipDescVector const Vector(Loadout);
        if (Api->SetShipAndLoadout(Player(), Arch, &Vector) != 0 ||
            Api->SaveNamed(Player(), "qsaveship", L"Quick Save (ship change)") != 0)
        {
            Print("Ship NOT changed.");
            return;
        }
        CCharacterSet::CharacterInfo const* const Info = FindSave("qsaveship.fl");
        if (Info == nullptr)
        {
            Print("Ship changed; load a game to fly it.");
            return;
        }
        Print("Ship: %s", IdsText(Archetype::GetShip(Arch)->IdsName).c_str());
        QueueLoadGameCharacter(&Info->Id, false);
    }

    // HD: "show -- toggles display of everything on the Nav Map", "show houses -- toggles display
    // of the House labels on the Universe Map", "show paths -- toggles filtering of patrol paths
    // on the Nav Map".
    void CmdShow(std::vector<std::string> const& Args, std::string const&)
    {
        if (Args.empty())
        {
            G_VisitBits ^= 1;
            Print("Show: %s", (G_VisitBits & 1) != 0 ? "on" : "off");
        }
        else if (_strnicmp(Args[0].c_str(), "houses", Args[0].size()) == 0)
        {
            G_bHouseLabels = !G_bHouseLabels;
            Print("House labels: %s", G_bHouseLabels ? "on" : "off");
        }
        else if (_strnicmp(Args[0].c_str(), "paths", Args[0].size()) == 0)
        {
            G_VisitBits ^= 4;
            Print("Patrol Paths: %s", (G_VisitBits & 4) != 0 ? "unfiltered" : "filtered");
        }
        else
            Print("show [houses|paths]");
    }

    // HD: "zoom [time] -- display/set time to zoom Nav Map".
    void CmdZoom(std::vector<std::string> const& Args, std::string const&)
    {
        float Value = 0.0f;
        if (!Args.empty() && ParseNumber(Args[0], &Value) && Value >= 0.0f)
            G_ZoomSeconds = Value;
        Print("Zoom time = %g", HdConsoleNavMapZoomSeconds(1.5f));
    }

    void CmdMonkey(std::vector<std::string> const&, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api != nullptr && Api->SetCostume(Player(), 1) == 0)
            Print("Monkey.");
    }

    void CmdRobot(std::vector<std::string> const&, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api != nullptr && Api->SetCostume(Player(), 2) == 0)
            Print("Robot.");
    }

    void CmdTrent(std::vector<std::string> const&, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api != nullptr && Api->SetCostume(Player(), 0) == 0)
            Print("Trent.");
    }

    void CmdTime(std::vector<std::string> const&, std::string const&)
    {
        time_t const Now = time(nullptr);
        char Text[64];
        strftime(Text, sizeof(Text), "%Y-%m-%d %H:%M:%S", localtime(&Now));
        Print("%s", Text);
    }

    void CmdCoin(std::vector<std::string> const&, std::string const&)
    {
        Print("%s", (rand() & 1) != 0 ? "Heads" : "Tails");
    }

    void CmdDice(std::vector<std::string> const& Args, std::string const&)
    {
        int const Sides = !Args.empty() ? atoi(Args[0].c_str()) : 6;
        Print("%d", Sides > 0 ? 1 + rand() % Sides : 0);
    }

    void CmdPing(std::vector<std::string> const&, std::string const&)
    {
        Print("Pong!");
    }

    void CmdPrint(std::vector<std::string> const&, std::string const& Rest)
    {
        Print("%s", Rest.c_str());
    }

    void CmdAbout(std::vector<std::string> const&, std::string const&)
    {
        Print("Console by Jason Hood (adoxa), version 1.24, as rebuilt in the macOS port.");
    }

    struct FCommandEntry
    {
        char const* Name;
        FCommand Run;
        char const* Group;
        char const* Usage;
    };

    // Groups and usage as HD's help gives them; "ship" and "jump system[,base]" are the port's.
    FCommandEntry const kCommands[] = {
        {"help", CmdHelp, nullptr, "help [command] -- the groups, or a command's usage"},
        {"commands", CmdHelp, nullptr, nullptr},
        {"autosave", CmdAutosave, "GAME", "autosave -- save the Auto Save game"},
        {"autoload", CmdAutoload, "GAME", "autoload -- load the Auto Save game"},
        {"save", CmdSave, "GAME", "save [slot|desc] -- save the game (a one-letter slot, or a description)"},
        {"load", CmdLoad, "GAME", "load [slot|desc] -- load a saved game (the newest console save by default)"},
        {"jump", CmdJump, "MOVE", "jump z, x, y | jump [object] | jump system[,base] -- move the ship"},
        {"pos", CmdPos, "MOVE", "pos [x, y, z] -- display/set position"},
        {"base", CmdBase, "MOVE", "base [base] -- display the base, or go to a base"},
        {"system", CmdSystem, "MOVE", "system [system [object | x, y, z]] -- display or jump to a system"},
        {"godmode", CmdGodmode, "SHIP", "godmode -- toggle invincibility"},
        {"ship", CmdShip, "SHIP", "ship [archetype] -- display or change the ship (port)"},
        {"show", CmdShow, "HUD", "show [houses|paths] -- Nav Map: everything, House labels, patrol path filtering"},
        {"zoom", CmdZoom, "HUD", "zoom [time] -- display/set time to zoom Nav Map"},
        {"cash", CmdCash, "PLAYER", "cash [[+|-]amount] -- display/set (relative) amount of money"},
        {"rep", CmdRep, "PLAYER", "rep faction [value|H|h|nh|n|nf|f|F] -- display/set reputation"},
        {"monkey", CmdMonkey, "PLAYER", "monkey -- give Trent a monkey's head (next dock)"},
        {"robot", CmdRobot, "PLAYER", "robot -- turn Trent into a robot (next dock)"},
        {"trent", CmdTrent, "PLAYER", "trent -- restore original Trent (next dock)"},
        {"time", CmdTime, "MISC", "time"},
        {"coin", CmdCoin, "MISC", "coin"},
        {"dice", CmdDice, "MISC", "dice [sides]"},
        {"ping", CmdPing, "MISC", "ping"},
        {"print", CmdPrint, "MISC", "print text"},
        {"say", CmdPrint, nullptr, nullptr},
        {"about", CmdAbout, nullptr, nullptr},
    };

    void CmdHelp(std::vector<std::string> const& Args, std::string const&)
    {
        if (!Args.empty())
        {
            bool bAny = false;
            for (FCommandEntry const& Command : kCommands)
                if (Command.Usage != nullptr && _strnicmp(Command.Name, Args[0].c_str(), Args[0].size()) == 0)
                {
                    Print("%s", Command.Usage);
                    bAny = true;
                }
            if (!bAny)
                Print("Unknown command: %s", Args[0].c_str());
            return;
        }
        static char const* const kGroups[] = {"GAME", "MOVE", "SHIP", "HUD", "PLAYER", "MISC"};
        for (char const* Group : kGroups)
        {
            std::string Line = std::string(Group) + ":";
            for (FCommandEntry const& Command : kCommands)
                if (Command.Group != nullptr && strcmp(Command.Group, Group) == 0)
                    Line += std::string(" ") + Command.Name + ",";
            Line.erase(Line.size() - 1);
            Print("%s", Line.c_str());
        }
        Print("Enter help command for its usage. Commands can be abbreviated to a unique prefix.");
    }
}

unsigned char HdConsoleVisitBits()
{
    return G_VisitBits;
}

bool HdConsoleHouseLabels()
{
    return G_bHouseLabels;
}

float HdConsoleNavMapZoomSeconds(float Default)
{
    return G_ZoomSeconds >= 0.0f ? G_ZoomSeconds : Default;
}

void HdConsoleExecute(wchar_t const* Line)
{
    // Commands are ASCII; any longer prefix that names one command selects it.
    std::string Text;
    for (wchar_t const* P = Line; *P != 0; ++P)
        Text += static_cast<char>(*P < 0x80 ? *P : '?');
    size_t const Start = Text.find_first_not_of(" \t");
    if (Start == std::string::npos)
        return;
    Text = Text.substr(Start);
    fprintf(stderr, "[hd console] > %s\n", Text.c_str());
    size_t const NameEnd = Text.find_first_of(" \t");
    std::string const Name = Text.substr(0, NameEnd);
    std::string Rest = NameEnd == std::string::npos ? std::string() : Text.substr(NameEnd + 1);
    size_t const RestStart = Rest.find_first_not_of(" \t");
    Rest = RestStart == std::string::npos ? std::string() : Rest.substr(RestStart);
    std::vector<std::string> Args;
    for (size_t Pos = 0; Pos < Rest.size();)
    {
        size_t const From = Rest.find_first_not_of(" \t", Pos);
        if (From == std::string::npos)
            break;
        size_t const To = Rest.find_first_of(" \t", From);
        Args.push_back(Rest.substr(From, To == std::string::npos ? std::string::npos : To - From));
        Pos = To == std::string::npos ? Rest.size() : To;
    }

    FCommandEntry const* Match = nullptr;
    int Matches = 0;
    for (FCommandEntry const& Command : kCommands)
    {
        if (_stricmp(Command.Name, Name.c_str()) == 0)
        {
            Match = &Command;
            Matches = 1;
            break;
        }
        if (_strnicmp(Command.Name, Name.c_str(), Name.size()) == 0)
        {
            Match = &Command;
            ++Matches;
        }
    }
    if (Matches != 1)
    {
        Print(Matches == 0 ? "Unknown command: %s (try help)" : "Ambiguous command: %s", Name.c_str());
        return;
    }
    Match->Run(Args, Rest);
}
