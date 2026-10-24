// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements the expanded_hardpoints module of FLUF, the Freelancer Unified
// Framework (TheStarport, https://codeberg.org/TheStarport/FLUF), as Discovery Freelancer ships
// it, in the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Hardpoint subtypes (common_expanded_hardpoints.h). The rules follow the module Discovery
// ships (EXE/modules/expanded_hardpoints.dll of its 2026 patches), which is newer than FLUF's
// published module: it adds internal_hp_subtype and applies the subtype search to every
// equipment-screen hardpoint lookup.

#include "common_expanded_hardpoints.h"

#include "common_goods.h"
#include "common_id.h"
#include "common_ini.h"
#include "hd_plugins.h"

#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    struct FShipRules
    {
        // hp_subtype lines: subtype -> its hardpoints, in file order (several lines append).
        std::unordered_map<unsigned, std::vector<std::string>> Hardpoints;
        // internal_hp_subtype values, in file order.
        std::vector<unsigned> Internal;
        // hp_info: lower-case hardpoint -> caption ids.
        std::unordered_map<std::string, unsigned> InfoIds;
    };

    struct FRules
    {
        std::vector<std::string> EquipmentFiles;
        std::vector<std::string> ShipFiles;
        bool bLoaded = false;
        // equipment hp_subtype values (any section with a nickname), file order, no repeats.
        std::unordered_map<unsigned, std::vector<unsigned>> Equipment;
        std::unordered_map<unsigned, FShipRules> Ships;
    };

    FRules& Rules()
    {
        static FRules Instance;
        return Instance;
    }

    std::string Lower(char const* Text)
    {
        std::string Out(Text != nullptr ? Text : "");
        for (char& Ch : Out)
            Ch = static_cast<char>(tolower(static_cast<unsigned char>(Ch)));
        return Out;
    }

    bool Contains(std::vector<unsigned> const& List, unsigned Value)
    {
        for (unsigned Have : List)
        {
            if (Have == Value)
                return true;
        }
        return false;
    }

    void LoadShips(FRules& R, char const* Path)
    {
        INI_Reader Reader;
        if (!Reader.open(Path, false))
            return;
        while (Reader.read_header())
        {
            if (!Reader.is_header("Ship"))
                continue;
            FShipRules* Ship = nullptr;
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Ship = &R.Ships[CreateID(Reader.get_value_string(0))];
                }
                else if (Ship == nullptr)
                {
                    continue;
                }
                else if (Reader.is_value("hp_subtype"))
                {
                    std::vector<std::string>& List =
                        Ship->Hardpoints[CreateID(Reader.get_value_string(0))];
                    for (unsigned Index = 1; !Reader.is_value_empty(Index); ++Index)
                        List.push_back(Reader.get_value_string(Index));
                }
                else if (Reader.is_value("internal_hp_subtype"))
                {
                    for (unsigned Index = 0; !Reader.is_value_empty(Index); ++Index)
                        Ship->Internal.push_back(CreateID(Reader.get_value_string(Index)));
                }
                else if (Reader.is_value("hp_info"))
                {
                    unsigned const Ids = static_cast<unsigned>(Reader.get_value_int(0));
                    for (unsigned Index = 1; !Reader.is_value_empty(Index); ++Index)
                        Ship->InfoIds[Lower(Reader.get_value_string(Index))] = Ids;
                }
            }
        }
        Reader.close();
    }

    void LoadEquipment(FRules& R, char const* Path)
    {
        INI_Reader Reader;
        if (!Reader.open(Path, false))
            return;
        // Every section; as in FLUF the last nickname carries over until the next one.
        unsigned Id = 0;
        while (Reader.read_header())
        {
            while (Reader.read_value())
            {
                if (Reader.is_value("nickname"))
                {
                    Id = CreateID(Reader.get_value_string(0));
                }
                else if (Reader.is_value("hp_subtype"))
                {
                    // Created on the first subtype only, so a lookup tells "has subtypes".
                    std::vector<unsigned>& Subtypes = R.Equipment[Id];
                    for (unsigned Index = 0; !Reader.is_value_empty(Index); ++Index)
                    {
                        unsigned const Subtype = CreateID(Reader.get_value_string(Index));
                        if (!Contains(Subtypes, Subtype))
                            Subtypes.push_back(Subtype);
                    }
                }
            }
        }
        Reader.close();
    }

    FRules const& Loaded()
    {
        FRules& R = Rules();
        if (!R.bLoaded)
        {
            R.bLoaded = true;
            for (std::string const& Path : R.ShipFiles)
                LoadShips(R, Path.c_str());
            for (std::string const& Path : R.EquipmentFiles)
                LoadEquipment(R, Path.c_str());
        }
        return R;
    }

    std::vector<unsigned> const* EquipmentSubtypes(unsigned ArchId)
    {
        FRules const& R = Loaded();
        std::unordered_map<unsigned, std::vector<unsigned>>::const_iterator It = R.Equipment.find(ArchId);
        return It != R.Equipment.end() ? &It->second : nullptr;
    }

    FShipRules const* ShipRules(unsigned ShipArchId)
    {
        FRules const& R = Loaded();
        std::unordered_map<unsigned, FShipRules>::const_iterator It = R.Ships.find(ShipArchId);
        return It != R.Ships.end() ? &It->second : nullptr;
    }

    // FLUF GetFreeHardpoint: the first of Hardpoints that no mounted item of List sits on (the
    // cargo bay never counts as taken). Names compare exactly, as FLUF's string set does.
    char const* FreeHardpoint(EquipDescList const* List, std::vector<std::string> const& Hardpoints)
    {
        std::set<std::string> Taken;
        for (EquipDesc const& Desc : List->Items)
        {
            if (Desc.bEquipped)
                Taken.insert(Desc.Hardpoint != nullptr ? Desc.Hardpoint : "");
        }
        Taken.erase(EquipDesc::CARGO_BAY_HP_NAME.Value);
        for (std::string const& Hardpoint : Hardpoints)
        {
            if (Taken.find(Hardpoint) == Taken.end())
                return Hardpoint.c_str();
        }
        return nullptr;
    }
}

