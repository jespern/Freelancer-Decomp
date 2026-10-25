// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements the client side of Discovery Freelancer's server-plugin messages
// (https://discoverygc.com) in the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

#include "discovery_server_messages.h"
#include "hd_plugins.h"

#include "menu_universe_base.h" // GetClientLocationCurrentBase, TryGetClientLocationBaseMarket,
                                // RefillClientLocationBaseMarket
#include "nn_inventory.h"       // IsPlayerDockedRetail
#include "space_state.h"        // SetPendingJumpSystem

#include "common_goods.h"       // MarketGoodInfo, TransactionType
#include "common_text.h"        // BinaryRDLReader

#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <wchar.h>

namespace
{
    bool TraceOn()
    {
        static bool const bOn = getenv("FLWIN_MP_TRACE") != nullptr;
        return bOn;
    }

    // The verbs directplay8.dll knows (its string table), plus playercntl's OverrideJumpObject,
    // which no client DLL of Discovery's handles but is a command all the same.
    const wchar_t* const kVerbs[] = {
        L"Coll",         L"Lights",             L"Cloaking",           L"ChangeSys",
        L"SetSystemEntryText", L"SetBaseEntryText", L"SetBaseInfoText", L"SetBaseInfoText2",
        L"SetOverrideSystem",  L"SetMarketOverride", L"ResetMarketOverride", L"InitCloakInfo",
        L"AddCloakAdminId",    L"RemCloakAdminId",   L"SolarInfo",        L"Freeze",
        L"Unfreeze",           L"RaceMode",          L"OverrideJumpObject",
    };

    // One good of the player base's market, as SendMarketGoodSync / SendMarketGoodUpdated
    // (base_plugin/ClientCommands.cpp) describe it: " SetMarketOverride %u %u %f %u %u" = the proxy
    // base, the good, the price, 1 when the base only buys it (none in stock, or at its minimum
    // stock), else 0 and the quantity for sale.
    struct FOverrideGood
    {
        float Price;
        bool bDemandOnly;
        int Quantity;
    };

    struct FMarketOverride
    {
        bool bActive = false;  // a SetMarketOverride since the last reset (the dummy counts)
        unsigned BaseId = 0;   // the proxy base of the last SetMarketOverride, 0 = any
        std::map<unsigned, FOverrideGood> Goods;
    };

    FMarketOverride G_Override;
    std::wstring G_BaseInfoText;  // SetBaseInfoText2
    std::wstring G_BaseInfoText1; // SetBaseInfoText

    void ApplyGood(FClientMarket* Market, unsigned GoodId, FOverrideGood const& Good)
    {
        FClientMarket::iterator It = Market->find(GoodId);
        if (It == Market->end())
        {
            FClientMarketGood Entry;
            Entry.Info.GoodId = GoodId;
            Entry.Info.Price = Good.Price;
            Entry.Info.MinInventory = 0;
            Entry.Info.MaxInventory = 0;
            Entry.Info.Type = TransactionBuyAndSell;
            Entry.Info.MinLevel = 0.0f;
            Entry.Info.MinRep = -1.0f;
            Entry.Stock = 0;
            Entry.PriceScale = 1.0f;
            Entry.Field24 = 0;
            It = Market->insert(std::make_pair(GoodId, Entry)).first;
        }
        FClientMarketGood& Entry = It->second;
        Entry.Info.Price = Good.Price;
        Entry.PriceScale = 1.0f;
        // "NB: If price is 0 it will not be shown at all" (the plugin's own note).
        if (Good.Price <= 0.0f)
        {
            Entry.Stock = 0;
            Entry.Info.Type = TransactionSaleOnly; // nothing to sell: neither side
        }
        else if (Good.bDemandOnly)
        {
            Entry.Stock = 0;
            Entry.Info.Type = TransactionDemandOnly;
        }
        else
        {
            Entry.Stock = Good.Quantity > 0 ? Good.Quantity : 0;
            Entry.Info.Type = TransactionBuyAndSell;
        }
    }

    // The live market of the base the player is docked at, when it has been loaded.
    FClientMarket* LiveMarket()
    {
        unsigned const BaseId = GetClientLocationCurrentBase();
        return BaseId != 0 ? TryGetClientLocationBaseMarket(BaseId) : nullptr;
    }

    // ---- the verbs ------------------------------------------------------------------------------

    void ResetMarketOverride()
    {
        bool const bWasActive = G_Override.bActive;
        G_Override = FMarketOverride();
        // Back to the base's own market (the data's), when one is loaded under an override.
        if (bWasActive)
            RefillClientLocationBaseMarket(GetClientLocationCurrentBase());
    }

