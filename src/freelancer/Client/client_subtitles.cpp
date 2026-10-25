// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file adds subtitles to the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

#include "client_subtitles.h"

#include "font_atlas.h"
#include "font_imp.h"
#include "font_string.h"
#include "globals.h"
#include "menu_sound_arch.h"
#include "voice_profiles.h"
#include "win3d_immediate.h"
#include "win3d_renderables.h"

#include <ctype.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    // FLSubs' text areas, laid out on its 1400x1050 reference screen (its subtitles.cfg).
    // On a wider screen the cinematic ones stretch along the letterbox bands; the in-flight
    // ones keep their size and stay at their edge, as the HUD does (they would otherwise run
    // under the centred nav bar). Colours are the draw tint, 0xAABBGGRR.
    enum EAnchor
    {
        AnchorStretch,
        AnchorLeft,
        AnchorRight,
    };

    struct FArea
    {
        char const* Name;
        char const* Font; // a fonts.ini [TrueType] nickname
        float X, Y, W, H;
        EAnchor Anchor;
        unsigned Align;   // FFontStringDrawOrigin::Field10: 0 left, 1 centre
        bool bBottom;     // stacked up from the box's bottom edge instead of down from its top
        unsigned Color;
        DWORD LingerMs;   // how long a caption stays after its sound has ended
    };

    FArea const kAreas[] = {
        // Bottom-aligned, 25 reference pixels up: inside the black band of a 4:3 letterbox,
        // and low over the picture on a wide screen, where the bands are thin.
        {"Cinematic", "NewsTicker", 175.f, 885.f, 1050.f, 140.f, AnchorStretch, 1, true, 0xffe0c387u, 250},
        {"News", "NewsTicker", 175.f, 10.f, 1050.f, 140.f, AnchorStretch, 1, false, 0xff30f0ccu, 250},
        {"Ingame", "Normal", 164.f, 78.f, 512.f, 140.f, AnchorLeft, 0, false, 0xffe0c387u, 1500},
        // Other ships' chatter goes under the comm line (FLSubs had it at the top edge, where the
        // nav bar and its tooltips are).
        {"Ingame_BG", "Normal", 164.f, 222.f, 512.f, 72.f, AnchorLeft, 0, false, 0xff1dbf3bu, 1500},
        {"Ingame_NN", "Normal", 1110.f, 62.f, 284.f, 120.f, AnchorRight, 1, false, 0xffff5aceu, 1500},
    };
    int const kAreaCount = static_cast<int>(sizeof(kAreas) / sizeof(kAreas[0]));

    // A fragment that starts within this long after the previous one in its area ended
    // continues that caption: the game assembles many lines from pieces ("Welcome to" +
    // "Freeport 4.").
    DWORD const kJoinGapMs = 600;

    struct FCaption
    {
        int Area;
        DWORD Start, End; // timed captions only
        std::wstring Text;
    };

    struct FEntry
    {
        bool bTimed = false;
        std::vector<FCaption> Captions; // one for a static entry
    };

    // A source the overlay has seen playing.
    struct FTracked
    {
        Archetype::ISound* Arch = nullptr;
        FEntry const* Entry = nullptr;
        DWORD Started = 0; // the frame it was first seen, moved on while it is paused
        bool bPaused = false;
        bool bSeen = false;
        int Paragraph = -1; // a static caption's paragraph id
    };

    // A caption on screen built from one or more static fragments.
    struct FParagraph
    {
        int Id;
        int Area;
        std::wstring Text;
        int Live = 0;      // sources still playing into it
        DWORD Ended = 0;   // when Live last dropped to 0
        bool bPaused = false;
    };

    struct FCachedString
    {
        std::unique_ptr<FFontString> String;
        unsigned LastFrame = 0;
    };

    struct FState
    {
        bool bLoaded = false;
        bool bEnabled = false;
        bool bLog = false;
        std::unordered_map<std::string, FEntry> Entries;
        std::unordered_map<void*, FTracked> Tracked;
        std::vector<FParagraph> Paragraphs;
        int NextParagraph = 1;
        DWORD LastTick = 0;
        unsigned Frame = 0;
        IFont* Fonts[sizeof(kAreas) / sizeof(kAreas[0])] = {};
        std::unordered_map<std::wstring, FCachedString> Strings[sizeof(kAreas) / sizeof(kAreas[0])];
    };

    FState* G_Subtitles = nullptr;

    int AreaIndex(char const* Name)
    {
        for (int I = 0; I < kAreaCount; ++I)
            if (strcmp(kAreas[I].Name, Name) == 0)
                return I;
        return -1;
    }

    std::wstring FromUtf8(char const* Text)
    {
        std::wstring Out;
        unsigned char const* P = reinterpret_cast<unsigned char const*>(Text);
        while (*P != 0)
        {
            unsigned Cp = *P++;
            int More = 0;
            if (Cp >= 0xf0)
            {
                Cp &= 0x07;
                More = 3;
            }
            else if (Cp >= 0xe0)
            {
                Cp &= 0x0f;
                More = 2;
            }
            else if (Cp >= 0xc0)
            {
                Cp &= 0x1f;
                More = 1;
            }
            for (; More > 0 && (*P & 0xc0) == 0x80; --More)
                Cp = (Cp << 6) | (*P++ & 0x3f);
            if (sizeof(wchar_t) == 2 && Cp > 0xffff)
            {
                Cp -= 0x10000;
                Out += static_cast<wchar_t>(0xd800 + (Cp >> 10));
                Out += static_cast<wchar_t>(0xdc00 + (Cp & 0x3ff));
            }
            else
                Out += static_cast<wchar_t>(Cp);
        }
        return Out;
    }

    // One line of subtitles.txt (tools/subtitles.py):
    //   S <key> <area> <text>  |  D <key> <area> <start ms> <end ms> <text>
    void ParseLine(FState& State, char* Line)
    {
        char* Fields[6] = {};
        int Count = 0;
        for (char* P = Line; Count < 6;)
        {
            Fields[Count++] = P;
            char* const Tab = Count < 6 ? strchr(P, '\t') : nullptr;
            if (Tab == nullptr)
                break;
            *Tab = '\0';
            P = Tab + 1;
        }
        bool const bTimed = Fields[0][0] == 'D';
        if ((Fields[0][0] != 'S' && !bTimed) || Count != (bTimed ? 6 : 4))
            return;
        int const Area = AreaIndex(Fields[2]);
        if (Area < 0)
            return;
        FEntry& Entry = State.Entries[Fields[1]];
        Entry.bTimed = bTimed;
        FCaption Caption;
        Caption.Area = Area;
        Caption.Start = bTimed ? static_cast<DWORD>(strtoul(Fields[3], nullptr, 10)) : 0;
        Caption.End = bTimed ? static_cast<DWORD>(strtoul(Fields[4], nullptr, 10)) : 0;
        Caption.Text = FromUtf8(Fields[bTimed ? 5 : 3]);
        Entry.Captions.push_back(Caption);
    }

    void Load(FState& State)
    {
        State.bLoaded = true;
        char const* const Switch = getenv("FLWIN_SUBTITLES");
        if (Switch != nullptr && strcmp(Switch, "0") == 0)
            return;
        State.bLog = getenv("FLWIN_SUBTITLES_LOG") != nullptr;
        FILE* const File = fopen("subtitles.txt", "rb");
        if (File == nullptr)
            return;
        std::vector<char> Line(4096);
        while (fgets(Line.data(), static_cast<int>(Line.size()), File) != nullptr)
        {
            size_t Len = strlen(Line.data());
            while (Len > 0 && (Line[Len - 1] == '\n' || Line[Len - 1] == '\r'))
                Line[--Len] = '\0';
            if (Len > 0 && Line[0] != ';')
                ParseLine(State, Line.data());
        }
        fclose(File);
        State.bEnabled = !State.Entries.empty();
        if (State.bLog)
            fprintf(stderr, "subtitles: %zu sounds captioned\n", State.Entries.size());
    }

    // The table key of what a source plays: a voice message by its profile and id, any other
    // sound by its wav path below DATA\AUDIO.
    std::string KeyOf(Archetype::ISound* Arch)
    {
        char Buffer[MAX_PATH + 32];
        FVoiceMsg* const Msg = dynamic_cast<FVoiceMsg*>(Arch);
        if (Msg != nullptr && Msg->Owner != nullptr && !Msg->Owner->UtfPath.empty() &&
            Msg->Owner->Name != nullptr)
        {
            std::string Profile = Msg->Owner->Name;
            for (char& C : Profile)
                C = static_cast<char>(tolower(static_cast<unsigned char>(C)));
            snprintf(Buffer, sizeof(Buffer), "voice:%s:%08x", Profile.c_str(), Msg->Id.Id);
            return Buffer;
        }
        char const* const File = Arch->GetFileName();
        if (File == nullptr)
            return std::string();
        std::string Path;
        for (char const* P = File; *P != '\0'; ++P)
            Path += *P == '\\' ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(*P)));
        while (Path.compare(0, 2, "./") == 0 || Path.compare(0, 1, "/") == 0)
            Path.erase(0, Path[0] == '/' ? 1 : 2);
        if (Path.compare(0, 6, "audio/") == 0)
            Path.erase(0, 6);
        return "file:" + Path;
    }

    std::string Narrow(std::wstring const& Text)
    {
        std::string Out;
        for (wchar_t C : Text)
            Out += C < 0x80 ? static_cast<char>(C) : '?';
        return Out;
    }

    // "subtitles: <tick> <key> [area] text" for every sound that starts.
    void LogStart(FState const& State, std::string const& Key, FEntry const* Entry, DWORD Now)
    {
        if (!State.bLog)
            return;
        if (Entry == nullptr)
        {
            fprintf(stderr, "subtitles: %lu %s (none)\n", static_cast<unsigned long>(Now),
                    Key.c_str());
            return;
        }
        fprintf(stderr, "subtitles: %lu %s [%s%s] %s%s\n", static_cast<unsigned long>(Now),
                Key.c_str(), kAreas[Entry->Captions.front().Area].Name,
                Entry->bTimed ? ", timed" : "", Narrow(Entry->Captions.front().Text).c_str(),
                Entry->Captions.size() > 1 ? " ..." : "");
    }

    FParagraph* FindParagraph(FState& State, int Id)
    {
        for (FParagraph& P : State.Paragraphs)
            if (P.Id == Id)
                return &P;
        return nullptr;
    }

    bool EndsSentence(wchar_t C)
    {
        return C == L'.' || C == L'!' || C == L'?' || C == L',' || C == L':' || C == L';';
    }

    // Appends a fragment of an assembled line. A lone "-" (a spoken "dash" in a call sign)
    // joins both neighbours unspaced, as does a leading comma with the text before it; a whole
    // sentence after an unpunctuated fragment ("Planet Manhattan" + "Docking ring cleared.")
    // starts a new sentence.
    void JoinFragment(std::wstring& Text, std::wstring const& Fragment)
    {
        bool const bGlue = Fragment == L"-" || Fragment[0] == L',' ||
                           (!Text.empty() && Text.back() == L'-');
        bool const bSentence = Fragment.find(L' ') != std::wstring::npos &&
                               iswupper(Fragment[0]) && EndsSentence(Fragment.back());
        if (bSentence && !Text.empty() && !EndsSentence(Text.back()))
            Text += L'.';
        if (!bGlue)
            Text += L' ';
        Text += Fragment;
    }

    // A static caption starts: join it to its area's newest caption when that one just ended
    // (or shows the same text), else open a new one.
    int OpenParagraph(FState& State, FCaption const& Caption, DWORD Now)
    {
        FParagraph* Last = nullptr;
        for (FParagraph& P : State.Paragraphs)
            if (P.Area == Caption.Area)
                Last = &P;
        if (Last != nullptr)
        {
            if (Last->Text == Caption.Text)
            {
                ++Last->Live;
                return Last->Id;
            }
            // Only fragments join: a previous caption that didn't end its sentence ("Planet
            // Manhattan"), or a continuation (", this is", "-", a lowercase start). Whole
            // sentences in quick succession -- a radio exchange -- are captions of their own,
            // or one line would grow for as long as the talking goes on.
            bool const bPrevFragment = !Last->Text.empty() && !EndsSentence(Last->Text.back());
            bool const bContinuation = Caption.Text[0] == L',' || Caption.Text[0] == L'-' ||
                                       iswlower(Caption.Text[0]);
            if (Last->Live == 0 && Now - Last->Ended <= kJoinGapMs &&
                (bPrevFragment || bContinuation) && Last->Text.size() < 200)
            {
                JoinFragment(Last->Text, Caption.Text);
                Last->Live = 1;
                return Last->Id;
            }
        }
        FParagraph P;
        P.Id = State.NextParagraph++;
        P.Area = Caption.Area;
        P.Text = Caption.Text;
        P.Live = 1;
        State.Paragraphs.push_back(P);
        return P.Id;
    }

    void CloseParagraph(FState& State, int Id, DWORD Now)
    {
        FParagraph* const P = FindParagraph(State, Id);
        if (P != nullptr && P->Live > 0 && --P->Live == 0)
        {
            P->Ended = Now;
            // "subtitles: <tick> = [area] caption so far" when its last sound ends.
            if (State.bLog)
                fprintf(stderr, "subtitles: %lu = [%s] %s\n", static_cast<unsigned long>(Now),
                        kAreas[P->Area].Name, Narrow(P->Text).c_str());
        }
    }

    struct FPlaying
    {
        void* Source;
        Archetype::ISound* Arch;
        bool bPaused;
    };

    void CollectSource(void* Context, void* Source, Archetype::ISound* Arch, bool bPaused)
    {
        static_cast<std::vector<FPlaying>*>(Context)->push_back({Source, Arch, bPaused});
    }

    void Poll(FState& State, DWORD Now)
    {
        DWORD const Delta = State.LastTick != 0 ? Now - State.LastTick : 0;
        State.LastTick = Now;
        std::vector<FPlaying> Playing;
        VisitPlayingClientSounds(&CollectSource, &Playing);

        // Sounds that ended (or whose source now plays something else) are closed before new
        // ones open: the game starts a line's next fragment in the frame the last one ends.
        for (auto& Pair : State.Tracked)
            Pair.second.bSeen = false;
        for (FPlaying const& P : Playing)
        {
            auto const It = State.Tracked.find(P.Source);
            if (It != State.Tracked.end() && It->second.Arch == P.Arch)
                It->second.bSeen = true;
        }
        for (auto It = State.Tracked.begin(); It != State.Tracked.end();)
        {
            if (It->second.bSeen)
            {
                ++It;
                continue;
            }
            if (It->second.Paragraph >= 0)
                CloseParagraph(State, It->second.Paragraph, Now);
            It = State.Tracked.erase(It);
        }

        for (FParagraph& P : State.Paragraphs)
            P.bPaused = false;
        for (FPlaying const& P : Playing)
        {
            auto It = State.Tracked.find(P.Source);
            if (It == State.Tracked.end())
            {
                FTracked Track;
                Track.Arch = P.Arch;
                Track.Started = Now;
                std::string const Key = KeyOf(P.Arch);
                auto const Found = State.Entries.find(Key);
                Track.Entry = Found != State.Entries.end() ? &Found->second : nullptr;
                LogStart(State, Key, Track.Entry, Now);
                if (Track.Entry != nullptr && !Track.Entry->bTimed)
                    Track.Paragraph = OpenParagraph(State, Track.Entry->Captions.front(), Now);
                It = State.Tracked.emplace(P.Source, Track).first;
            }
            FTracked& Track = It->second;
            // A paused sound's clock stands still, and its caption is hidden.
            if (P.bPaused && Track.bPaused)
                Track.Started += Delta;
            Track.bPaused = P.bPaused;
            if (Track.bPaused && Track.Paragraph >= 0)
                if (FParagraph* const Para = FindParagraph(State, Track.Paragraph))
                    Para->bPaused = true;
        }

        for (size_t I = 0; I < State.Paragraphs.size();)
        {
            FParagraph const& P = State.Paragraphs[I];
            if (P.Live == 0 && Now - P.Ended > kAreas[P.Area].LingerMs)
                State.Paragraphs.erase(State.Paragraphs.begin() + static_cast<long>(I));
            else
                ++I;
        }
    }

    FFontString* StringFor(FState& State, int Area, std::wstring const& Text)
    {
        FCachedString& Cached = State.Strings[Area][Text];
        if (!Cached.String)
        {
            Cached.String.reset(new FFontString());
            FontStringSetFont(Cached.String.get(), State.Fonts[Area]);
            Cached.String->bMultiLine = 1;
            FontStringSetText(Cached.String.get(), Text.c_str());
        }
        Cached.LastFrame = State.Frame;
        return Cached.String.get();
    }

    void DrawArea(FState& State, int Area, std::vector<std::wstring const*> const& Lines)
    {
        if (Lines.empty() || State.Fonts[Area] == nullptr)
            return;
        FArea const& A = kAreas[Area];
        float const ScreenW = static_cast<float>(G_RenderSurfaceWidth);
        float const Sy = static_cast<float>(G_RenderSurfaceHeight) / 1050.f;
        float const Sx = A.Anchor == AnchorStretch ? ScreenW / 1400.f : Sy;
        float const BoxX = A.Anchor == AnchorRight ? ScreenW - (1400.f - A.X) * Sx : A.X * Sx;
        float const BoxW = A.W * Sx;
        float const BoxH = A.H * Sy;

        // Lay out every caption at the box width; when they do not all fit, the oldest go.
        std::vector<FFontString*> Strings;
        std::vector<int> Heights;
        int Total = 0;
        for (std::wstring const* Text : Lines)
        {
            FFontString* const S = StringFor(State, Area, *Text);
            if (S->ScaleKey != static_cast<int>(BoxW))
            {
                S->ScaleKey = static_cast<int>(BoxW);
                S->LayoutGen = 0;
            }
            int Size[2] = {};
            FontStringGetExtent(S, Size);
            Strings.push_back(S);
            Heights.push_back(Size[1]);
            Total += Size[1];
        }
        size_t First = 0;
        while (First + 1 < Strings.size() && static_cast<float>(Total) > BoxH)
            Total -= Heights[First++];

        unsigned const SavedColor = FontAtlasDrawColor();
        FontAtlasSetDrawColor(A.Color);
        float Y = A.bBottom ? (A.Y + A.H) * Sy - static_cast<float>(Total) : A.Y * Sy;
        for (size_t I = First; I < Strings.size(); ++I)
        {
            FFontStringDrawOrigin Origin = {};
            Origin.X = BoxX;
            Origin.Y = Y;
            Origin.Scale = BoxW;
            Origin.Height = static_cast<float>(Heights[I]);
            Origin.Field10 = A.Align;
            FontStringDrawChildren(&Origin, Strings[I], nullptr, nullptr);
            Y += static_cast<float>(Heights[I]);
        }
        FontAtlasSetDrawColor(SavedColor);
    }

    void PruneStrings(FState& State)
    {
        // Font strings hold atlas pages; drop the ones not drawn for a few seconds.
        if ((State.Frame & 63u) != 0)
            return;
        for (auto& Cache : State.Strings)
            for (auto It = Cache.begin(); It != Cache.end();)
                if (State.Frame - It->second.LastFrame > 300u)
                    It = Cache.erase(It);
                else
                    ++It;
    }
}