namespace ExpandedHardpoints
{
    bool Enabled()
    {
        static bool const bEnabled = HdPlugins::Enabled("expanded_hardpoints.dll");
        return bEnabled;
    }

    void NoteDataFile(bool bShips, char const* Path)
    {
        FRules& R = Rules();
        (bShips ? R.ShipFiles : R.EquipmentFiles).push_back(Path);
        R.bLoaded = false;
    }

    bool IsValidInternal(unsigned ShipArchId, unsigned ArchId)
    {
        if (!Enabled())
            return true;
        std::vector<unsigned> const* const Subtypes = EquipmentSubtypes(ArchId);
        if (Subtypes == nullptr)
            return true;
        FShipRules const* const Ship = ShipRules(ShipArchId);
        if (Ship == nullptr)
            return false;
        for (unsigned Internal : Ship->Internal)
        {
            if (Contains(*Subtypes, Internal))
                return true;
        }
        return false;
    }

    bool IsValidItem(unsigned ArchId, unsigned ShipArchId, EquipDescList const* List)
    {
        if (!Enabled())
            return true;
        std::vector<unsigned> const* const Subtypes = EquipmentSubtypes(ArchId);
        if (Subtypes == nullptr)
            return true;
        FShipRules const* const Ship = ShipRules(ShipArchId);
        if (Ship == nullptr)
            return false;

        // The first subtype the ship has a line for decides: with a list, by a free hardpoint.
        for (unsigned Subtype : *Subtypes)
        {
            std::unordered_map<unsigned, std::vector<std::string>>::const_iterator It =
                Ship->Hardpoints.find(Subtype);
            if (It == Ship->Hardpoints.end())
                continue;
            if (List == nullptr)
                return true;
            return FreeHardpoint(List, It->second) != nullptr;
        }

        // Then the ship's internal subtypes.
        for (unsigned Internal : Ship->Internal)
        {
            if (Contains(*Subtypes, Internal))
                return true;
        }
        return false;
    }