    void SetMarketOverride(const wchar_t* Args)
    {
        wchar_t* End = nullptr;
        unsigned const BaseId = static_cast<unsigned>(wcstoul(Args, &End, 10));
        unsigned const GoodId = static_cast<unsigned>(wcstoul(End, &End, 10));
        float const Price = static_cast<float>(wcstod(End, &End));
        unsigned const Flag = static_cast<unsigned>(wcstoul(End, &End, 10));
        int const Quantity = static_cast<int>(wcstol(End, &End, 10));

        bool const bFirst = !G_Override.bActive;
        G_Override.bActive = true;
        if (BaseId != 0)
            G_Override.BaseId = BaseId;
        if (GoodId == 0)
        {
            // The dummy " SetMarketOverride 0 0 0 0": an override with no goods.
            if (bFirst)
                if (FClientMarket* const Market = LiveMarket())
                    DiscoveryServerMessages::ApplyMarketOverride(GetClientLocationCurrentBase(), Market);
            return;
        }

        FOverrideGood Good;
        Good.Price = Price;
        Good.bDemandOnly = Flag != 0;
        Good.Quantity = Quantity;
        G_Override.Goods[GoodId] = Good;

        if (FClientMarket* const Market = LiveMarket())
        {
            if (bFirst)
                DiscoveryServerMessages::ApplyMarketOverride(GetClientLocationCurrentBase(), Market);
            else if (G_Override.BaseId == 0 || G_Override.BaseId == GetClientLocationCurrentBase())
                ApplyGood(Market, GoodId, Good);
        }
    }

    void ChangeSys(const wchar_t* Args)
    {
        unsigned const SystemId = static_cast<unsigned>(wcstoul(Args, nullptr, 10));
        if (SystemId == 0)
            return;
        // In space: the jump the server asked for, without a gate -- the space frame loads the
        // system and reports SystemSwitchOutComplete, as after a jump hole (space_state.cpp).
        // Docked, there is no ship to switch; the plugin's ForceLaunch case is left alone.
        if (!IsPlayerDockedRetail())
            SetPendingJumpSystem(&SystemId, false);
        else if (TraceOn())
            fprintf(stderr, "[mp] server command ChangeSys %u while docked: ignored\n", SystemId);
    }
}

namespace DiscoveryServerMessages
{
    bool Enabled()
    {
        static bool const bOn = HdPlugins::Enabled("directplay8.dll");
        return bOn;
    }

    bool HandleChat(const void* Buffer, unsigned SizeBytes)
    {
        if (!Enabled() || Buffer == nullptr || SizeBytes < 8)
            return false;

        // The message's text: a command is a space, a verb and its arguments, and nothing else.
        std::vector<wchar_t> Text(SizeBytes / 2 + 2, L'\0');
        unsigned OutSize = 0;
        BinaryRDLReader Reader;
        if (!Reader.extract_text_from_buffer(&Text[0], static_cast<unsigned>(Text.size() - 1),
                                             OutSize, static_cast<const char*>(Buffer), SizeBytes))
            return false;
        const wchar_t* const Message = &Text[0];
        if (Message[0] != L' ' || Message[1] == L'\0' || Message[1] == L' ')
            return false;

        const wchar_t* const Verb = Message + 1;
        size_t VerbLength = 0;
        while (Verb[VerbLength] != L'\0' && Verb[VerbLength] != L' ')
            ++VerbLength;
        bool bKnown = false;
        for (const wchar_t* Known : kVerbs)
        {
            if (wcslen(Known) == VerbLength && wcsncmp(Known, Verb, VerbLength) == 0)
            {
                bKnown = true;
                break;
            }
        }
        if (!bKnown)
            return false;
        const wchar_t* Args = Verb + VerbLength;
        if (*Args == L' ')
            ++Args;

        if (TraceOn())
        {
            // The base status runs to kilobytes; the log gets the head of it.
            fprintf(stderr, "[mp] server command:%.120ls%s\n", Message,
                    wcslen(Message) > 120 ? " ..." : "");
        }

        if (wcsncmp(Verb, L"ResetMarketOverride", VerbLength) == 0)
            ResetMarketOverride();
        else if (wcsncmp(Verb, L"SetMarketOverride", VerbLength) == 0)
            SetMarketOverride(Args);
        else if (wcsncmp(Verb, L"SetBaseInfoText2", VerbLength) == 0)
            G_BaseInfoText = Args;
        else if (wcsncmp(Verb, L"SetBaseInfoText", VerbLength) == 0)
            G_BaseInfoText1 = Args;
        else if (wcsncmp(Verb, L"ChangeSys", VerbLength) == 0)
            ChangeSys(Args);
        // The rest (the cloak indicator, and the verbs nothing public sends) are only swallowed:
        // Discovery's client shows none of them as text either.
        return true;
    }

    void ApplyMarketOverride(unsigned BaseId, FClientMarket* Market)
    {
        if (!Enabled() || Market == nullptr || !G_Override.bActive)
            return;
        if (G_Override.BaseId != 0 && BaseId != 0 && G_Override.BaseId != BaseId)
            return;
        // A player base trades only what its market lists (base_plugin GFGoodBuy / GFGoodSell:
        // "Base will not accept goods"): everything else is off both lists.
        for (FClientMarket::iterator It = Market->begin(); It != Market->end(); ++It)
        {
            It->second.Stock = 0;
            It->second.Info.Type = TransactionSaleOnly;
        }
        for (std::map<unsigned, FOverrideGood>::const_iterator It = G_Override.Goods.begin();
             It != G_Override.Goods.end(); ++It)
            ApplyGood(Market, It->first, It->second);
        if (TraceOn())
            fprintf(stderr, "[mp] market override applied to base %08x: %u good(s)\n", BaseId,
                    static_cast<unsigned>(G_Override.Goods.size()));
    }

    const wchar_t* BaseInfoText()
    {
        if (!Enabled())
            return nullptr;
        if (!G_BaseInfoText.empty())
            return G_BaseInfoText.c_str();
        if (!G_BaseInfoText1.empty())
            return G_BaseInfoText1.c_str();
        return nullptr;
    }
}
