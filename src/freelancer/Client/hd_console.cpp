// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file reimplements plugins of Freelancer: HD Edition (https://github.com/FLHDE) in the
// engine, for the macOS port (https://github.com/jespern/freelancer-mac).
// Author: jespern.

// HD: console.dll's single-player commands (hd_console.h).

#include "hd_console.h"

#include "chat_init.h"
#include "ids_rdl.h"
#include "player_spawn.h" // GetPlayerConnectionId (DAT_00673344)

#include "common_misc.h" // SinglePlayer
#include "common_text.h"
#include "common_universe.h"
#include "hd_console_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <windows.h>

namespace
{
    // Server.dll's pub:: calls, resolved once (the client does not link Server).
    FHdConsoleServerApi const* ServerApi()
    {
        static FHdConsoleServerApi const* Api = nullptr;
        if (Api == nullptr)
        {
            HMODULE const Server = GetModuleHandleA("server.dll");
            typedef FHdConsoleServerApi const* (*FGetApi)();
            FGetApi const Get = Server != nullptr
                                    ? reinterpret_cast<FGetApi>(GetProcAddress(Server, "HdConsoleServerApi"))
                                    : nullptr;
            Api = Get != nullptr ? Get() : nullptr;
        }
        return Api;
    }

    void Print(char const* Format, ...)
    {
        char Narrow[512];
        va_list Args;
        va_start(Args, Format);
        vsnprintf(Narrow, sizeof(Narrow), Format, Args);
        va_end(Args);
        wchar_t Wide[512];
        int Length = 0;
        for (; Narrow[Length] != '\0' && Length < 511; ++Length)
            Wide[Length] = static_cast<wchar_t>(static_cast<unsigned char>(Narrow[Length]));
        Wide[Length] = 0;
        FChatMessageEntry* const Entry = ChatAcquireEntry();
        TextNode Text(Wide, -1);
        RdlAppendNode(Entry->Body, Text.Clone());
        ChatCommitEntries();
    }

