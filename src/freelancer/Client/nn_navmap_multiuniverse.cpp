// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements a plugin of Discovery Freelancer (https://discoverygc.com) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Discovery's MultiUniverse.dll (nn_navmap_multiuniverse.h); behaviour from the DLL Discovery
// ships (version 1.1.2.20).

#include "nn_navmap_multiuniverse.h"

#include "nn_navmap.h"
#include "character_select_equip.h" // GetPlayerStarSystemIdRetail, GetPlayerVisitFlagsRetail
#include "menu_ui_init.h"           // PlayOrRewindUiSoundSlot
#include "player_spawn.h"           // GetPlayerWaypointCountRetail / GetPlayerWaypointRetail

#include "common_id.h"   // CreateID
#include "common_ini.h"  // INI_Reader
#include "common_universe.h"
#include "DALib.h"
#include "engine/engbase.h"
#include "hd_plugins.h"

#include <string>
#include <vector>

namespace
{
    struct FMuLabel
    {
        int Ids = 0;
        float X = 0.0f;
        float Y = 0.0f;
    };

    struct FMuSector
    {
        unsigned MappingId = 0;                       // the sectorNN pseudo-system (0: the galaxy)
        float PosX = 0.0f, PosY = 0.0f, PosZ = 0.0f; // the atlas region: matrix [9..11]
        float ScaleX = 0.0f, ScaleY = 0.0f;          // and [0] / [4]
        int Ids = 0;                                  // the title
        FMuLabel Labels[4];                           // over the four faction labels
        std::vector<unsigned> Links;                  // the sectors its systems jump to
    };

    struct FMuSystem
    {
        unsigned Id;
        float X, Y;
        int Sector;
    };

    enum class EMuPending
    {
        None,
        Toggle,   // sector <-> galaxy at the end of the switch
        Resector, // the player's own sector at the end of the switch
    };

    struct FNavMapMultiUniverse
    {
        bool bParsed = false;
        bool bOk = false;
        std::string PrettyPath;
        std::vector<FMuSector> Sectors; // [0] the galaxy ("null"), then the "mapping" lines
        std::vector<FMuSystem> Systems;
        bool bLinksBuilt = false;

        bool bGalaxy = false;   // the universe view shows the galaxy, not a sector
        int ViewSector = 0;     // the sector the universe view shows (0: not chosen)
        int CurSector = 0;      // the player's
        int ShownSector = 0;    // the one the last build used
        int PickedSector = 0;   // chosen from a galaxy star
        EMuPending Pending = EMuPending::None;
        bool bFadingOut = false;
    };

    FNavMapMultiUniverse& State()
    {
        static FNavMapMultiUniverse Instance;
        return Instance;
    }

    // The plugin's FUN_00405f68, in its order: [const] prettymap first, then the map section with
    // its "null" line, then the [sector] sections. Anything else, and it patches nothing.
    void Parse(FNavMapMultiUniverse& S)
    {
        S.bParsed = true;
        INI_Reader Reader;
        if (!Reader.open("..\\data\\universe\\multiuniverse.ini", false))
            return;
        if (!Reader.read_header() || !Reader.is_header("const") || !Reader.read_value() ||
            !Reader.is_value("prettymap"))
        {
            Reader.close();
            return;
        }
        S.PrettyPath = std::string("Interface\\NeuroNet\\NavMap\\NewNavMap\\") + Reader.get_value_string(0);
        std::string const MapSection = Reader.get_value_string(1);

        bool bFound = false;
        while (Reader.read_header())
        {
            if (!Reader.is_header(MapSection.c_str()) || !Reader.read_value() || !Reader.is_value("null"))
                continue;
            FMuSector Galaxy;
            Galaxy.PosX = Reader.get_value_float(0);
            Galaxy.PosY = Reader.get_value_float(1);
            Galaxy.PosZ = Reader.get_value_float(2);
            Galaxy.ScaleX = Reader.get_value_float(3);
            Galaxy.ScaleY = Reader.get_value_float(4);
            Galaxy.Ids = Reader.get_value_int(5);
            S.Sectors.push_back(Galaxy);
            while (Reader.read_value())
            {
                if (!Reader.is_value("mapping"))
                    continue;
                FMuSector Sector;
                Sector.MappingId = CreateID(Reader.get_value_string(0));
                Sector.PosX = Reader.get_value_float(1);
                Sector.PosY = Reader.get_value_float(2);
                Sector.PosZ = Reader.get_value_float(3);
                Sector.ScaleX = Reader.get_value_float(4);
                Sector.ScaleY = Reader.get_value_float(5);
                Sector.Ids = Reader.get_value_int(6);
                S.Sectors.push_back(Sector);
            }
            bFound = true;
            break;
        }
        if (!bFound)
        {
            Reader.close();
            return;
        }

        while (Reader.read_header())
        {
            if (!Reader.is_header("sector") || !Reader.read_value() || !Reader.is_value("mapping"))
                continue;
            unsigned const Mapping = CreateID(Reader.get_value_string(0));
            int Index = 0;
            for (int Candidate = static_cast<int>(S.Sectors.size()) - 1; Candidate >= 1; --Candidate)
            {
                if (S.Sectors[Candidate].MappingId == Mapping)
                {
                    Index = Candidate;
                    break;
                }
            }
            if (Index == 0)
                continue;
            int Labels = 0;
            while (Reader.read_value())
            {
                if (Reader.is_value("system"))
                {
                    S.Systems.push_back({CreateID(Reader.get_value_string(0)), Reader.get_value_float(1),
                                         Reader.get_value_float(2), Index});
                }
                else if (Reader.is_value("label") && Labels < 4)
                {
                    FMuLabel& Label = S.Sectors[Index].Labels[Labels++];
                    Label.Ids = Reader.get_value_int(0);
                    Label.X = Reader.get_value_float(1);
                    Label.Y = Reader.get_value_float(2);
                }
            }
        }
        Reader.close();
        S.bOk = true;
    }