void DrawClientSubtitles()
{
    if (G_Subtitles == nullptr)
        G_Subtitles = new FState();
    FState& State = *G_Subtitles;
    if (!State.bLoaded)
        Load(State);
    if (!State.bEnabled)
        return;

    ++State.Frame;
    DWORD const Now = GetTickCount();
    Poll(State, Now);

    std::vector<std::wstring const*> Lines[sizeof(kAreas) / sizeof(kAreas[0])];
    bool bAny = false;
    for (FParagraph const& P : State.Paragraphs)
        if (!P.bPaused)
        {
            Lines[P.Area].push_back(&P.Text);
            bAny = true;
        }
    for (auto const& Pair : State.Tracked)
    {
        FTracked const& Track = Pair.second;
        if (Track.Entry == nullptr || !Track.Entry->bTimed || Track.bPaused)
            continue;
        DWORD const Elapsed = Now - Track.Started;
        for (FCaption const& C : Track.Entry->Captions)
            if (Elapsed >= C.Start && Elapsed < C.End)
            {
                Lines[C.Area].push_back(&C.Text);
                bAny = true;
            }
    }
    if (bAny)
    {
        UiOverlayPrepPipeline();
        int SavedViewport[4] = {};
        Win3DGetViewport(0xba2, SavedViewport);
        Win3DSetViewportRect(0, 0, static_cast<int>(G_RenderSurfaceWidth),
                             static_cast<int>(G_RenderSurfaceHeight), __FILE__, __LINE__);
        for (int Area = 0; Area < kAreaCount; ++Area)
        {
            if (Lines[Area].empty())
                continue;
            if (State.Fonts[Area] == nullptr)
                State.Fonts[Area] = FindMenuFontImp(kAreas[Area].Font);
            DrawArea(State, Area, Lines[Area]);
        }
        UiOverlayPrepPipeline();
        Win3DSetViewportRect(SavedViewport[0], SavedViewport[1], SavedViewport[2],
                             SavedViewport[3], __FILE__, __LINE__);
    }
    PruneStrings(State);
}

void ShutdownClientSubtitles()
{
    delete G_Subtitles;
    G_Subtitles = nullptr;
}
