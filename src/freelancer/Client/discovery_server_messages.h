// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements the client side of Discovery Freelancer's server-plugin messages
// (https://discoverygc.com) in the engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// Discovery's FLHook plugins (base.dll "PlayerBase", mobiledock.dll, cloak.dll, playercntl.dll,
// alley.dll; github.com/DiscoveryGC/FLHook) talk to the client through ordinary chat messages:
// HkFMsg(client, L"<TEXT> SetMarketOverride 1 2 3 4 5</TEXT>") -- an RDL whose text is a space,
// a verb and its arguments. Discovery's client DLL directplay8.dll (EXE/dacom.ini [Libraries])
// takes these out of the chat and acts on them; a client without it shows them as text. This is
// that DLL's job, built into the port and on when the game folder's [Libraries] names it.
//
// The verbs, as the DLL lists them, and which plugin of the public FLHook sends each:
//
//   ResetMarketOverride                      base.dll / mobiledock.dll: forget the market override
//   SetMarketOverride <base> <good> <price> <demand_only> <quantity>
//                                            base.dll: one good of the player base's market, at
//                                            the system's proxy base (<system>_proxy_base); the
//                                            dummy "0 0 0 0" marks an empty market
//   SetBaseInfoText2 <RDL xml>               base.dll / mobiledock.dll: the base status shown by
//                                            "Current Information" while docked ("" clears it)
//   SetBaseInfoText <text>                   older variant, no public sender; kept like the above
//   ChangeSys <system id>                    playercntl.dll (jump drives), alley.dll: switch the
//                                            ship to that system -- the client loads it and
//                                            sends SystemSwitchOutComplete, which the plugin
//                                            answers with the position; base.dll uses it to
//                                            launch a ship whose base is gone
//   Cloaking device on / off, InitCloakInfo <range>
//                                            cloak.dll, for a HUD indicator; the visible text
//                                            follows as a plain message
//   OverrideJumpObject <object> <system>     playercntl.dll; no handler in Discovery's client DLL
//   Coll, Lights on/off, SetSystemEntryText, SetBaseEntryText, SetOverrideSystem, AddCloakAdminId,
//   RemCloakAdminId, SolarInfo, Freeze, Unfreeze, RaceMode
//                                            in the DLL's list, no public sender
//
// Every one of them is taken out of the chat; the market, base info and system switch ones are
// acted on, the rest only logged (FLWIN_MP_TRACE).
#pragma once

#include "client_base_market.h" // FClientMarket

namespace DiscoveryServerMessages
{
    // The game folder names directplay8.dll in [Libraries].
    bool Enabled();

    // An incoming chat message's RDL buffer (ChatDispatchIncoming). True when it was a server
    // plugin command: it has been handled and is not to be shown.
    bool HandleChat(const void* Buffer, unsigned SizeBytes);

    // Lays the market override over a freshly filled client market of BaseId (ClientMarketFill):
    // every good the override does not list is neither bought nor sold, the listed ones take
    // the player base's price, stock and direction.
    void ApplyMarketOverride(unsigned BaseId, FClientMarket* Market);

    // The base status text for the "Current Information" window while docked, or null when the
    // server has set none.
    const wchar_t* BaseInfoText();
}
