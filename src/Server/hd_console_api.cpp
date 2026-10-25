// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: the Server.dll entry points Freelancer: HD Edition's console.dll uses (hd_console_api.h):
// its pub:: imports, what it reads off PlayerData, and the state behind its patches of
// IServerImpl::SystemSwitchOutComplete and of the save-game writer.

#include "hd_console_api.h"
#include "common_reputation.h" // CostumeDescriptions
#include "common_server.h"
#include "common_server_playerdb.h" // Players
#include "server_savegame.h"        // PlayerData_SaveGame

#include <cstring>

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
        SERVER_API int __cdecl PopUpDialog(unsigned int player_id, const FmtStr& title, const FmtStr& text,
                                           unsigned int param);
    }
    namespace SpaceObj
    {
        SERVER_API int __cdecl GetSolarArchetypeID(unsigned int obj_id, unsigned int& out_arch_id);
        SERVER_API int __cdecl GetBurnRadius(const unsigned int& obj_id_ref, float& out_radius,
                                             Vector& out_center);
        SERVER_API int __cdecl IsPosEmpty(const unsigned int& system_id_ref, const Vector& pos,
                                          const float& radius, bool& out_empty);
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
    int GetPlayerVibe(unsigned Player, int* OutVibe)
    {
        return pub::Player::GetRep(Player, *OutVibe);
    }
    int GetAssetValue(unsigned Player, float* OutValue)
    {
        return pub::Player::GetAssetValue(Player, *OutValue);
    }
    int ForceLand(unsigned Player, unsigned Base)
    {
        return pub::Player::ForceLand(Player, Base);
    }
    int SetShipAndLoadout(unsigned Player, unsigned Arch, EquipDescVector const* Loadout)
    {
        return pub::Player::SetShipAndLoadout(Player, Arch, *Loadout);
    }
    int SetCostume(unsigned Player, int Which)
    {
        if (Which == 1)
            return pub::Player::SetMonkey(Player);
        if (Which == 2)
            return pub::Player::SetRobot(Player);
        return pub::Player::SetTrent(Player);
    }
    int PopUpDialog(unsigned Player, FmtStr const* Title, FmtStr const* Text, unsigned Buttons)
    {
        return pub::Player::PopUpDialog(Player, *Title, *Text, Buttons);
    }
    void SetBaseCostume(unsigned Player, unsigned Costume)
    {
        if (!Players.is_valid(Player))
            return;
        GetCostumeDescriptions()->get_costume(static_cast<int>(Costume), Players[Player].costume);
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
    struct FGroupEnumerator : pub::Reputation::Enumerator
    {
        FHdConsoleGroupFn Fn;
        void* Arg;
        char Execute(unsigned Group) override
        {
            return Fn(Group, Arg) ? 1 : 0;
        }
    };
    int EnumerateGroups(FHdConsoleGroupFn Fn, void* Arg)
    {
        FGroupEnumerator Enumerator;
        Enumerator.Fn = Fn;
        Enumerator.Arg = Arg;
        return pub::Reputation::EnumerateGroups(Enumerator);
    }
    int GetShortGroupName(unsigned Group, unsigned* OutIds)
    {
        return pub::Reputation::GetShortGroupName(Group, *OutIds);
    }

    int GetLocation(unsigned Obj, Vector* OutPos, Matrix* OutOrient)
    {
        return pub::SpaceObj::GetLocation(Obj, *OutPos, *OutOrient);
    }
    int Relocate(unsigned Ship, unsigned System, Vector const* Pos, Matrix const* Orient)
    {
        return pub::SpaceObj::Relocate(Ship, System, *Pos, *Orient);
    }
    int GetSystem(unsigned Obj, unsigned* OutSystem)
    {
        return pub::SpaceObj::GetSystem(Obj, *OutSystem);
    }
    int GetTarget(unsigned Ship, unsigned* OutTarget)
    {
        return pub::SpaceObj::GetTarget(Ship, *OutTarget);
    }
    int GetType(unsigned Obj, unsigned* OutType)
    {
        return pub::SpaceObj::GetType(Obj, *OutType);
    }
    int GetRadius(unsigned Obj, float* OutRadius, Vector* OutCenter)
    {
        return pub::SpaceObj::GetRadius(Obj, *OutRadius, *OutCenter);
    }
    int GetBurnRadius(unsigned Obj, float* OutRadius, Vector* OutCenter)
    {
        return pub::SpaceObj::GetBurnRadius(Obj, *OutRadius, *OutCenter);
    }
    int GetSolarArchetypeID(unsigned Obj, unsigned* OutArch)
    {
        return pub::SpaceObj::GetSolarArchetypeID(Obj, *OutArch);
    }
    int IsPosEmpty(unsigned System, Vector const* Pos, float Radius, bool* OutEmpty)
    {
        return pub::SpaceObj::IsPosEmpty(System, *Pos, Radius, *OutEmpty);
    }

    struct FSysObjEnumerator : pub::System::SysObjEnumerator
    {
        FHdConsoleSysObjFn Fn;
        void* Arg;
        bool Slot00(pub::System::FSysObj const* Object) override
        {
            FHdConsoleSysObj Record;
            Record.Id = CreateID(Object->Nickname);
            Record.Nickname = Object->Nickname;
            Record.IdsName = Object->IdsName;
            Record.ArchetypeId = Object->ArchetypeId;
            return Fn(&Record, Arg);
        }
    };
    int EnumerateObjects(unsigned System, FHdConsoleSysObjFn Fn, void* Arg)
    {
        FSysObjEnumerator Enumerator;
        Enumerator.Fn = Fn;
        Enumerator.Arg = Arg;
        return pub::System::EnumerateObjects(System, Enumerator);
    }

    int Save(unsigned Player, unsigned SaveType)
    {
        return pub::Save(Player, SaveType);
    }
    int SaveNamed(unsigned Player, char const* Name, wchar_t const* Description)
    {
        if (!SinglePlayer() || !Players.is_valid(Player))
            return 0;
        return PlayerData_SaveGame(Players[Player], Name, Description, 0) ? 1 : 0;
    }

    bool IsRepaired(unsigned Player)
    {
        // FUN_06113ee0: Players[1]+0x268 == 1.0f, then every node of the +0x270 list (its
        // float at +0xc: CollisionGroupDesc::Health) and of the +0x27c list (+0x18:
        // EquipDesc::Health) == 1.0f, compared as the dword 0x3f800000.
        if (!Players.is_valid(Player))
            return false;
        PlayerData const& Slot = Players[Player];
        if (Slot.relative_health != 1.0f)
            return false;
        for (CollisionGroupDesc const& Group : Slot.collision_groups)
            if (Group.Health != 1.0f)
                return false;
        for (EquipDesc const& Item : Slot.equipment.Items)
            if (Item.Health != 1.0f)
                return false;
        return true;
    }
    unsigned GetWeaponGroups(unsigned Player, char* Buffer, unsigned Size)
    {
        if (!Players.is_valid(Player))
            return 0;
        std::string const& Text = Players[Player].weapon_group_name;
        if (Buffer != nullptr && Size > 0)
        {
            size_t const N = Text.size() < Size - 1 ? Text.size() : Size - 1;
            memcpy(Buffer, Text.data(), N);
            Buffer[N] = 0;
        }
        return static_cast<unsigned>(Text.size());
    }
    void SetWeaponGroups(unsigned Player, char const* Text)
    {
        if (Players.is_valid(Player))
            Players[Player].weapon_group_name = Text != nullptr ? Text : "";
    }

    // --- the SystemSwitchOutComplete patches ---
    struct FSwitchOverride
    {
        unsigned Player = 0;
        unsigned System = 0;
        unsigned Object = 0;
        Vector Pos;
        Matrix Orient;
    } GSwitch;

    void SetSystemSwitch(unsigned Player, unsigned System, unsigned Object, Vector const* Pos,
                         Matrix const* Orient)
    {
        GSwitch.Player = Player;
        GSwitch.System = System;
        GSwitch.Object = Object;
        GSwitch.Pos = *Pos;
        GSwitch.Orient = *Orient;
    }

    FHdConsoleSaveFileFn GOnSaveWritten = nullptr;
    FHdConsoleSaveFileFn GOnSaveDeleted = nullptr;
    void SetSaveFileHooks(FHdConsoleSaveFileFn OnWritten, FHdConsoleSaveFileFn OnDeleted)
    {
        GOnSaveWritten = OnWritten;
        GOnSaveDeleted = OnDeleted;
    }

    FHdConsoleServerApi const GApi = {
        InspectCash,     AdjustCash,       GetShip,         GetShipArch,         GetBase,
        GetPlayerSystem, GetPlayerVibe,    GetAssetValue,   ForceLand,           SetShipAndLoadout,
        SetCostume,      PopUpDialog,      SetBaseCostume,  GetGroup,            GetRep,
        SetRep,          EnumerateGroups,  GetShortGroupName, GetLocation,       Relocate,
        GetSystem,       GetTarget,        GetType,         GetRadius,           GetBurnRadius,
        GetSolarArchetypeID, IsPosEmpty,   EnumerateObjects, Save,               SaveNamed,
        IsRepaired,      GetWeaponGroups,  SetWeaponGroups, SetSystemSwitch,     SetSaveFileHooks};
}

extern "C" __declspec(dllexport) FHdConsoleServerApi const* HdConsoleServerApi()
{
    return &GApi;
}

// --- the Server side of the patches (called from IServerImpl.cpp, server_pub.cpp,
// server_classes.cpp) ---

bool HdConsoleTakeSystemSwitch(unsigned Player, unsigned* OutSystem, unsigned* OutObject, Vector* OutPos,
                               Matrix* OutOrient)
{
    if (GSwitch.Player == 0 || GSwitch.Player != Player)
        return false;
    *OutSystem = GSwitch.System;
    *OutObject = GSwitch.Object;
    *OutPos = GSwitch.Pos;
    *OutOrient = GSwitch.Orient;
    GSwitch.Player = 0; // the unpatch stub (0x061131d0) runs once
    return true;
}

void HdConsoleSystemSwitchStandoff(unsigned Object, Vector* OutPos, Matrix* OutOrient)
{
    // console.dll's replacement of the arrival call (0x061133a0 for 0x06cef85d): the object's
    // location, its larger radius (GetRadius / GetBurnRadius), then by type bits -- a moon,
    // planet or sun (bits 0..2): twice the radius; a docking ring (0x20): 98 along its x axis;
    // a jump gate (0x40): 1.5 radii, turned round (rows 0 and 2 negated); and for all of them
    // the spot is the object's position plus the radius along its z axis, facing as it does.
    Vector Center;
    float Radius = 0.0f, Burn = 0.0f;
    unsigned Type = 0;
    pub::SpaceObj::GetLocation(Object, *OutPos, *OutOrient);
    pub::SpaceObj::GetRadius(Object, Radius, Center);
    pub::SpaceObj::GetBurnRadius(Object, Burn, Center);
    pub::SpaceObj::GetType(Object, Type);
    if (Radius < Burn)
        Radius = Burn;
    Matrix& M = *OutOrient;
    if ((Type & 7) != 0)
        Radius += Radius;
    else if (Type == 0x20)
    {
        OutPos->x += M.E[0][0] * 98.0f;
        OutPos->y += M.E[1][0] * 98.0f;
        OutPos->z += M.E[2][0] * 98.0f;
    }
    else if (Type == 0x40)
    {
        Radius *= 1.5f;
        for (int Column = 0; Column < 3; ++Column)
        {
            M.E[0][Column] = -M.E[0][Column];
            M.E[2][Column] = -M.E[2][Column];
        }
    }
    // pos -= (-R) * column 2
    OutPos->x += Radius * M.E[0][2];
    OutPos->y += Radius * M.E[1][2];
    OutPos->z += Radius * M.E[2][2];
}

void HdConsoleOnSaveWritten(char const* Path)
{
    if (GOnSaveWritten != nullptr)
        GOnSaveWritten(Path);
}

void HdConsoleOnSaveDeleted(char const* Path)
{
    if (GOnSaveDeleted != nullptr)
        GOnSaveDeleted(Path);
}
