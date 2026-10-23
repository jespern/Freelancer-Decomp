// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Freelancer: HD Edition plugins in the client (hd_client.h).

#include "hd_client.h"

#include "chat_control.h"
#include "commodity_by_base.h"
#include "hd_plugins.h"
#include "hud_contact_list.h"
#include "hud_status_core.h"
#include "nn_inventory.h"
#include "nn_load_save.h"
#include "nn_mission_vendor.h"
#include "nn_navmap.h"
#include "nn_news.h"
#include "nn_player_info.h"
#include "nn_preferences.h"
#include "nn_repair.h"
#include "page_character_select_single.h"
#include "page_server_select.h"
#include "rich_text_win.h"
#include "ui_fast_table.h"
#include "ui_scroll.h"
#include "character_select_equip.h"
#include "common_ini.h"
#include "common_text.h"
#include "common_universe.h"
#include "ids_strings.h"
#include "misc_util.h"
#include "nn_arrival_text.h"
#include "nn_modal.h"
#include "nn_text_control.h"
#include "nn_text_scratch.h"
#include "common_id.h"
#include "hud_command_dispatch.h"
#include "menu_sound_arch.h"
#include "star_sys.h"
#include "type_select.h"
#include "voice_profiles.h"
#include "space_app.h"
#include "text_win.h"
#include "globals.h"
#include "win_factory.h"
#include "space_camera_turret.h"
#include "win3d.h"

#include <stdio.h>
#include <algorithm>
#include <math.h>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <string>
#include <stdlib.h>
#include <windows.h>
#include <string.h>

namespace
{
    // WheelScroll's two tables: by class (+0x10) the scrollbar member, by window name (+0x18)
    // its parent's. The port's typed members stand for the retail offsets.
    UiScroll* ScrollOf(Win3D* Window)
    {
        if (UiScroll* const Scroll = dynamic_cast<UiScroll*>(Window))
            return Scroll;
        if (HUD_ContactList* const W = dynamic_cast<HUD_ContactList*>(Window))
            return W->Scroll;                // +0x3d4
        if (HUD_Status* const W = dynamic_cast<HUD_Status*>(Window))
            return W->WeaponListScroll;      // +0x58c
        if (NnCommodityByBase* const W = dynamic_cast<NnCommodityByBase*>(Window))
            return W->Scroll;                // +0x348
        if (NnInventory* const W = dynamic_cast<NnInventory*>(Window))
            return static_cast<UiScroll*>(W->InventoryScroll); // NN_Dealer / NN_Inventory / NN_Scanner +0x3d0
        if (NnLoadSave* const W = dynamic_cast<NnLoadSave*>(Window))
            return W->Scroll;                // +0x1758
        if (NnMissionVendor* const W = dynamic_cast<NnMissionVendor*>(Window))
            return W->SummaryScroll;         // +0x428
        if (NnNews* const W = dynamic_cast<NnNews*>(Window))
            return W->Scroll;                // +0x648
        if (NnPlayerInfo* const W = dynamic_cast<NnPlayerInfo*>(Window))
            return W->Scroll;                // +0x394
        if (NnPreferences* const W = dynamic_cast<NnPreferences*>(Window))
            return W->KeyScroll;             // +0x4b8
        if (NnRepair* const W = dynamic_cast<NnRepair*>(Window))
            return W->Scroll;                // +0x46c
        if (UiScrollTable* const W = dynamic_cast<UiScrollTable*>(Window))
            return W->ScrollBar;             // the chat history and other tables
        if (RichTextWin* const W = dynamic_cast<RichTextWin*>(Window))
            return W->ScrollBarV;            // RichTextWin / UIRichTextWin +0x3f0
        if (NeuroNetNavMap* const W = dynamic_cast<NeuroNetNavMap*>(Window))
            return W->ObjectiveScroll;       // +0xae0

        Win3D* const Parent = Window->GetParent();
        char const* const Name = Window->GetName();
        if (Parent == nullptr || Name == nullptr)
            return nullptr;
        if (_stricmp(Name, "CharSelDesc") == 0 || _stricmp(Name, "CharSelTime") == 0)
        {
            if (PageCharacterSelectSingle* const Page = dynamic_cast<PageCharacterSelectSingle*>(Parent))
                return Page->CharScroll;     // +0xd4
        }
        else if (_stricmp(Name, "ServerTable") == 0)
        {
            if (PageServerSelect* const Page = dynamic_cast<PageServerSelect*>(Parent))
                return Page->ServerScroll;   // +0xc8
        }
        else if (_stricmp(Name, "NavMapBase") == 0 || _stricmp(Name, "NavMapSystemTitleText") == 0)
        {
            for (Win3D* Up = Parent; Up != nullptr; Up = Up->GetParent())
                if (NeuroNetNavMap* const Map = dynamic_cast<NeuroNetNavMap*>(Up))
                    return Map->BaseListScroll; // +0xfc4
        }
        return nullptr;
    }
}

