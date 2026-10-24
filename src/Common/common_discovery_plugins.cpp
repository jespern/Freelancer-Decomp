// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Discovery Freelancer (https://discoverygc.com) in the engine,
// for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Discovery's multiCruise, EquipMass and MuzzleCone plugins (common_discovery_plugins.h); behaviour
// from the DLLs Discovery ships.

#include "common_discovery_plugins.h"

#include "common_archetype.h"
#include "common_cequip.h"
#include "common_cobject.h"
#include "common_goods.h" // EngineEquipConsts
#include "common_ivp_real_object.h"
#include "common_misc.h" // MUZZLE_CONE_ANGLE
#include "hd_plugins.h"
#include "../IVP/ivp_physics/ivp_core.h"

#include <unordered_map>

namespace
{
    std::unordered_map<unsigned, float>& CruiseSpeeds()
    {
        static std::unordered_map<unsigned, float> Table;
        return Table;
    }

    std::unordered_map<unsigned, float>& MassMults()
    {
        static std::unordered_map<unsigned, float> Table;
        return Table;
    }

    std::unordered_map<unsigned, float>& MuzzleCones()
    {
        static std::unordered_map<unsigned, float> Table;
        return Table;
    }

    thread_local int GBaseMassDepth = 0;
}

namespace DiscoveryPlugins
{
    bool MultiCruiseEnabled()
    {
        static bool const bEnabled = HdPlugins::Enabled("multiCruise.dll");
        return bEnabled;
    }

    void NoteCruiseSpeed(unsigned EngineArchId, float Speed)
    {
        CruiseSpeeds()[EngineArchId] = Speed; // a later definition wins
    }

    float CruisingSpeed(unsigned EngineArchId)
    {
        if (MultiCruiseEnabled())
        {
            std::unordered_map<unsigned, float>::const_iterator It = CruiseSpeeds().find(EngineArchId);
            if (It != CruiseSpeeds().end())
                return It->second;
        }
        return EngineEquipConsts::CRUISING_SPEED;
    }

    bool EquipMassEnabled()
    {
        static bool const bEnabled = HdPlugins::Enabled("EquipMass.dll");
        return bEnabled;
    }

    void NoteMassMult(unsigned EquipArchId, float Mult)
    {
        MassMults()[EquipArchId] = Mult;
    }

    float ShipMass(CShip const* Ship, float BaseMass)
    {
        // Every live item but cargo adds its mass; mass_mult values add up (each mounted copy
        // counts).
        float Mult = 1.0f;
        float EquipmentMass = 0.0f;
        CEquipTraverser Walk;
        for (CEquip const* Equip = Ship->EquipManager.Traverse(Walk); Equip != nullptr;
             Equip = Ship->EquipManager.Traverse(Walk))
        {
            if (Equip->Type == EquipTypeCargo)
                continue;
            EquipmentMass += Equip->Arch->Mass;
            std::unordered_map<unsigned, float>::const_iterator It = MassMults().find(Equip->Arch->Nickname.Id);
            if (It != MassMults().end())
                Mult += It->second;
        }
        float const Mass = BaseMass * Mult + EquipmentMass;
        // The plugin writes the core's inverse mass (+0x40) and leaves its mass (+0x20) alone. It
        // writes through whatever register held the core, so a ship without a body would be a
        // stray write; here there is nothing to write then.
        if (Ship->BodyPtr != nullptr)
            Ship->BodyPtr->core->InvMass = 1.0f / Mass;
        return Mass;
    }

    void BeginBaseMass()
    {
        ++GBaseMassDepth;
    }

    void EndBaseMass()
    {
        --GBaseMassDepth;
    }

    bool BaseMassOnly()
    {
        return GBaseMassDepth > 0;
    }

    bool MuzzleConeEnabled()
    {
        static bool const bEnabled = HdPlugins::Enabled("MuzzleCone.dll");
        return bEnabled;
    }

    void NoteMuzzleCone(unsigned GunArchId, float Degrees)
    {
        MuzzleCones()[GunArchId] = Degrees;
    }

    float MuzzleConeAngle(unsigned GunArchId)
    {
        if (MuzzleConeEnabled())
        {
            std::unordered_map<unsigned, float>::const_iterator It = MuzzleCones().find(GunArchId);
            if (It != MuzzleCones().end())
                return It->second;
        }
        return MUZZLE_CONE_ANGLE;
    }
}
