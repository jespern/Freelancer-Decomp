// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

// Retail Client/Space/JumpEffect.cpp — Nebula renderable (operator_new 0x2d0).
// See docs/re/freelancer.md (Space section).
//
// Layout notes (trees):
//   +0x04  VC6 tree nodes 0x5c (FUN_00509c30) — root@+0x08, head@+0x0c, count@+0x14
//   +0x38  zone_shell map (FUN_004e7f60, VC6 nodes 0x18) — a real std::map in the port
//   +0x270 GameLight* ambient (FUN_00537a90), driven by FUN_005097c0

#include "common_net.h"
#include "common_universe.h"
#include "texture_shapes.h"

#include <cstddef>
#include <map>

// FUN_00550180 — Clouds subobject (SpaceTerrainRenderComp), sizeof 0x12c.
// Also allocated by TexturePanels::Load for nested [Clouds] / Exterior panels.
//
// The code works in retail offsets (`Clouds->Raw + 0x..`). Retail keeps pointers in five of its
// dwords (+0x00 puff table, +0x18 four lightning shapes, +0x28 eight puff shapes, +0xc4 zone);
// those cannot hold LP64 pointers, so Raw maps them onto native members and every other offset
// onto the retail byte image.
struct FTexShape;
struct FNebulaClouds
{
    struct FRaw
    {
        unsigned char Bytes[0x12c];
        unsigned char* operator+(int RetailOffset);
        unsigned char& operator[](int RetailOffset) { return *(*this + RetailOffset); }
    };
    FRaw Raw;                         // must stay first: FRaw finds its owner through it
    void** PuffTable;                 // retail +0x00
    FTexShape* LightningShapes[4];    // retail +0x18
    FTexShape* PuffShapes[8];         // retail +0x28
    Universe::IZone* Zone;            // retail +0xc4
};

static_assert(offsetof(FNebulaClouds, Raw) == 0, "Clouds raw image first");

// DAT_00679860 -- SpaceTerrainRenderComp.cpp's lazily filled I3DMathEngine (FUN_005b73e0 on
// first use). Readers: FUN_0054c8b0 (0x0054cb06, the band rings in cli_asteroid.cpp),
// FUN_00550d40 (0x00550e2f, 0x0055123e) and FUN_00551500 (0x0055156e, 0x005519f3, 0x00551b94).
struct I3DMathEngine;
extern I3DMathEngine* G_CloudsMath;

void NebulaCloudsConstruct(FNebulaClouds* Clouds);                 // FUN_00550180
void NebulaCloudsParseIni(FNebulaClouds* Clouds, class INI_Reader& Reader,
                          FShapeDB* Shapes); // FUN_00550240
void NebulaCloudsReleaseBits(FNebulaClouds* Clouds);               // FUN_005501e0 / 00550cf0
void NebulaCloudsRebuildBits(FNebulaClouds* Clouds);               // FUN_00550b60 (+ FUN_00550040)
void NebulaCloudsApplyDetail(FNebulaClouds* Clouds, float Detail); // FUN_005524a0
void NebulaCloudsRegenExcluded(FNebulaClouds* Clouds, void* ExclusionCtx); // FUN_005513b0
// FUN_00550d40 (__thiscall, RET 0x10) — per-frame puff update: fade, cull, sort, respawn,
// drift, move; active count at +0x14. Camera is an ICamera (vt+0x0c / vt+0x10).
void NebulaCloudsUpdate(FNebulaClouds* Clouds, void* Camera, float Dt, float Visibility,
                        Exclusion::NebulaExclusionZoneMgr* ExclusionMgr);
void NebulaCloudsDestroy(FNebulaClouds* Clouds, unsigned char Flags);

struct FNebulaCloudsListNode
{
    FNebulaCloudsListNode* Next;
    FNebulaCloudsListNode* Prev;
    FNebulaClouds* Clouds;
};

struct FNebulaZoneShell;
typedef std::map<unsigned, FNebulaZoneShell*> FNebulaZoneShellMap;

struct FNebulaEffectCompanion
{
    FNebulaEffectCompanion();
    ~FNebulaEffectCompanion();
    void* Values[4];
};

struct FNebulaCloudsListStorage
{
    FNebulaCloudsListStorage();
    ~FNebulaCloudsListStorage();
    unsigned Allocator; // Intentionally unwritten, as in the existing list constructor.
    FNebulaCloudsListNode* Sentinel;
    unsigned Count;
};
static_assert(sizeof(FNebulaCloudsListStorage) == 0xc, "Nebula clouds list size");

struct INebula
{
    virtual void Load(char const* FilePath, char const* ZoneName) = 0;
    virtual void Shutdown() = 0;
    virtual void Update(void* Camera, float Dt) = 0;
    virtual void DrawFogOverlay(void* Camera) = 0;
    virtual void DrawFogShell(void* Camera) = 0;
    virtual void DrawBoltsPass(void* Camera) = 0;
    virtual void DrawExclusionShell(void* Camera) = 0;
    virtual void DrawCloudsPanels(void* Camera) = 0;
    virtual float ZoneExteriorFactor(float const* CamPos3) = 0;
    virtual float GetFloat294() = 0;
    virtual float GetFloat2C4() = 0;
    virtual float GetFloat2C8() = 0;
    virtual float* CopyFogRgb(float* OutRgb3) = 0;
    virtual float* FillFogRgb(float* OutRgb3) = 0;
    virtual float GetFloat2AC() = 0;
    virtual float FogFactor(float const* CamPos3) = 0;
    virtual float GetFloat2B0() = 0;
    virtual float FogFarClamp(float Unused) = 0;
    virtual unsigned char const* GetAmbientBytes() = 0;
    virtual Universe::IZone* GetZone() = 0;
    virtual float GetZoneCenterRadius(float* OutPos3) = 0;
    virtual void Detail(float DetailLevel) = 0;
    virtual ~INebula() {}
};