namespace
{
    // TurretZoom 1.41 (adoxa, TurretZoom.c), with its option defaults.
    struct FTurretZoom
    {
        float Max = 200000.0f;
        float Shift = 10000.0f;
        float Ctrl = 1000.0f;
        float Alt = 100.0f;
        float ShiftCtrl = 10.0f;
        float CtrlAlt = 1.0f;
        float FactorSmall = 1.1f;
        float FactorLarge = 1.5f;
        float FactorSwitch = 5000.0f;
        float SpeedAdjust = 0.5f;
        int SpeedInterval = 10000;
        bool bSmooth = true;
        bool bInverse = true;      // the "inverse" option turns it off

        float Target[2] = {0.0f, 0.0f};   // tether y, z the camera moves to
        float SavedY = 0.0f, SavedZ = 0.0f, SavedYaw = 0.0f, SavedPitch = 0.0f;
        int Speed = 0;
        int Axis = 1;                     // 0 = y, 1 = z

        FTurretZoom()
        {
            char const* const Dll = "TurretZoom.dll";
            bSmooth = !HdPlugins::Flag(Dll, "fast");
            bInverse = !HdPlugins::Flag(Dll, "inverse");
            Max = HdPlugins::Number(Dll, "max", Max);
            Shift = HdPlugins::Number(Dll, "shift", Shift);
            Ctrl = HdPlugins::Number(Dll, "ctrl", Ctrl);
            Alt = HdPlugins::Number(Dll, "alt", Alt);
            ShiftCtrl = HdPlugins::Number(Dll, "s+c", ShiftCtrl);
            CtrlAlt = HdPlugins::Number(Dll, "c+a", CtrlAlt);
            FactorSmall = HdPlugins::Number(Dll, "small", FactorSmall);
            FactorLarge = HdPlugins::Number(Dll, "large", FactorLarge);
            FactorSwitch = HdPlugins::Number(Dll, "switch", FactorSwitch);
            SpeedAdjust = HdPlugins::Number(Dll, "adjust", SpeedAdjust);
            SpeedInterval = static_cast<int>(HdPlugins::Number(Dll, "interval", static_cast<float>(SpeedInterval)));
        }
    };

    FTurretZoom& TurretZoom()
    {
        static FTurretZoom State;
        return State;
    }

    float& CamAxis(FTurretCamera* Cam, int Axis)
    {
        return Cam->Tether[1 + Axis];     // 0x00679064 y, 0x00679068 z
    }

    void AdjustZoom(FTurretZoom& Z, FTurretCamera* Cam, int Dir, unsigned Modifiers)
    {
        float Dist = 0.0f;
        bool bNegative = false;
        switch (Modifiers)
        {
        case 1: Dist = Z.Shift; break;
        case 4: Dist = Z.Ctrl; break;
        case 0x10: Dist = Z.Alt; break;
        case 5: Dist = Z.ShiftCtrl; break;
        case 0x14: Dist = Z.CtrlAlt; break;
        default:
        {
            float const Factor = fabsf(Z.Target[Z.Axis]) < Z.FactorSwitch ? Z.FactorLarge : Z.FactorSmall;
            if (Dir < 0)
                Z.Target[Z.Axis] /= Factor;
            else
                Z.Target[Z.Axis] *= Factor;
            goto TestSpeed;
        }
        }
        bNegative = Z.Target[Z.Axis] < 0.0f;
        if (bNegative)
            Dir = -Dir;
        Z.Target[Z.Axis] += Dist * static_cast<float>(Dir);
    TestSpeed:
        if (Z.Axis == 0)
            return;
        if (Z.Target[1] < 0.0f)
        {
            // Keep adjusting the turn speed while zooming in.
            int const Speed = -static_cast<int>(Z.Target[1]) / Z.SpeedInterval + 1;
            if (Speed != Z.Speed)
            {
                float const Factor = powf(Z.SpeedAdjust, static_cast<float>(Speed));
                Cam->YawRotateSpeed = Z.SavedYaw * Factor;
                Cam->PitchRotateSpeed = Z.SavedPitch * Factor;
                Z.Speed = Speed;
            }
        }
        else if (bNegative)
        {
            Cam->YawRotateSpeed = Z.SavedYaw;
            Cam->PitchRotateSpeed = Z.SavedPitch;
            Z.Speed = 0;
        }
    }
}