    FNavMapMultiUniverse& Loaded()
    {
        FNavMapMultiUniverse& S = State();
        if (!S.bParsed)
            Parse(S);
        return S;
    }

    // FUN_00405570: the last listing of the system wins.
    FMuSystem const* FindSystem(unsigned Id)
    {
        FNavMapMultiUniverse const& S = State();
        for (size_t Index = S.Systems.size(); Index-- > 0;)
        {
            if (S.Systems[Index].Id == Id)
                return &S.Systems[Index];
        }
        return nullptr;
    }

    int SectorOfSystem(unsigned Id)
    {
        FMuSystem const* const System = FindSystem(Id);
        return System != nullptr ? System->Sector : 0;
    }

    // FUN_004055a0: never the galaxy record.
    int SectorOfMapping(unsigned Id)
    {
        FNavMapMultiUniverse const& S = State();
        for (int Index = static_cast<int>(S.Sectors.size()) - 1; Index >= 1; --Index)
        {
            if (S.Sectors[Index].MappingId == Id)
                return Index;
        }
        return 0;
    }

    // A sector counts as visited when the player has a visit entry for one of its systems (the
    // plugin marks it in the sector views' builds, which always come first).
    bool SectorVisited(int Sector)
    {
        for (FMuSystem const& System : State().Systems)
        {
            if (System.Sector != Sector)
                continue;
            unsigned char Flags = 0;
            if (GetPlayerVisitFlagsRetail(System.Id, &Flags, 0, 0))
                return true;
        }
        return false;
    }

    // FUN_004055f8, with its two loop bounds (the last listed system and each system's last
    // connection are not looked at).
    void BuildSectorLinks(FNavMapMultiUniverse& S)
    {
        S.bLinksBuilt = true;
        for (size_t Index = 0; Index + 1 < S.Systems.size(); ++Index)
        {
            Universe::ISystem const* const System = Universe::get_system(S.Systems[Index].Id);
            if (System == nullptr)
                continue;
            int const From = SectorOfSystem(System->GetId());
            if (From == 0)
                continue;
            int const Count = System->GetConnectionCount();
            for (int Connection = 0; Connection + 1 < Count; ++Connection)
            {
                Universe::FSystemConnection Record;
                System->GetConnection(static_cast<unsigned>(Connection), &Record);
                int const To = SectorOfSystem(Record.GotoSystemId);
                if (To == 0 || To == From)
                    continue;
                std::vector<unsigned>& Links = S.Sectors[From].Links;
                unsigned const Target = S.Sectors[To].MappingId;
                bool bKnown = false;
                for (unsigned const Link : Links)
                    bKnown = bKnown || Link == Target;
                if (!bKnown)
                    Links.push_back(Target);
            }
        }
    }

    void StartSwitch(NeuroNetNavMap* Map)
    {
        Map->bViewSwitch = 1;
        Map->bTransition = 1;
        Map->TransitionView = 0;
        Map->TransitionTime = 0.0f;
        Map->TransitionDuration = 0.4f;
    }
}

namespace NavMapMultiUniverse
{
    bool Enabled()
    {
        static bool const bEnabled = HdPlugins::Enabled("MultiUniverse.dll") && Loaded().bOk;
        return bEnabled;
    }

