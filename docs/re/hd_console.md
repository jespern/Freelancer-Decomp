# HD Edition: console.dll (adoxa's single-player console 1.24)

Freelancer: HD Edition ships Jason Hood's `console.dll` 1.24 (7 May 2016, closed source, image
base `0x06110000`): a chat-box command console for single player with a ship store, plus line
editing and a command history for every input box. The port reproduces it in
`src/freelancer/Client/hd_console.cpp` (with `src/Server/hd_console_api.cpp` for its Server.dll
imports and patches), from the DLL's disassembly; this document records what was recovered. The
DLL's own help (`help`, `help <group>`) and `EXE/console_readme.txt` describe the commands from
the user's side; the port's deliberate differences are in [deviations.md](deviations.md)
(HD-CONSOLE-*).

## How it hooks the game

| Site | What the DLL does | Port |
|---|---|---|
| `0x005a6260` (IClient CHATMSG receive, `LAB_005a6250`) | `call 0x06116f70`: the echoed single-player chat line is run as a command (`FUN_06116df0`, the text read from the send buffer `DAT_0066d4f2`); a handler that returns true replaces the message's RDL with the console's output, anything else drops the message | the chat input's Enter runs `HdConsoleExecute` directly and never submits the line (`chat_text_input.cpp`); same output path (a chat entry) |
| `0x005b1cab` (after `[Resources]`, `FUN_005b1300`) | registers its own instance as an IDS resource module (`FUN_0057d800`); ids base `DAT_0611d964` = (module count) << 16 | `HdConsoleRegisterResources`: console.dll copied to bin/ by the mod profile and registered the same way |
| `0x005c6cb8` (import `Archetype::LoadShips`) | after the game's call, writes `%TEMP%\shipless.ini` (`[Ship] nickname = shipless_ship, DA_archetype = ships\utility\csv\csv_shield.3db`), loads it, deletes it; saves the cruise constants | `HdConsoleLoadShiplessShip` after `DataConfig::startup` |
| `0x005a811b` (import `SetMultiPlayer` in StartServer) | `FUN_06117d10`: single player applies the console's cruise constants and sets the chat target `DAT_0066d3fc` to 0; multiplayer restores the originals | `HdConsoleOnSetMultiPlayer` |
| `0x005ab231` (`CLoginMgr::SelectCharacterById`) | loads the ship store `<file>.fls` of the character, then `SelectCharacter` | `HdConsoleLoadShipStore` |
| Server.dll `0x06d4d275` (`FUN_06d4ccd0`, the save writer) | `FUN_06115840` writes `<save>.fls` (deletes it when the store is empty) | `PlayerData_SerializeToFile` -> `HdConsoleOnSaveWritten` |
| Server.dll `0x06d4a177` (`FUN_06d49f60`, character delete) | deletes `<save>.fls` | `PlayerData_DestroyCharacter` -> `HdConsoleOnSaveDeleted` |
| `0x004c3e15` (`FUN_004c3e10`, the player's ship archetype) | `jmp 0x06114450`: the shipless archetype reads as 0 except to callers `0x00448866`, `0x00448b33`, `0x0047b36f`, `0x0048146f`, `0x00481f00`, `0x004826f5`, and `0x00478e45` / `0x00479145` while the inventory's dealer is a ship dealer; the NavBar's launch tab (`0x0043f6eb`) is forced during the refresh after park / drive | `GetPlayerShipArchetypeIdRetail` / `GetPlayerShipArchetypeIdRaw` |
| `0x0043f664`, `0x0044eb74`, `0x0044f021`, `0x0044f072`, `0x0044f985` | the room script's "player" entity (the ship on its pad) remembered, hidden while shipless, shown after drive; the "Equipment" room noted | not reproduced: the shipless placeholder is an invisible model |
| `0x0048b8fe` | a JZ made a JMP while parked (`NN_MissionVendor`) | not reproduced |
| `0x00437375`, `0x0046a120`, `0x0046b650` -> `0x0046b580`, `0x0054ae68`, `0x00574b92`, `0x00574c44`, `0x00574f75` | the chat window in single player | the `HdConsoleEnabled()` branches in gameflow, chat_init, space_state, menu_enter_state |
| `0x0057ce00..0x0057ce24` (RichTextEditWin's edit-key table) | Ctrl+Left / Right / Backspace / Delete / Home / End word editing (word = alphanumerics and `_`), not with an IME box | `rich_text_win.cpp` |
| `0x0045f828 / 0x0045f830 / 0x0045f834` (ChatTextInputDisplay's Enter / Up / Down) | the history: Enter stores the line (an identical earlier one removed; in MP only lines starting with `.` or `/`), Up / Down step, Ctrl+Up / Down search by the text before the caret; MP keeps the target keys unless the line starts with `.` or `/` | `chat_text_input.cpp` |
| `0x0057baf3` | the IME box dropped with `NoIme` on the command line or a single-byte code page | n/a |
| rp8.dll `+0x9284` 0x4000 -> 0x8000, `0x0048e4d3` 96000 -> 192000, `0x0048e4e3` 72000, `0x0049d990` 12000 | bigger nav-map vertex buffers for `show` | n/a (the port's buffers are not the retail fixed sizes) |

Freelancer.exe functions in its table (`0x0611d030`): `FUN_0042ae40` LookupClientSoundArch,
`FUN_004285f0` PlayClientSoundArchIfIdle, `FUN_004347e0` GetIdsStringW (into `DAT_0066dc60`,
0x1000 characters), `FUN_00442060` NavBarEnsure, `FUN_0045a460` NN_MainButton::ClosePanel,
`FUN_0045a650` NnReputationColorArgb, `FUN_0045b2d0` NnArrivalTextShowBase, `FUN_0045b490`
NnArrivalTextShowSystemNow, `FUN_004779a0` FormatGroupedNumber, `FUN_00477ab0` NnInventoryOpenDealer,
`FUN_004bc830` LoadAutosave, `FUN_004c4910` SetPlayerBaseIdRetail, `LAB_0053c310`
SendCommonSetWeaponGroup, `FUN_005b2c70` QueueLoadGameCharacter, `DAT_0066d3fc` G_ChatInputTarget.
Its Common.dll and Server.dll imports are the `pub::` functions and types named per command below.

## Dispatch (`FUN_06116df0`)

Single player only. The first word (up to a space), lower-cased first character, is looked up in
the table at `0x0611d0c0` (sorted): the entry with the same name, else the one entry it is a
prefix of; two prefix matches, or none, drop the line silently. The handler gets the rest after
the spaces. Output is an RDL list opened with STYLE `0x3ea8` (the system-notice style); a
handler returning false shows nothing. `about`, the `Bookmarks` / `Ships` lists and `rep` reset
the list and choose their own styles (`0xe784` headings, `0x66b7` the author, TRA `(4,4)` bold
on / `(0,4)` off, TRA `(colour << 8 | 1, 0xffffff01)` a colour).

Aliases in the table: `al` autoload, `as` autosave, `l` launch, `s` system (so `a`, `l`, `s` are
not ambiguous).

Arguments: `FUN_06111090` parses one number of a comma list -- empty keeps the current value, a
`k`/`K` suffix multiplies by 1000, a number must end at a space, comma or the end;
`FUN_06112130` takes a trailing `*` (second match), `**` (third) or `*N` off a name;
`FUN_06111200` / `FUN_06111150` turn a matrix into pitch, yaw, roll in degrees rounded to two
decimals (-0.005..0 is 0, at or below -179.995 is 180).

## Commands

Messages are the DLL's string table (ids 100..148 under its base); "silent" means the handler
returns false.

| Command | Handler | Behaviour |
|---|---|---|
| `about` | `FUN_06111300` | "Console by Jason Hood (adoxa)." / "Version 1.24 (7 May, 2016)." |
| `help [topic]` | `FUN_061113b0` | `pub::Player::PopUpDialog(0, title, page, 8)`: title ids 0..6, page ids 1000..1006; the topic is a prefix of a group name, tried from `player` back to `game` (so `s` is `ship`); none is the index page |
| `autosave`, `as` | `FUN_06111530` | `pub::Save(1, 1)`; "Game saved." |
| `autoload`, `al` | `FUN_06111560` | `FUN_004bc830`; silent |
| `save [slot\|desc]` | `FUN_06111660` / `FUN_06111570` | one character: `qsave<SLOT>.fl` with SLOT upper-cased (`:` -> `?`), listed "Quick Save <SLOT>"; none: `qsave.fl`, "Quick Save"; longer: `<CreateID in hex>.fl`, listed as the text with the separating space in front; `pub::Save - 0x127a0` (FUN_06d4c800, PlayerData_SaveGame); "Game saved." / "Game NOT saved." by its result |
| `load [slot\|desc]` | `FUN_061116c0` | the same name; "Game not found." unless `<user data>\Accts\SinglePlayer\<file>` exists, else `FUN_005b2c70(file, 1)`; silent |
| `play sound` | `FUN_06116da0` | `FUN_0042ae40(CreateID)`; none: "Sound not found."; else `FUN_004285f0(arch, 0, 0)`, silent |
| `launch`, `l` | `FUN_06113b40` | in a base (`pub::Player::GetBase`): the NavBar's launch click (`FUN_00440f94`); silent |
| `park` | `FUN_06114530` | docked, not shipless (`FUN_06113f40`: base, `pub::Player::GetShipID`, the equipment list `Players+0x278`, the weapon groups `Players+0x380`); "Repair damage." unless `FUN_06113ee0` (hull `+0x268`, every collision group and item at 1.0); then the record is stored, `SetShipAndLoadout(1, shipless_ship, empty)`, `0x0048b8fe` patched, `FUN_0045a460`, the NavBar rebuilt with the launch tab forced off; "Parked <ship>." |
| `drive [ship]` | `FUN_06114780` | docked, store not empty; `FUN_06113de0` picks the record: a prefix of the ship's name at this base (`*N`), no name = the only ship here, else the list; "Ship not found." / "No ships here."; a real current ship needs "Repair damage." and is parked first ("Parked X." then a paragraph); `FUN_06114150`: `SetShipAndLoadout`, weapon groups to the server and client (`LAB_0053c310`); the driven ship's name; record removed, `0x0048b8fe` restored, `FUN_0045a460` |
| `sell ship` | `FUN_061154e0` | docked, store not empty; no name lists this base's ships; the record as `drive`; "Repair damage."; the stored ship is driven, `pub::Player::GetAssetValue` added to the cash (`AdjustCash`), the previous ship driven again (or shipless restored); "Sold <ship> for <amount>." (`FUN_004779a0`) |
| `hitch [base]` | `FUN_06115770` | docked; a real ship is parked; with a base, `base`; else the one other base holding ships -> the in-place base change (`FUN_06112fc0`), silent; none or several: "Specify base." |
| `ships` | `FUN_06113dc0` / `FUN_06113c90` | store empty: silent; else heading "Ships" and "<ship> (<base>)" per record, "None" (ids 3022) when the filter leaves none |
| `xfer [ship]` | `FUN_06115160` | docked, store not empty; shipless: no name lists, a name "Drive a ship."; "Repair damage."; the record as `drive`; then the dealer window (`FUN_00477ab0(3, 0x06114d20, &record, 0)`) over the client market rewired by ~30 byte patches (`0x00437fb5`..`0x00483ad0`: titles ids 141 / 142, prices 0, stock = the stored ship's items, nanobot / battery limits) so buying takes from the stored ship and selling puts into it; the result hook `0x06114d20` updates the record and restores everything -- not reproduced |
| `jump z, x, y` | `FUN_061123d0` | in space; `pos + x * col0 + y * col1 - z * col2` of the ship's orientation; bookmark 0; `pub::SpaceObj::Relocate`; silent |
| `jump [object]` | `FUN_061123d0` | none: `pub::SpaceObj::GetTarget` (none: silent); a nickname (`pub::SpaceObj::GetSystem` succeeds) or `FUN_06112260` ("Object not found."). Radius = max(`GetRadius`, `GetBurnRadius`); a non-server target: the client target's position, radius 0.3121905. By `pub::SpaceObj::GetType`: a station whose solar archetype is `docking_fixture`: behind it (`+R * col2`), its orientation; docking ring (0x20): the same plus 98 along its col0; jump gate (0x40): `-1.5R * col2`, its orientation; trade lane ring (0x80): the nearer side, facing along the lane (rows 0 and 2 negated for the far side); anything else: on the object-ship line at R (2R for a moon / planet / sun unless already between R+2 and 2R+2), `LookMatrixYup` at the object; a station also tests `IsPosEmpty(pos + half ship radius * col2, half - 1)` and, when full, goes `2R` through and turns round. Silent |
| `pos` | `FUN_06111920` | in space: "pos = x, y, z" |
| `pos copy` | | "<system nickname> x, y, z, p, y, r" to the clipboard (CF_TEXT); "Position copied." / "Clipboard unavailable." |
| `pos N`, `pos clip` | | handed to `system` |
| `pos =` | | "Bookmarks" then "N = <system name>, x, y, z" per set bookmark, "None" when none |
| `pos =N` | | `FUN_06111800(N)`; "Bookmark set." |
| `pos[r] [x, y, z [, p, y, r]]` | `FUN_06111920` / `FUN_06111d90` | absolute (empty fields keep the current) / relative (empty fields 0); a rotation given re-makes the matrix (`EulerMatrix`); bookmark 0, Relocate; "pos = ..." and, with a rotation, a paragraph "rotate = p, y, r" |
| `rot[r] [p, y, r]` | `FUN_06111f50` / `FUN_06112020` | the same for the orientation; "rotate = ..." |
| `base` | `FUN_06113000` | docked: the base nickname; in space: the previous base's name (set by this command), else silent |
| `base .` | | docked: remembers this base; in space: needs a previous one |
| `base base` | | the nickname, then a case-sensitive substring of a base name whose nickname does not start with `intro` (`*N`); "Base not found."; docked: `FUN_06112fc0` -- `FUN_0043b3e0`, `FUN_0043b290(base)`, `FUN_004c4910(&base)`, `FUN_0045b2d0(2.0)`; in space: bookmark 0, `pub::Player::ForceLand`; silent |
| `system` | `FUN_061134d0` | the current system's nickname |
| `system N`, `system clip` | | bookmark N ("Bookmark not set.") / the clipboard text parsed as `system <...>` ("Clipboard unavailable.") |
| `system sys [object \| x, y, z [, p, y, r]]` | | `.` is the current system, else the nickname then a substring of the name ("System not found."). In the current system, a bookmark or clipboard position is a move. Otherwise a switch: coordinates (empty fields keep the ship's; the orientation kept in space, identity from a base), an object (`FUN_06112260` in that system), or nothing (`DAT_0611d9e4`: the ship's own position). In space: `DAT_0067977c` = system (the client switches as after a jump). From a base: the launch (`FUN_06113b40`) with `Players+0x340` set, the `CShip::launch` hook at `0x062b2a81` setting `DAT_0067977c` as the ship launches (and, with nothing given, `FUN_06113270` copying the ship's launch position in, after `CEqObj::launch_pos` took the `HpDockPoint` for the `HpDockMount` hardpoint through the hook at `0x062aa9df`). Server.dll `SystemSwitchOutComplete` (`0x06cef600`) is patched for the one switch: `0x06cef6d7` JE -> JMP `0x06cef719` where `MOV EBX,<system>` replaces `MOV EAX,[EDI+0x1b8]`; with an object the arrival call `0x06cef85d` becomes `0x061133a0` (its standoff: moon / planet / sun 2R, docking ring 98 along col0, jump gate 1.5R turned round, all `+R * col2`), else the default position / orientation immediates `0x06cef8b0..0x06cef908` are the given ones; `0x06cef866` skips the FDUMP; `0x06cef988` `XOR EAX,EAX` for the missing arrival id; the autosave call `0x06cef9c8` becomes the unpatch stub, which tail-calls `FUN_0045b490` (the system caption). Silent |
| `cspd [speed]` | `FUN_06112b80` | `EngineEquipConsts::CRUISING_SPEED`, `PhySys::ANOM_LIMITS_MAX_VELOCITY` raised to 1.5x; "Cruise speed = %g m/s" |
| `cacc [time]` | `FUN_06112bf0` | `EngineEquipConsts::CRUISE_ACCEL_TIME`; "Cruise accel time = %gs" |
| `anim [[>\|<]wings\|script]` | `FUN_06112d40` | needs the player's CShip. None / `<` / `>` alone: `close_bay` when `+0x2ac` > 1, else `open_bay` with `0x062b2ba1` (its shield loop test) a JMP; a running animation is reversed (`EvalPlayback(h, reverse, -2.0, 1.0, 0, 1.0, 1.0, 0)`, `AnimDB::Add`); else `IAnimation2::PlayScript(archetype +0x44, ship +0x04, name)` (`wings` = `Sc_extend wing`), "Animation not found." for -1; `EvalPlayback(h, dir<0, dir<0 ? -1.0 : -2.0, ...)`, `AnimDB::Add`, and a thread (`LAB_06112c50`, 100 ms) that ends it (`AnimDB::Rem`, `StopPlayback`, `DestroyScript`) when stored >= script duration (forwards, explicit) or <= 0 (backwards), or leaves state 2 for the next command to reverse; the thread exits when the ship changes |
| `ghost` | `FUN_06113ac0` | `DAT_00679cc4 ^= 1` (game option 0x37 DISABLE_COLLIDE); "Ghost: on/off" |
| `godmode` | `FUN_06113b00` | `DAT_0067ecc0 ^= 1` (the `+G` flag, applied by `CLoginMgr::Login`); "Godmode: on/off" |
| `ntb [on\|off\|sight]` | `FUN_06115ed0` | `off` (prefix, more than one letter): `0x004ec087` JLE -> JMP (no brackets); `on`: restored, `DAT_005d954c` = original (0.0004); `sight` (prefix): restored, `DAT_005d954c` = 1.0; other text silent; "NTB: off/on/sight" |
| `show` | `FUN_06115fc0` | `0x004c4df1` <-> `MOV ECX,[ESP+8]; OR BYTE PTR [ECX],1; RET` (every visit flag carries bit 1); "Show: on/off" |
| `show houses` | | `0x0049e799` `8b8e` <-> `eb4e` (the universe map's House labels); "House labels: off/on" |
| `show paths` | | `0x0049cd34` `0f85` <-> `90e9` (the nav map's patrol-path band filter skipped); "Patrol Paths: unfiltered/filtered"; other text silent |
| `zoom [time]` | `FUN_06116110` | `0x004944ac` and `0x0049f2a7` (the nav map zoom times), 0 becomes 0.001; "Zoom time = %g" |
| `cash [[+\|-]amount]` | `FUN_06116170` | `wcstol`, `k` x1000, `+`/`-` relative else absolute (`InspectCash`, `AdjustCash`); the cash as `FUN_004779a0` prints it |
| `rep faction [value]` | `FUN_06116680` | `FUN_06116230`: a single letter b / k / l / r is the House police group; else `GetReputationGroup`, else a prefix of a short name (groups with info card 66205 skipped, except 25320; `*N`); "Faction not found."; a value (`FUN_06116470`: H -0.9, h -0.6, nh -0.2, n 0, nf 0.2, f 0.6, F 0.9, or a number, a percentage beyond +-1, clamped) is set; output "<short name>: %g" in `FUN_0045a650`'s colour |
| `rep = [faction\|value]` | | first use saves every group's reputation ("Rep saved."); a value sets all ("Reputation: %g" in its colour); a faction copies its reputation to all (the short name in `DAT_00679bac`) |
| `rep undo` | | restores the saved reputations ("Rep loaded."); silent when none |
| `monkey` / `robot` / `trent` | `FUN_06116c80..` | `pub::Player::SetMonkey / SetRobot / SetTrent(1)`; "Monkey." / "Robot." / "Trent." |
| `costume character` | `FUN_06116d10` | `CostumeDescriptions::find_costume(CreateID)`: "Costume not found."; else `get_costume` into `Players+0x2e4`; the name with its first letter upper-cased and a full stop |

## The ship store file

`<save>.fls` beside each `.fl`, INI text, one `[Player]` block per stored ship:

```
[Player]
base = <base nickname id, decimal>
ship_archetype = <ship archetype id, decimal>
equip = <archetype>, <hardpoint or empty for internal>, <health>
cargo = <archetype>, <count>, , [<health when not 1>], <mission flag>

wg = ...            (the weapon-group lines as the client sent them, verbatim)
```

Reading (`FUN_06115a80`) takes `base` / `ship_archetype` as a number or a nickname, `equip` and
`cargo` through `Loadout::ReadEquipLine` / `ReadCargoLine`, and keeps every `wg` line. The
placeholder archetype `shipless_ship` (an invisible `csv_shield.3db`) is what a parked player
flies; it has no `.fls` entry.