namespace
{
    // Zone 1.20 (adoxa, Zone.cpp). The zone sets are ordered largest radius first, as the
    // plugin's; equal radii are kept apart by address (the plugin's set would merge them).
    struct FZoneOrder
    {
        bool operator()(Universe::IZone const* A, Universe::IZone const* B) const
        {
            float const Ra = A->GetRadius(), Rb = B->GetRadius();
            return Ra != Rb ? Ra > Rb : A < B;
        }
    };
    typedef std::set<Universe::IZone const*, FZoneOrder> FZoneSet;

    struct FZoneState
    {
        bool bEnabled = false;
        unsigned ResourceBase = 0;
        bool bShowLeft = false;
        bool bUpper = false;
        unsigned DangerColors[4] = {0xff00ffff, 0xff007fff, 0xff0000ff, 0xff0000ff};
        FZoneSet Current, Previous;
        unsigned PreviousSystem = 0;
    };

    FZoneState& ZoneState()
    {
        static FZoneState State;
        return State;
    }

    bool SameNames(FZoneSet const& Set, Universe::IZone const* Zone)
    {
        for (Universe::IZone const* Other : Set)
            if (Other->GetName() == Zone->GetName())
                return true;
        return false;
    }

    // FmtStr_sprintf of a mad lib with the zone and the system; the length written.
    int FormatZoneLine(unsigned Ids, Universe::IZone const* Zone, unsigned System, wchar_t* Out, int Room)
    {
        FmtStr Message;
        Message.begin_mad_lib(Ids);
        unsigned const ZoneId = Zone->GetId();
        Message.append_zone_id(ZoneId);
        Message.append_system(System);
        Message.end_mad_lib();
        G_HdZoneFmtFallback = true;
        G_HdZoneFmtBlank = false;
        int const Written = FmtStrSprintf(&Message, Out, Room);
        G_HdZoneFmtFallback = false;
        return Written;
    }

    // DisplayZone: only when no arrival text is up; 0.25 s delay, coloured when dangerous.
    bool DisplayZone(FZoneState const& Z, unsigned Flags)
    {
        if (G_NnArrivalText != nullptr)
            return false;
        WinFactoryCreate("NN_ArrivalText", "NN_ArrivalText", nullptr, nullptr);
        NnArrivalText* const Text = G_NnArrivalText;
        if (Text == nullptr)
            return true;
        Text->Delay = 0.25f;
        Text->Caption->SetProperty(0x1d, reinterpret_cast<uintptr_t>(G_NnTextScratchB), 0);
        if ((Flags & 0x38u) != 0)
            Text->Caption->SetProperty(0x33, reinterpret_cast<uintptr_t>(&Z.DangerColors[(Flags & 0x30u) >> 4]), 1);
        return true;
    }

    void ParseZoneColor(FZoneState& Z, char const* Word, int Index)
    {
        char const* const Value = HdPlugins::FindOption("Zone.dll", Word);
        if (Value == nullptr || *Value == '\0')
            return;
        unsigned long const Rgb = strtoul(Value, nullptr, 16);
        Z.DangerColors[Index] =
            0xff000000u | ((Rgb & 0xffu) << 16) | (Rgb & 0xff00u) | ((Rgb >> 16) & 0xffu);
    }
}