struct FNebulaEffect : INebula
{
    FNebulaEffect();
    void Load(char const* FilePath, char const* ZoneName) override;
    void Shutdown() override;
    void Update(void* Camera, float Dt) override;
    void DrawFogOverlay(void* Camera) override;
    void DrawFogShell(void* Camera) override;
    void DrawBoltsPass(void* Camera) override;
    void DrawExclusionShell(void* Camera) override;
    void DrawCloudsPanels(void* Camera) override;
    float ZoneExteriorFactor(float const* CamPos3) override;
    float GetFloat294() override;
    float GetFloat2C4() override;
    float GetFloat2C8() override;
    float* CopyFogRgb(float* OutRgb3) override;
    float* FillFogRgb(float* OutRgb3) override;
    float GetFloat2AC() override;
    float FogFactor(float const* CamPos3) override;
    float GetFloat2B0() override;
    float FogFarClamp(float Unused) override;
    unsigned char const* GetAmbientBytes() override;
    Universe::IZone* GetZone() override;
    float GetZoneCenterRadius(float* OutPos3) override;
    void Detail(float DetailLevel) override;
    ~FNebulaEffect() override;

    // The members retail keeps at fixed offsets. cli_nebula.cpp reads the object in retail
    // offsets through FNebulaBytes (NebulaRetailAt), which maps these offsets onto the members
    // and every other offset onto Image -- retail's 0x2d0 bytes, of which only the unnamed
    // scalars are used. The pointers retail keeps in dwords get pointer-sized members here.
    FShapeDB Shapes;                                 // +0x04
    float FrameDelta;                                // +0x1c
    Universe::IZone* Zone;                            // +0x20
    Exclusion::NebulaExclusionZone* Field24;          // +0x24 the exclusion zone the camera is in
    unsigned Field28;                                // +0x28 immersion (float bits)
    unsigned char Flag2C;                            // +0x2c
    FNebulaZoneShell* Field30;                        // +0x30 that zone's shell
    Exclusion::NebulaExclusionZoneMgr* ExclusionMgr;  // +0x34
    FNebulaZoneShellMap ZoneShells;                  // +0x38
    FTexShape* FogShape;                              // +0x5c
    FTexShape* BoltShapes[8];                         // +0x60
    float* BoltSegments;                              // +0xe0
    FNebulaEffectCompanion Companion;                // +0xe4
    FNebulaCloudsListStorage Clouds;                  // +0xf4
    Universe::IZone* Zone248;                         // +0x248
    struct FGameLight* AmbientLight;                  // +0x270
    float FogDistance;                                // +0x298
    float FogNear;                                    // +0x29c
    unsigned char FlagLoaded;                         // +0x2c0
    unsigned char Image[0x2d0];
};

// The native home of retail offset Off of a nebula (cli_nebula.cpp).
unsigned char* NebulaRetailAt(FNebulaEffect* Nebula, int Off);
struct FNebulaBytes
{
    FNebulaEffect* Nebula;
    unsigned char* operator+(int Off) const { return NebulaRetailAt(Nebula, Off); }
    unsigned char& operator[](int Off) const { return *NebulaRetailAt(Nebula, Off); }
};


FNebulaEffect* NebulaConstruct(FNebulaEffect* This);
FNebulaEffect* NebulaCreate();
void NebulaDestroy(FNebulaEffect* Nebula);

// LAB_00509ac0
void NebulaSetDetail(FNebulaEffect* Nebula, float Detail);

// FUN_00506600
void NebulaRebuild(FNebulaEffect* Nebula);

// FUN_00506cc0
void NebulaShutdown(FNebulaEffect* Nebula);

// LAB_00506f20 — vtable +0x08 (RET 8). Arg1 = camera ([ESP+0x3c] after the prologue, its
// vt+0xc gives the position); arg2 = frame dt, stored to +0x1c (506f20 MOV EAX,[ESP+8]).
// Caller: StarSysUpdate FUN_00554eb0 (PUSH dt; PUSH cam; CALL [EDX+8]).
void NebulaUpdate(FNebulaEffect* Nebula, void* Camera, float Dt);

// FUN_00507340 — bolt segment basis re-orient
void NebulaOrientBolts(FNebulaEffect* Nebula, float Tx, float Ty, float Tz);

// FUN_005074e0 — exclusion-shell fog frustum draw (+0x5c FTexShape*)
void NebulaUpdateBoltCamera(FNebulaEffect* Nebula, float CamX, float CamY, float CamZ);

// FUN_00507ed0 — bolt draw (Win3D quads + Pipeline RS 0x18)
void NebulaDrawBolts(FNebulaEffect* Nebula, void* Camera);

// FUN_00504cf0 — full object destructor body
void NebulaDestruct(FNebulaEffect* Nebula);

// FUN_00509b80 — PerfOptions "Nebula" apply: set global detail, walk StarSys list.
void NebulaApplyGlobalDetail(float Detail);

// FUN_00422440 (cdecl) — fog colour from float RGB (x255, clamped) and the raw float bits of
// FogStart / FogEnd into D3DRS 0x22 / 0x24 / 0x25, fog modes by pipeline state 0xd, FOGENABLE.
void FogApplyColorAndRange(float const* Rgb3, float FogStart, float FogEnd);
