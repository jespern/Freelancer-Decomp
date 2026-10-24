// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Freelancer: HD Edition plugins built into the port (freelancer-mac's docs/mods.md).
//
// An HD install loads its plugin DLLs from EXE/dacom.ini [Libraries]; the port implements those
// plugins in the modules they patched, and the same [Libraries] line turns each one on for a game
// folder (DACOM records it instead of loading a DLL). A folder whose DACOM.INI names no HD plugin
// (the vanilla one) behaves as retail. Options follow the DLL name, as HD writes them:
//     TurretZoom.dll inverse max=1000 shift=200
#pragma once

#include "dacom.h"

#include <cstdlib>
#include <cstring>

namespace HdPlugins
{
    inline bool Enabled(const char* Dll)
    {
        return DACOM_HdPlugin(Dll) != nullptr;
    }

    // Looks for Word (a flag such as "inverse", or "max" of "max=1000") among the options.
    // Returns the text after '=' ("" for a bare flag) or null when absent.
    inline const char* FindOption(const char* Dll, const char* Word)
    {
        const char* Options = DACOM_HdPlugin(Dll);
        if (Options == nullptr)
            return nullptr;
        const size_t Length = std::strlen(Word);
        for (const char* P = Options; *P != '\0';)
        {
            while (*P == ' ' || *P == '\t')
                ++P;
            const char* End = P;
            while (*End != '\0' && *End != ' ' && *End != '\t')
                ++End;
            if ((size_t)(End - P) >= Length && _strnicmp(P, Word, Length) == 0 &&
                (P + Length == End || P[Length] == '='))
                return P[Length] == '=' ? P + Length + 1 : P + Length;
            P = End;
        }
        return nullptr;
    }

    inline bool Flag(const char* Dll, const char* Word)
    {
        return FindOption(Dll, Word) != nullptr;
    }

    inline float Number(const char* Dll, const char* Word, float Default)
    {
        const char* Value = FindOption(Dll, Word);
        return Value != nullptr && *Value != '\0' && *Value != ' ' ? (float)std::atof(Value) : Default;
    }

    // A plugin's own configuration file in the EXE folder (flplusplus.ini, FLSharp.ini, ...),
    // copied there by the mod profile. Missing keys give Default, as the plugins do.
    inline bool ConfigString(const char* File, const char* Section, const char* Key, char* Out, DWORD Size)
    {
        char Path[MAX_PATH] = ".\\";
        std::strncat(Path, File, sizeof(Path) - 3);
        GetPrivateProfileStringA(Section, Key, "", Out, Size, Path);
        if (char* Comment = std::strchr(Out, ';'))
            *Comment = '\0';
        size_t Length = std::strlen(Out);
        while (Length > 0 && (Out[Length - 1] == ' ' || Out[Length - 1] == '\t'))
            Out[--Length] = '\0';
        return Length > 0;
    }

    inline float ConfigFloat(const char* File, const char* Section, const char* Key, float Default)
    {
        char Value[64];
        return ConfigString(File, Section, Key, Value, sizeof(Value)) ? (float)std::atof(Value) : Default;
    }

    inline bool ConfigBool(const char* File, const char* Section, const char* Key, bool Default)
    {
        char Value[64];
        if (!ConfigString(File, Section, Key, Value, sizeof(Value)))
            return Default;
        return _stricmp(Value, "true") == 0 || _stricmp(Value, "yes") == 0 || std::atoi(Value) != 0;
    }

    // flplusplus.ini [flplusplus], read when flplusplus.dll is enabled; Default otherwise.
    inline float FlppFloat(const char* Key, float Default)
    {
        return Enabled("flplusplus.dll") ? ConfigFloat("flplusplus.ini", "flplusplus", Key, Default) : Default;
    }

    // flplusplus' detail multipliers (lod_scale, pbubble_scale, ...): 1 unless enabled and
    // above 1, at most Max. Cache the result: the INI is read on every call.
    inline float FlppScale(const char* Key, float Max)
    {
        const float Scale = FlppFloat(Key, 1.0f);
        return Scale <= 1.0f ? 1.0f : (Scale > Max ? Max : Scale);
    }

    inline bool FlppBool(const char* Key, bool Default)
    {
        return Enabled("flplusplus.dll") && ConfigBool("flplusplus.ini", "flplusplus", Key, Default);
    }

    // FLSharp.ini [FLSharp]: every feature is on unless its key says false (FLSharp 0.9.2).
    inline bool FlSharp(const char* Feature)
    {
        return Enabled("FLSharp.dll") && ConfigBool("FLSharp.ini", "FLSharp", Feature, true);
    }

    // HD changes that came as exe patches: words of the "HDEdition.dll" line (mods/hd.ini).
    // An exe patch HD ships as a feature of the HDEdition.dll line; Discovery's patched binaries
    // carry some of the same ones, as features of its DiscoveryPatches.dll line.
    inline bool Feature(const char* Word)
    {
        return Flag("HDEdition.dll", Word) || Flag("DiscoveryPatches.dll", Word);
    }
}