    void OnCreate(NeuroNetNavMap* Map)
    {
        (void)Map;
        if (!Enabled())
            return;
        FNavMapMultiUniverse& S = State();
        S.CurSector = SectorOfSystem(GetPlayerStarSystemIdRetail());
        S.ViewSector = S.CurSector;
        S.bGalaxy = false;
        S.PickedSector = 0;
        S.Pending = EMuPending::None;
        S.bFadingOut = false;
        if (!S.bLinksBuilt)
            BuildSectorLinks(S);
    }

    char const* PrettyMapPath(char const* RetailPath)
    {
        return Enabled() ? State().PrettyPath.c_str() : RetailPath;
    }

    void ApplyPrettyMap(NeuroNetNavMap* Map)
    {
        if (!Enabled())
            return;
        FNavMapMultiUniverse const& S = State();
        FMuSector const& Sector = S.Sectors[static_cast<size_t>(S.ShownSector)];
        float* const Matrix = DALib::Engine->GetInstanceTransform(Map->PrettyMapMesh.EngineInstance);
        if (Matrix == nullptr)
            return;
        Matrix[9] = Sector.PosX;
        Matrix[10] = Sector.PosY;
        Matrix[11] = Sector.PosZ;
        Matrix[0] = Sector.ScaleX;
        Matrix[4] = Sector.ScaleY;
        DALib::Engine->UpdateInstance(Map->PrettyMapMesh.EngineInstance, 0, nullptr);
    }

    void PrepareUniverseView(NeuroNetNavMap* Map)
    {
        if (!Enabled())
            return;
        FNavMapMultiUniverse& S = State();
        if (S.bGalaxy)
            S.ViewSector = 0;
        else if (S.ViewSector == 0)
            S.ViewSector = S.CurSector;
        S.ShownSector = S.bGalaxy ? 0 : S.ViewSector;
        ApplyPrettyMap(Map);
    }

    bool KeepUniverseSystem(Universe::ISystem* System)
    {
        if (!Enabled())
            return true;
        FNavMapMultiUniverse const& S = State();
        unsigned const Id = System->GetId();
        if (S.bGalaxy)
        {
            int const Sector = SectorOfMapping(Id);
            return Sector != 0 && SectorVisited(Sector);
        }
        FMuSystem const* const Listed = FindSystem(Id);
        if (Listed == nullptr || Listed->Sector != S.ShownSector)
            return false;
        // The plugin writes the sector position into the system for good (+0x74 / +0x78).
        System->Pos.x = Listed->X;
        System->Pos.y = Listed->Y;
        return true;
    }

    bool BuildGalaxyLinks(NeuroNetNavMap* Map)
    {
        if (!Enabled() || !State().bGalaxy)
            return false;
        Map->JumpLinks.clear();
        for (FNavMapItem* const From : Map->Items)
        {
            if (From->bShown == 0 || (From->Type & 4) == 0 || From->System == nullptr)
                continue;
            int const Sector = SectorOfMapping(From->System->GetId());
            if (Sector == 0)
                continue;
            for (unsigned const Target : State().Sectors[static_cast<size_t>(Sector)].Links)
            {
                if (!SectorVisited(SectorOfMapping(Target)))
                    continue;
                for (FNavMapItem* const To : Map->Items)
                {
                    if (To->bShown == 0 || (To->Type & 4) == 0 || To->System == nullptr)
                        continue;
                    if (To->System->GetId() == Target)
                        Map->AddJumpLink(From, To);
                }
            }
        }
        return true;
    }

    unsigned char ConnectionVisitFlags(bool bFound, unsigned char Flags, unsigned char Visit)
    {
        if (!Enabled())
            return static_cast<unsigned char>(Flags | Visit);
        return static_cast<unsigned char>((bFound ? 1 : 0) | Visit);
    }

    void MarkGalaxyLink(FNavMapJumpLink* Link)
    {
        if (!Enabled() || !State().bGalaxy || Link->From->System == nullptr || Link->To->System == nullptr)
            return;
        int const A = SectorOfMapping(Link->From->System->GetId());
        int const B = SectorOfMapping(Link->To->System->GetId());
        bool bA = false;
        bool bB = false;
        int const Count = GetPlayerWaypointCountRetail();
        for (int Index = 0; Index < Count; ++Index)
        {
            FPlayerWaypointRecord const* const Waypoint = GetPlayerWaypointRetail(Index);
            if (Waypoint == nullptr)
                continue;
            int const Sector = SectorOfSystem(Waypoint->SystemId);
            bA = bA || Sector == A;
            bB = bB || Sector == B;
        }
        if (bA && bB)
            Link->Kind = 1;
    }