    char const* FindSubtypeHardpoint(unsigned ArchId, unsigned ShipArchId, int Count,
                                     char const* (*Name)(void* Context, int Index),
                                     bool (*bFree)(void* Context, int Index), void* Context)
    {
        if (!Enabled())
            return nullptr;
        std::vector<unsigned> const* const Subtypes = EquipmentSubtypes(ArchId);
        if (Subtypes == nullptr)
            return nullptr;
        FShipRules const* const Ship = ShipRules(ShipArchId);
        if (Ship == nullptr || Ship->Hardpoints.empty())
            return nullptr;

        // Every hardpoint of the item's subtypes on this ship...
        std::set<std::string> Allowed;
        for (unsigned Subtype : *Subtypes)
        {
            std::unordered_map<unsigned, std::vector<std::string>>::const_iterator It =
                Ship->Hardpoints.find(Subtype);
            if (It != Ship->Hardpoints.end())
                Allowed.insert(It->second.begin(), It->second.end());
        }

        // ...and the first free one of the model, named as the ship line spells it.
        for (int Index = 0; Index < Count; ++Index)
        {
            if (!bFree(Context, Index))
                continue;
            char const* const HpName = Name(Context, Index);
            if (HpName == nullptr)
                continue;
            for (std::string const& Candidate : Allowed)
            {
                if (_stricmp(Candidate.c_str(), HpName) == 0)
                    return StringAlloc(Candidate.c_str(), false);
            }
        }
        return nullptr;
    }

    unsigned HardpointInfoIds(unsigned ShipArchId, char const* Hardpoint)
    {
        if (!Enabled() || Hardpoint == nullptr)
            return 0;
        FShipRules const* const Ship = ShipRules(ShipArchId);
        if (Ship == nullptr)
            return 0;
        // FLUF looks the hardpoint up as the model names it against lower-cased ship lines.
        std::unordered_map<std::string, unsigned>::const_iterator It =
            Ship->InfoIds.find(Hardpoint);
        return It != Ship->InfoIds.end() ? It->second : 0;
    }

    char const* ServerHardpoint(unsigned ArchId, unsigned ShipArchId, char const* Hardpoint,
                                EquipDescList const* List, bool* bUnequip)
    {
        *bUnequip = false;
        if (!Enabled())
            return Hardpoint;

        // Internal equipment the ship's internal_hp_subtype refuses.
        if (_stricmp(Hardpoint, EquipDesc::CARGO_BAY_HP_NAME.Value) == 0 &&
            !IsValidInternal(ShipArchId, ArchId))
        {
            *bUnequip = true;
            return nullptr;
        }

        std::vector<unsigned> const* const Subtypes = EquipmentSubtypes(ArchId);
        FShipRules const* const Ship = ShipRules(ShipArchId);
        bool const bShipHasSubtypes = Ship != nullptr && !Ship->Hardpoints.empty();
        if (!bShipHasSubtypes)
        {
            // A subtype item on a ship without subtype hardpoints goes into the hold.
            if (Subtypes == nullptr)
                return Hardpoint;
            *bUnequip = true;
            return EquipDesc::CARGO_BAY_HP_NAME.Value;
        }

        if (Subtypes == nullptr)
        {
            // An item without a subtype may not take a hardpoint a subtype reserves.
            for (std::pair<unsigned const, std::vector<std::string>> const& Line : Ship->Hardpoints)
            {
                for (std::string const& Reserved : Line.second)
                {
                    if (_stricmp(Reserved.c_str(), Hardpoint) == 0)
                    {
                        *bUnequip = true;
                        return EquipDesc::CARGO_BAY_HP_NAME.Value;
                    }
                }
            }
            return Hardpoint;
        }

        // A subtype item goes on the first free hardpoint of its first subtype that has one.
        // This also sends internal subtype items (armour) to the hold unequipped: FLUF's server
        // patch only looks at hardpoint lines here.
        for (unsigned Subtype : *Subtypes)
        {
            std::unordered_map<unsigned, std::vector<std::string>>::const_iterator It =
                Ship->Hardpoints.find(Subtype);
            if (It == Ship->Hardpoints.end())
                continue;
            if (char const* const Free = FreeHardpoint(List, It->second))
                return StringAlloc(Free, false);
        }
        *bUnequip = true;
        return EquipDesc::CARGO_BAY_HP_NAME.Value;
    }

    bool IsBuiltIn(unsigned ArchId)
    {
        GoodInfo const* const Good = GoodList::find_by_archetype(ArchId);
        return Good == nullptr || Good->Price == 0.0f;
    }
}
