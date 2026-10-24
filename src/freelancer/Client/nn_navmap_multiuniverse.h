// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements a plugin of Discovery Freelancer (https://discoverygc.com) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Discovery's MultiUniverse.dll, built in: a galaxy map above the nav map's universe view.
//
// DATA/UNIVERSE/multiuniverse.ini names a "pretty map" atlas ([const] prettymap = <3db>, <section>),
// the atlas region and title of the galaxy ("null") and of each sector ("mapping = sectorNN, ...",
// the sector being a pseudo-system of universe.ini), and each sector's systems with their map
// positions ([sector] system = <nickname>, x, y; up to four label = <ids>, x, y). The universe view
// then shows one sector at a time, on its region of the atlas and under its title; the universe
// button, now shown in the universe view too, fades between a sector and the galaxy view, which
// shows the visited sectors as stars joined where their systems' jumps cross. Zooming into a
// sector star opens that sector. Systems no sector lists are not on the universe map.
//
// On when the game folder's DACOM.INI [Libraries] names MultiUniverse.dll and the file parses, as
// the plugin installs its patches only then. The plugin keeps its state in flag bits; the port
// keeps the same states as fields (FNavMapMultiUniverse in the .cpp).
#pragma once

class NeuroNetNavMap;
struct FNavMapItem;
struct FNavMapJumpLink;

namespace Universe
{
    struct ISystem;
}

namespace NavMapMultiUniverse
{
    bool Enabled();

    // NeuroNetNavMap::Create (its hook at 0x00490b3b): the player's sector, the galaxy links.
    void OnCreate(NeuroNetNavMap* Map);
    // LoadMapResources: the atlas instead of NAV_prettymap.3db (0x00490d1d), and the region of
    // the view shown (after 0x00490e74).
    char const* PrettyMapPath(char const* RetailPath);
    void ApplyPrettyMap(NeuroNetNavMap* Map);

    // BuildUniverseItems (0x004915f2): whether this system gets a star (and its sector position).
    bool KeepUniverseSystem(Universe::ISystem* System);
    // BuildJumpLinks (0x0049764d): the galaxy view's sector links; true when it built them.
    bool BuildGalaxyLinks(NeuroNetNavMap* Map);
    // The visit flags BuildJumpLinks tests in a sector view (the plugin: "has an entry").
    unsigned char ConnectionVisitFlags(bool bFound, unsigned char Flags, unsigned char Visit);
    // AddJumpLink (0x0049786d): galaxy links between waypointed sectors are kind 1.
    void MarkGalaxyLink(FNavMapJumpLink* Link);
    // EnterUniverseView (0x0049e639 / 0x0049e867): the shown sector, its title and labels, and the
    // universe button; whether the player marker goes on.
    void PrepareUniverseView(NeuroNetNavMap* Map); // the shown sector and its atlas region
    bool WantPlayerMarker();
    void FinishUniverseView(NeuroNetNavMap* Map);
    // PlayerMapPosition (0x00491c2d): the system whose star the universe view marks.
    unsigned PlayerMarkerSystem(unsigned PlayerSystem);

    // Input and the view switch.
    bool OnUniverseButton(NeuroNetNavMap* Map); // true: a switch of ours started
    bool OnShipButton(NeuroNetNavMap* Map);     // true: handled (galaxy or another sector)
    bool OnZoomIntoStar(NeuroNetNavMap* Map, FNavMapItem* Item); // true: a sector picked
    void OnOpenSystem(Universe::ISystem const* System);           // 0x004940df / 0x0049435b
    bool ShipButtonHidden();                                       // 0x004949a2
    bool FadesOut();                     // the switch-out phase fades the texts out (0x00496057)
    bool OnSwitchEnded(NeuroNetNavMap* Map); // 0x00495ceb: true when it rebuilt the view
    void OnLeaveSystem();                    // 0x00495ceb, out of a system: the player's sector
}