    bool WantPlayerMarker()
    {
        if (!Enabled())
            return true;
        FNavMapMultiUniverse const& S = State();
        return S.bGalaxy || S.ShownSector == S.CurSector;
    }

    void FinishUniverseView(NeuroNetNavMap* Map)
    {
        if (!Enabled())
            return;
        FMuSector const& Sector = State().Sectors[static_cast<size_t>(State().ShownSector)];
        Map->SystemNameText->SetProperty(0x1e, static_cast<uintptr_t>(Sector.Ids), 0);
        for (int Index = 3; Index >= 0; --Index)
        {
            WinControl* const Label = Map->FactionLabels[Index];
            Label->Offset[0] = Sector.Labels[Index].X;
            Label->Offset[1] = Sector.Labels[Index].Y;
            Label->SetProperty(0x1e, static_cast<uintptr_t>(Sector.Labels[Index].Ids), 0);
        }
        // The universe button stays on the universe view: it switches sector <-> galaxy.
        Map->UniverseButton->SetProperty(1, 0, 0);
    }

    unsigned PlayerMarkerSystem(unsigned PlayerSystem)
    {
        if (!Enabled() || !State().bGalaxy)
            return PlayerSystem;
        return State().Sectors[static_cast<size_t>(State().CurSector)].MappingId;
    }

    bool OnUniverseButton(NeuroNetNavMap* Map)
    {
        if (!Enabled() || Map->ViewMode != 0)
            return false;
        if (Map->bTransition != 0)
            return true;
        FNavMapMultiUniverse& S = State();
        S.Pending = EMuPending::Toggle;
        S.bFadingOut = true;
        Map->HideObjectTools();
        StartSwitch(Map);
        PlayOrRewindUiSoundSlot(8);
        return true;
    }

    bool OnShipButton(NeuroNetNavMap* Map)
    {
        if (!Enabled())
            return false;
        FNavMapMultiUniverse& S = State();
        if (S.bGalaxy && Map->ViewMode == 0)
            return true; // hidden, and ignored
        S.CurSector = SectorOfSystem(GetPlayerStarSystemIdRetail());
        if (Map->ViewMode != 0 || S.ViewSector == S.CurSector)
        {
            S.ViewSector = S.CurSector;
            return false; // retail: the player's system opens
        }
        if (Map->bTransition != 0)
            return true;
        S.ViewSector = S.CurSector;
        S.Pending = EMuPending::Resector;
        S.bFadingOut = true;
        StartSwitch(Map);
        PlayOrRewindUiSoundSlot(8);
        return true;
    }

    bool OnZoomIntoStar(NeuroNetNavMap* Map, FNavMapItem* Item)
    {
        if (!Enabled() || !State().bGalaxy || Map->ViewMode != 0 || Item == nullptr ||
            Item->System == nullptr)
            return false;
        int const Sector = SectorOfMapping(Item->System->GetId());
        if (Sector == 0)
            return true;
        FNavMapMultiUniverse& S = State();
        S.PickedSector = Sector;
        S.Pending = EMuPending::Toggle;
        S.bFadingOut = true;
        StartSwitch(Map);
        PlayOrRewindUiSoundSlot(8);
        return true;
    }

    void OnOpenSystem(Universe::ISystem const* System)
    {
        if (!Enabled() || System == nullptr)
            return;
        State().ViewSector = SectorOfSystem(System->GetId());
    }

    bool ShipButtonHidden()
    {
        return Enabled() && State().bGalaxy;
    }

    bool FadesOut()
    {
        return Enabled() && State().bFadingOut;
    }

    bool OnSwitchEnded(NeuroNetNavMap* Map)
    {
        if (!Enabled())
            return false;
        FNavMapMultiUniverse& S = State();
        if (S.Pending == EMuPending::None)
            return false;
        if (S.Pending == EMuPending::Toggle)
        {
            if (S.bGalaxy)
            {
                S.bGalaxy = false;
                S.ViewSector = S.PickedSector != 0 ? S.PickedSector : S.CurSector;
            }
            else
            {
                S.bGalaxy = true;
            }
        }
        S.PickedSector = 0;
        S.Pending = EMuPending::None;
        S.bFadingOut = false;
        S.CurSector = SectorOfSystem(GetPlayerStarSystemIdRetail());
        Map->ZoomScale = 1.0f;
        Map->EnterUniverseView();
        StartSwitch(Map);
        return true;
    }

    void OnLeaveSystem()
    {
        if (!Enabled())
            return;
        State().CurSector = SectorOfSystem(GetPlayerStarSystemIdRetail());
    }
}