    bool PlayerShip(unsigned* OutShip)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        *OutShip = 0;
        if (Api == nullptr || Api->GetShip(GetPlayerConnectionId(), OutShip) != 0 || *OutShip == 0)
        {
            Print("Not in space.");
            return false;
        }
        return true;
    }

    // A reputation value: -1..1, a percentage, or HD's letters H h nh n nf f F.
    bool ParseRep(char const* Text, float* Out)
    {
        static struct { char const* Word; float Value; } const kWords[] = {
            {"H", -0.9f}, {"h", -0.6f}, {"nh", -0.2f}, {"n", 0.0f}, {"nf", 0.2f}, {"f", 0.6f}, {"F", 0.9f}};
        for (auto const& Word : kWords)
            if (strcmp(Text, Word.Word) == 0)
            {
                *Out = Word.Value;
                return true;
            }
        char* End = nullptr;
        double Value = strtod(Text, &End);
        if (End == Text)
            return false;
        if (*End == '%')
            Value /= 100.0;
        if (Value < -1.0)
            Value = -1.0;
        if (Value > 1.0)
            Value = 1.0;
        *Out = static_cast<float>(Value);
        return true;
    }

    typedef void (*FCommand)(std::vector<std::string> const& Args, std::string const& Rest);

    void CmdHelp(std::vector<std::string> const&, std::string const&);

    void CmdCash(std::vector<std::string> const& Args, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        unsigned const Player = GetPlayerConnectionId();
        int Cash = 0;
        Api->InspectCash(Player, &Cash);
        if (!Args.empty())
        {
            char const* const Text = Args[0].c_str();
            long const Amount = strtol(Text, nullptr, 10);
            int const Delta = (Text[0] == '+' || Text[0] == '-') ? static_cast<int>(Amount)
                                                                 : static_cast<int>(Amount) - Cash;
            Api->AdjustCash(Player, Delta);
            Api->InspectCash(Player, &Cash);
        }
        Print("Cash: %d", Cash);
    }

    void CmdRep(std::vector<std::string> const& Args, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        if (Api == nullptr)
            return;
        if (Args.empty())
        {
            Print("rep faction [value]");
            return;
        }
        int Vibe = 0;
        if (Api->GetPlayerVibe(GetPlayerConnectionId(), &Vibe) != 0)
            return;
        unsigned Group = 0;
        std::string Nickname = Args[0];
        if (Api->GetGroup(Nickname.c_str(), &Group) != 0 || Group == 0)
        {
            Nickname += "_grp";
            if (Api->GetGroup(Nickname.c_str(), &Group) != 0 || Group == 0)
            {
                Print("No faction %s.", Args[0].c_str());
                return;
            }
        }
        float Value = 0.0f;
        if (Args.size() > 1)
        {
            if (!ParseRep(Args[1].c_str(), &Value))
            {
                Print("Bad value %s.", Args[1].c_str());
                return;
            }
            Api->SetRep(Vibe, Group, Value);
        }
        Api->GetRep(Vibe, Group, &Value);
        Print("%s: %.2f", Nickname.c_str(), Value);
    }

    void CmdPos(std::vector<std::string> const&, std::string const& Rest)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        Vector Position;
        Matrix Orientation;
        Api->GetLocation(Ship, &Position, &Orientation);
        float X, Y, Z;
        if (sscanf(Rest.c_str(), " %f , %f , %f", &X, &Y, &Z) == 3)
        {
            unsigned System = 0;
            Api->GetSystem(Ship, &System);
            Position.x = X;
            Position.y = Y;
            Position.z = Z;
            Api->Relocate(Ship, System, &Position, &Orientation);
        }
        Print("Position: %.0f, %.0f, %.0f", Position.x, Position.y, Position.z);
    }

    void CmdSystem(std::vector<std::string> const&, std::string const&)
    {
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        unsigned System = 0;
        Api->GetSystem(Ship, &System);
        Universe::ISystem const* const Info = Universe::get_system(System);
        Print("System: %s", Info != nullptr ? Info->GetNickname() : "?");
    }

    void CmdGodmode(std::vector<std::string> const&, std::string const&)
    {
        static bool bGod = false;
        FHdConsoleServerApi const* const Api = ServerApi();
        unsigned Ship = 0;
        if (Api == nullptr || !PlayerShip(&Ship))
            return;
        bGod = !bGod;
        Api->SetInvincible(Ship, bGod);
        Print("God mode %s.", bGod ? "on" : "off");
    }

    void CmdTime(std::vector<std::string> const&, std::string const&)
    {
        time_t const Now = time(nullptr);
        char Text[64];
        strftime(Text, sizeof(Text), "%Y-%m-%d %H:%M:%S", localtime(&Now));
        Print("%s", Text);
    }

    void CmdCoin(std::vector<std::string> const&, std::string const&)
    {
        Print("%s", (rand() & 1) != 0 ? "Heads" : "Tails");
    }

    void CmdDice(std::vector<std::string> const& Args, std::string const&)
    {
        int const Sides = !Args.empty() ? atoi(Args[0].c_str()) : 6;
        Print("%d", Sides > 0 ? 1 + rand() % Sides : 0);
    }

    void CmdPing(std::vector<std::string> const&, std::string const&)
    {
        Print("Pong!");
    }

    void CmdPrint(std::vector<std::string> const&, std::string const& Rest)
    {
        Print("%s", Rest.c_str());
    }

    void CmdAbout(std::vector<std::string> const&, std::string const&)
    {
        Print("Console: Freelancer: HD Edition's console.dll, a first subset in the port.");
    }

    struct FCommandEntry
    {
        char const* Name;
        FCommand Run;
        char const* Usage;
    };

    FCommandEntry const kCommands[] = {
        {"help", CmdHelp, "help -- this list"},
        {"commands", CmdHelp, "commands -- this list"},
        {"cash", CmdCash, "cash [[+|-]amount]"},
        {"rep", CmdRep, "rep faction [value|H|h|nh|n|nf|f|F]"},
        {"pos", CmdPos, "pos [x,y,z]"},
        {"system", CmdSystem, "system"},
        {"godmode", CmdGodmode, "godmode -- toggle invincibility"},
        {"time", CmdTime, "time"},
        {"coin", CmdCoin, "coin"},
        {"dice", CmdDice, "dice [sides]"},
        {"ping", CmdPing, "ping"},
        {"print", CmdPrint, "print text"},
        {"say", CmdPrint, "say text"},
        {"about", CmdAbout, nullptr},
    };

    void CmdHelp(std::vector<std::string> const&, std::string const&)
    {
        for (FCommandEntry const& Command : kCommands)
            if (Command.Usage != nullptr)
                Print("%s", Command.Usage);
    }
}

void HdConsoleExecute(wchar_t const* Line)
{
    // Commands are ASCII; any longer prefix that names one command selects it.
    std::string Text;
    for (wchar_t const* P = Line; *P != 0; ++P)
        Text += static_cast<char>(*P < 0x80 ? *P : '?');
    size_t const Start = Text.find_first_not_of(" \t");
    if (Start == std::string::npos)
        return;
    Text = Text.substr(Start);
    size_t const NameEnd = Text.find_first_of(" \t");
    std::string const Name = Text.substr(0, NameEnd);
    std::string const Rest = NameEnd == std::string::npos ? std::string() : Text.substr(NameEnd + 1);
    std::vector<std::string> Args;
    for (size_t Pos = 0; Pos < Rest.size();)
    {
        size_t const From = Rest.find_first_not_of(" \t", Pos);
        if (From == std::string::npos)
            break;
        size_t const To = Rest.find_first_of(" \t", From);
        Args.push_back(Rest.substr(From, To == std::string::npos ? std::string::npos : To - From));
        Pos = To == std::string::npos ? Rest.size() : To;
    }

    FCommandEntry const* Match = nullptr;
    int Matches = 0;
    for (FCommandEntry const& Command : kCommands)
    {
        if (_stricmp(Command.Name, Name.c_str()) == 0)
        {
            Match = &Command;
            Matches = 1;
            break;
        }
        if (_strnicmp(Command.Name, Name.c_str(), Name.size()) == 0)
        {
            Match = &Command;
            ++Matches;
        }
    }
    if (Matches != 1)
    {
        Print(Matches == 0 ? "Unknown command: %s (try help)" : "Ambiguous command: %s", Name.c_str());
        return;
    }
    Match->Run(Args, Rest);
}