void HdZoneRegisterResources()
{
    FZoneState& Z = ZoneState();
    if (!HdPlugins::Enabled("Zone.dll"))
        return;
    HMODULE const Strings = LoadLibraryExA("Zone.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (Strings == nullptr)
        return;
    Z.ResourceBase = RegisterIdsResourceModule(Strings);
    wchar_t Test[4] = {0};
    Z.bShowLeft = LoadStringW(Strings, 1, Test, 4) != 0 && Test[0] != 0;
    Z.bUpper = HdPlugins::Flag("Zone.dll", "upper");
    ParseZoneColor(Z, "low", 0);
    ParseZoneColor(Z, "medium", 1);
    ParseZoneColor(Z, "high", 2);
    ParseZoneColor(Z, "ultra", 3);
    Z.bEnabled = true;
}

void HdZoneReset()
{
    ZoneState().Previous.clear();
}

bool HdArrivalTextMultiLine()
{
    return ZoneState().bEnabled || HdPlugins::Enabled("Territory.dll");
}

void HdZoneObserve(Universe::IZone const* Zone, Vector const& PlayerPos, unsigned Index, unsigned Count)
{
    FZoneState& Z = ZoneState();
    if (!Z.bEnabled)
        return;
    if (Index == 0)
        Z.Current.clear();
    if (Zone->GetName() != 0 && Zone->InZone(PlayerPos, 0.0f))
        Z.Current.insert(Zone);
    if (Index != Count - 1 || Z.Current == Z.Previous)
        return;

    unsigned const System = GetPlayerStarSystemIdRetail();
    wchar_t* const Buffer = G_NnTextScratchB;
    int const Size = G_NnTextScratchCapacity;
    int Length = 0;
    unsigned Flags = 0;
    bool bLeft = false;
    if (Z.PreviousSystem == System)
    {
        for (Universe::IZone const* Old : Z.Previous)
        {
            if (SameNames(Z.Current, Old))
                continue;
            if (Length != 0)
                Buffer[Length++] = L'\n';
            int const Written = FormatZoneLine(Z.ResourceBase + 1, Old, Z.PreviousSystem, Buffer + Length, Size - Length);
            if (!G_HdZoneFmtBlank)
            {
                if (!Z.bShowLeft)
                {
                    bLeft = true;
                    break;
                }
                Length += Written;
            }
        }
    }
    for (Universe::IZone const* New : Z.Current)
    {
        if (SameNames(Z.Previous, New))
            continue;
        if (Length != 0)
            Buffer[Length++] = L'\n';
        int const Written = FormatZoneLine(Z.ResourceBase, New, System, Buffer + Length, Size - Length);
        if (!G_HdZoneFmtBlank)
            Length += Written;
        Flags |= New->PropertyFlags;
    }
    if (Length != 0)
    {
        Buffer[Length] = 0;
        if (Z.bUpper)
            CharUpperW(Buffer);
    }
    if ((Length != 0 && DisplayZone(Z, Flags)) || (Length == 0 && bLeft))
    {
        Z.Previous = Z.Current;
        Z.PreviousSystem = System;
    }
}

float HdHudShiftBase(unsigned RetailAddress, float Retail)
{
    static std::map<unsigned, float>* Values = nullptr;
    static bool bLoaded = false;
    if (!bLoaded)
    {
        bLoaded = true;
        // The raw lines: INI_Reader reads "5e28c8" as a number. GetPrivateProfileSection hands
        // back every [Group] section's lines.
        static char Lines[0x10000];
        if (HdPlugins::Enabled("HudShift.dll") &&
            GetPrivateProfileSectionA("Group", Lines, sizeof(Lines), "..\\DATA\\INTERFACE\\HudShift.ini") > 0)
        {
            Values = new std::map<unsigned, float>;
            for (char* Line = Lines; *Line != '\0'; Line += strlen(Line) + 1)
            {
                // position = xAddr, x, yAddr, y[, scale] ; comment -- an empty address skips
                // its value.
                char Text[256];
                strncpy(Text, Line, sizeof(Text) - 1);
                Text[sizeof(Text) - 1] = '\0';
                if (char* const Comment = strchr(Text, ';'))
                    *Comment = '\0';
                char* const Equals = strchr(Text, '=');
                if (Equals == nullptr)
                    continue;
                *Equals = '\0';
                char Key[32];
                if (sscanf(Text, " %31s", Key) != 1 || _stricmp(Key, "position") != 0)
                    continue;
                char* Fields[4] = {nullptr, nullptr, nullptr, nullptr};
                char* Cursor = Equals + 1;
                for (int Field = 0; Field < 4 && Cursor != nullptr; ++Field)
                {
                    Fields[Field] = Cursor;
                    Cursor = strchr(Cursor, ',');
                    if (Cursor != nullptr)
                        *Cursor++ = '\0';
                }
                for (int Pair = 0; Pair < 2; ++Pair)
                {
                    char* const Address = Fields[Pair * 2];
                    char* const Value = Fields[Pair * 2 + 1];
                    if (Address == nullptr || Value == nullptr)
                        continue;
                    char* End = nullptr;
                    unsigned long const Retail32 = strtoul(Address, &End, 16);
                    if (End == Address)
                        continue;
                    (*Values)[static_cast<unsigned>(Retail32)] = static_cast<float>(atof(Value));
                }
            }
        }
    }
    if (Values == nullptr)
        return Retail;
    std::map<unsigned, float>::const_iterator It = Values->find(RetailAddress & 0x00ffffffu);
    return It == Values->end() ? Retail : It->second;
}

namespace
{
    // VersionText 1.0 (closed source; behaviour from the DLL's disassembly).
    struct FVersionTextConfig
    {
        bool bRandom = true;
        float CycleSeconds = 4.0f;
        std::vector<unsigned> Colors;                    // 0xffBBGGRR, as the game's colours
        std::vector<std::pair<std::string, std::string>> Sounds; // sound, voice
        size_t NextSound = 0;
    };

    FVersionTextConfig& VersionTextConfig()
    {
        static FVersionTextConfig Config;
        static bool bLoaded = false;
        if (bLoaded)
            return Config;
        bLoaded = true;
        Config.bRandom = HdPlugins::ConfigBool("VersionText.ini", "Options", "random_sounds", true);
        Config.CycleSeconds = HdPlugins::ConfigFloat("VersionText.ini", "Options", "color_cycle_seconds", 4.0f);
        // [Colors] / [Sounds] repeat their key; read the raw sections.
        static char Lines[0x4000];
        if (GetPrivateProfileSectionA("Colors", Lines, sizeof(Lines), ".\\VersionText.ini") > 0)
            for (char const* Line = Lines; *Line != '\0'; Line += strlen(Line) + 1)
            {
                int R, G, B;
                char const* const Value = strchr(Line, '=');
                if (Value != nullptr && sscanf(Value + 1, " %d , %d , %d", &R, &G, &B) == 3)
                    Config.Colors.push_back(0xff000000u | ((B & 0xff) << 16) | ((G & 0xff) << 8) | (R & 0xff)); // ABGR
            }
        if (GetPrivateProfileSectionA("Sounds", Lines, sizeof(Lines), ".\\VersionText.ini") > 0)
            for (char const* Line = Lines; *Line != '\0'; Line += strlen(Line) + 1)
            {
                char const* const Value = strchr(Line, '=');
                if (Value == nullptr)
                    continue;
                char Sound[128] = {0}, Voice[128] = {0};
                if (sscanf(Value + 1, " %127[^, ;] , %127[^ ;]", Sound, Voice) >= 1)
                    Config.Sounds.push_back(std::make_pair(std::string(Sound), std::string(Voice)));
            }
        if (Config.Colors.empty())
        {
            // An HSV rainbow when the ini names no colours.
            static unsigned const kRainbow[6] = {0xff0000ffu, 0xff00ffffu, 0xff00ff00u,
                                                 0xffffff00u, 0xffff0000u, 0xffff00ffu}; // ABGR
            Config.Colors.assign(kRainbow, kRainbow + 6);
        }
        return Config;
    }

    unsigned LerpColor(unsigned A, unsigned B, float T)
    {
        unsigned Out = 0xff000000u;
        for (int Shift = 0; Shift < 24; Shift += 8)
        {
            float const Ca = static_cast<float>((A >> Shift) & 0xff);
            float const Cb = static_cast<float>((B >> Shift) & 0xff);
            Out |= (static_cast<unsigned>(Ca + (Cb - Ca) * T) & 0xff) << Shift;
        }
        return Out;
    }

    void PlayVersionSound(std::pair<std::string, std::string> const& Line)
    {
        unsigned PlayKey[6] = {};
        void* Handle = nullptr;
        if (!Line.second.empty())
        {
            unsigned const VoiceId = CreateID(Line.second.c_str());
            unsigned const MsgId = CreateID(Line.first.c_str());
            if (!StarSysHasVoiceMessage(VoiceId, MsgId))
                StarSysPreloadVoiceMessage(VoiceId, MsgId);
            FVoiceProfile* const Profile = LookupClientVoice(VoiceId);
            FVoiceMsg* const Msg = Profile != nullptr ? Profile->FindMessage(MsgId) : nullptr;
            if (Msg != nullptr)
                PlayClientSoundArchIfIdle(static_cast<Archetype::ISound*>(Msg), PlayKey, &Handle);
            return;
        }
        ID_String SoundId;
        SoundId.Id = CreateID(Line.first.c_str());
        if (void* const Arch = LookupClientSoundArch(&SoundId))
            PlayClientSoundArchIfIdle(Arch, nullptr, nullptr);
    }

    class HdVersionText : public NnTextControl
    {
    public:
        HdVersionText()
        {
            Byte35c = 1;   // clickable
            Byte380 = 0;
        }

        ~HdVersionText() override
        {
            if (bCycling)
                RestoreColors();
        }

        bool Create(Win3D* NewParent, void* Data) override
        {
            bool const bCreated = NnTextControl::Create(NewParent, Data);
            Byte35c = 1;
            Byte380 = 0;
            return bCreated;
        }

        void Draw() override
        {
            if (bCycling)
            {
                FVersionTextConfig const& Config = VersionTextConfig();
                float const Step = static_cast<float>(GetClientFrameTimeCapped()) /
                                   (Config.CycleSeconds > 0.0f ? Config.CycleSeconds : 4.0f);
                Phase += Step;
                while (Phase >= 1.0f)
                    Phase -= 1.0f;
                size_t const Count = Config.Colors.size();
                float const Scaled = Phase * static_cast<float>(Count);
                size_t const Index = static_cast<size_t>(Scaled) % Count;
                unsigned const Color =
                    LerpColor(Config.Colors[Index], Config.Colors[(Index + 1) % Count], Scaled - static_cast<float>(static_cast<size_t>(Scaled)));
                TextColour = Color;
                if (bOnMainMenu)
                    for (TextMsgButton& Button : GetTypeSelect().Buttons)
                        Button.ColorIdle = Color;
            }
            NnTextControl::Draw();
        }

        bool MouseEvent(FWin3DMouseEvent const* Event) override
        {
            if (Event->Field0 == -1 && (Event->Field1 == 2 || Event->Field1 == 3))
            {
                unsigned const Modifiers = HudCommandRecord()->Modifiers;
                if ((Modifiers & 1) != 0)
                {
                    if (bCycling)
                        RestoreColors();
                    else
                    {
                        SavedText = TextColour;
                        bOnMainMenu = GetTypeSelect().Version == this;
                        if (bOnMainMenu)
                            for (int Index = 0; Index < 5; ++Index)
                                SavedButtons[Index] = GetTypeSelect().Buttons[Index].ColorIdle;
                        Phase = 0.0f;
                        bCycling = true;
                    }
                }
                else if ((Modifiers & 4) == 4)
                {
                    FVersionTextConfig& Config = VersionTextConfig();
                    if (!Config.Sounds.empty())
                    {
                        size_t const Index = Config.bRandom ? static_cast<size_t>(rand()) % Config.Sounds.size()
                                                            : Config.NextSound++ % Config.Sounds.size();
                        PlayVersionSound(Config.Sounds[Index]);
                    }
                }
            }
            return NnTextControl::MouseEvent(Event);
        }

    private:
        void RestoreColors()
        {
            TextColour = SavedText;
            if (bOnMainMenu && GetTypeSelect().Version == this)
                for (int Index = 0; Index < 5; ++Index)
                    GetTypeSelect().Buttons[Index].ColorIdle = SavedButtons[Index];
            bCycling = false;
        }

        bool bCycling = false;
        bool bOnMainMenu = false;
        float Phase = 0.0f;
        unsigned long SavedText = 0;
        unsigned long SavedButtons[5] = {};
    };

    WINFACTORY<HdVersionText> G_HdVersionTextFactory("HdVersionText");
}

char const* HdVersionTextClass()
{
    static const bool bEnabled = HdPlugins::Enabled("VersionText.dll");
    return bEnabled ? "HdVersionText" : "TextControl";
}

float HdVersionTextHeight(float Retail)
{
    // 0x005745d9 / 0x0056dd8d: the line-height FILD NOPed, the 0.002 FMUL a FLD of 0.03.
    static const bool bEnabled = HdPlugins::Enabled("VersionText.dll");
    return bEnabled ? 0.03f : Retail;
}

bool HdTurretZoomEnabled()
{
    static const bool bEnabled = HdPlugins::Enabled("TurretZoom.dll");
    return bEnabled;
}

void HdTurretZoomWheel(int Wheel, unsigned Modifiers)
{
    // Zoom() of TurretZoom.c.
    FTurretZoom& Z = TurretZoom();
    FTurretCamera* const Cam = &G_SpaceApp.Turret;
    if (Wheel == 0)
        return;
    if (Modifiers == (1u | 0x10u))
    {
        Z.Axis = Wheel < 0 ? 0 : 1;
        return;
    }
    if (Modifiers == (1u | 4u | 0x10u))
    {
        if (Z.Axis == 1 && Z.Target[1] < 0.0f)
        {
            if (Wheel < 0)
            {
                Cam->YawRotateSpeed *= Z.SpeedAdjust;
                Cam->PitchRotateSpeed *= Z.SpeedAdjust;
            }
            else
            {
                Cam->YawRotateSpeed /= Z.SpeedAdjust;
                Cam->PitchRotateSpeed /= Z.SpeedAdjust;
            }
        }
        return;
    }
    float& Target = Z.Target[Z.Axis];
    if ((Wheel < 0) == Z.bInverse)
    {
        if (Target < 0.0f)
        {
            if (Target > -Z.Max)
                AdjustZoom(Z, Cam, 1, Modifiers);
        }
        else if (Modifiers == 0 && Target < 1.0f)
        {
            Target = -Target;
            if (Z.Axis == 1)
            {
                Cam->YawRotateSpeed *= Z.SpeedAdjust;
                Cam->PitchRotateSpeed *= Z.SpeedAdjust;
                Z.Speed = 1;
            }
        }
        else
            AdjustZoom(Z, Cam, -1, Modifiers);
    }
    else
    {
        if (Target >= 0.0f)
        {
            if (Target < Z.Max)
                AdjustZoom(Z, Cam, 1, Modifiers);
        }
        else if (Modifiers == 0 && Target > -1.0f)
        {
            Target = -Target;
            if (Z.Axis == 1)
            {
                Cam->YawRotateSpeed = Z.SavedYaw;
                Cam->PitchRotateSpeed = Z.SavedPitch;
                Z.Speed = 0;
            }
        }
        else
            AdjustZoom(Z, Cam, -1, Modifiers);
    }
    if (!Z.bSmooth)
        CamAxis(Cam, Z.Axis) = Z.Target[Z.Axis];
}

void HdTurretZoomToggle(FTurretCamera* Cam, bool bEntering)
{
    FTurretZoom& Z = TurretZoom();
    if (bEntering)
    {
        Z.Target[0] = Z.SavedY = Cam->Tether[1];
        Z.Target[1] = Z.SavedZ = Cam->Tether[2];
        Z.SavedYaw = Cam->YawRotateSpeed;
        Z.SavedPitch = Cam->PitchRotateSpeed;
        Z.Speed = 0;
        Z.Axis = 1;
    }
    else
    {
        Cam->Tether[1] = Z.SavedY;
        Cam->Tether[2] = Z.SavedZ;
        Cam->YawRotateSpeed = Z.SavedYaw;
        Cam->PitchRotateSpeed = Z.SavedPitch;
    }
}

void HdTurretZoomEase(FTurretCamera* Cam, float DeltaSeconds)
{
    FTurretZoom& Z = TurretZoom();
    if (!Z.bSmooth)
        return;
    Cam->Tether[1] += (Z.Target[0] - Cam->Tether[1]) * DeltaSeconds * Cam->AccelSpeed;
    Cam->Tether[2] += (Z.Target[1] - Cam->Tether[2]) * DeltaSeconds * Cam->AccelSpeed;
}

bool HdWheelScroll(Win3D* Target, int Wheel)
{
    static const bool bEnabled = HdPlugins::Enabled("WheelScroll.dll");
    if (!bEnabled || Wheel == 0)
        return false;
    for (Win3D* Window = Target; Window != nullptr; Window = Window->GetParent())
    {
        UiScroll* const Scroll = ScrollOf(Window);
        if (Scroll == nullptr)
            continue;
        if ((Scroll->Flags & 2) == 0)
            return false;            // a hidden scrollbar: nothing to scroll
        Scroll->WheelStep(Wheel);
        return true;
    }
    return false;
}
