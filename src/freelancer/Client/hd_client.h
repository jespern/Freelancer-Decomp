// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Freelancer: HD Edition plugins in the client that need more than a branch in one function
// (freelancer-mac's docs/mods.md, src/include/hd_plugins.h).
#pragma once

class Win3D;

// HD: WheelScroll.dll -- the wheel over a window scrolls its list: the scrollbar of the first
// window, from Target up its parents, that owns one (the windows WheelScroll knows). True when
// it scrolled. Win3D's wheel event falls back to it when the window itself does not take it.
bool HdWheelScroll(Win3D* Target, int Wheel);

namespace Universe
{
    struct IZone;
}
struct Vector;

// HD: Zone.dll -- "ENTERED <zone>." / "LEFT <zone>." arrival text when the player crosses named
// zones, coloured by danger; its strings are Zone.dll's own string table (copied to bin/).
// Registered after freelancer.ini's [Resources] (0x005b1cb3).
void HdZoneRegisterResources();
// Each zone of the once-a-second zone scan (0x0051ce64), before its visit test.
void HdZoneObserve(Universe::IZone const* Zone, Vector const& PlayerPos, unsigned Index, unsigned Count);
// A system change forgets the zones the player was in (0x00554687).
void HdZoneReset();
// Zone and Territory let the arrival caption take several lines (0x0045ac3e).
bool HdArrivalTextMultiLine();

// HD: HudShift.dll's DATA\INTERFACE\HudShift.ini: "position = xAddr, x, yAddr, y" in [Group]
// sections give the base value of the float at a retail address. The port takes only those
// base values; the horizontal move to the screen edges stays hud_widescreen's shear. Retail
// when HudShift is off or the address is not listed.
float HdHudShiftBase(unsigned RetailAddress, float Retail);

// HD: VersionText.dll -- the menus' version caption becomes clickable: Shift-click cycles its
// colour (and the main menu buttons') through VersionText.ini [Colors], Ctrl-click plays a
// [Sounds] line. Answers the window class to create ("TextControl" when off) and the caption
// box height the plugin forces (0.03; Retail when off).
char const* HdVersionTextClass();
float HdVersionTextHeight(float Retail);

struct FTurretCamera;

// HD: TurretZoom.dll -- in turret view the wheel moves the turret camera (tether z, or y after
// Shift+Alt+wheel) instead of the throttle, by factors or, with modifiers, fixed steps, and
// slows the turret's turn speed as the camera comes closer. Options from its [Libraries] line.
bool HdTurretZoomEnabled();
// The wheel in turret view (0x004d845c): true, it took the event.
void HdTurretZoomWheel(int Wheel, unsigned Modifiers);
// The turret view toggle (0x0054a880), before it flips: saves the camera on entering, restores
// it on leaving.
void HdTurretZoomToggle(FTurretCamera* Cam, bool bEntering);
// The turret camera update (0x005dbd30): eases the tether towards the wheel's target.
void HdTurretZoomEase(FTurretCamera* Cam, float DeltaSeconds);
