// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Discovery Freelancer (https://discoverygc.com) in the engine,
// for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Discovery's equipment plugins that change Common's simulation, built into the port. Each is on
// when the game folder's DACOM.INI [Libraries] names its DLL (hd_plugins.h; Discovery's profile
// writes the lines), and its key is read from the equipment files whether or not it is:
//
//   multiCruise.dll  [Engine] cruise_speed = <m/s>      the engine's own cruise speed instead of
//                                                       EngineEquipConsts::CRUISING_SPEED
//   EquipMass.dll    <equipment> mass_mult = <f>        a ship weighs hull * (1 + sum of its
//                                                       equipment's mass_mult) + the mass of
//                                                       its equipment other than cargo
//   MuzzleCone.dll   [Gun] muzzle_cone_override = <deg> the gun's own MUZZLE_CONE_ANGLE; a fire
//                                                       attempt that fails no longer resets the
//                                                       refire timer
//
// The DLLs patch Common in memory, so in single player they apply to every ship, as here.
#pragma once

#include "common_exports.h"

class CShip;

namespace DiscoveryPlugins
{
    // multiCruise.dll
    bool MultiCruiseEnabled();
    void NoteCruiseSpeed(unsigned EngineArchId, float Speed);
    float CruisingSpeed(unsigned EngineArchId); // the engine's, else CRUISING_SPEED

    // EquipMass.dll
    bool EquipMassEnabled();
    void NoteMassMult(unsigned EquipArchId, float Mult);
    // The effective mass of Ship given its base (hull) mass; also sets the physics core's
    // inverse mass to match, as the plugin does.
    float ShipMass(CShip const* Ship, float BaseMass);
    // Callers that rebuild a ship's physics body from get_mass() take the base mass (the plugin
    // exempts them by return address): ProcessPendingSystemSwitch and GameObject::leave_system.
    COMMON_API void BeginBaseMass();
    COMMON_API void EndBaseMass();
    bool BaseMassOnly();

    // MuzzleCone.dll
    bool MuzzleConeEnabled();
    void NoteMuzzleCone(unsigned GunArchId, float Degrees);
    float MuzzleConeAngle(unsigned GunArchId); // the gun's, else MUZZLE_CONE_ANGLE
}
