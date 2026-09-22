# BindGuard — Documentation

**Every Action Bound, On Every Device.**

Online: <https://wiki.teufel-engineering.com/en/BindGuard/documentation>
Support: <mailto:teufelsilvan@gmail.com>

Version 1.0.0 · Unreal Engine 5.8 · Win64 · Source included

---

## Contents

1. [What BindGuard is](#1-what-bindguard-is)
2. [Supported engine and platforms](#2-supported-engine-and-platforms)
3. [Installation](#3-installation)
4. [Quick start — five minutes](#4-quick-start--five-minutes)
5. [The demo map](#5-the-demo-map)
6. [The five checks](#6-the-five-checks)
7. [What the plugin **cannot** know — static against observed](#7-what-the-plugin-cannot-know--static-against-observed)
8. [Using the exemption list properly](#8-using-the-exemption-list-properly)
9. [The report](#9-the-report)
10. [The gate, and the JSON](#10-the-gate-and-the-json)
11. [Console commands](#11-console-commands)
12. [Project settings reference](#12-project-settings-reference)
13. [Class overview](#13-class-overview)
14. [Blueprint API](#14-blueprint-api)
15. [C++ API and recipes](#15-c-api-and-recipes)
16. [Architecture](#16-architecture)
17. [Tests](#17-tests)
18. [Troubleshooting](#18-troubleshooting)
19. [Limits and known edges](#19-limits-and-known-edges)

---

## 1. What BindGuard is

A checker for Enhanced Input.

Enhanced Input is a good system for *managing* bindings and it does not check them, because checking is
not its job. The truth about an action is spread over at least three places: the action itself is a
`UInputAction` asset, its keys live in one or more `UInputMappingContext` assets, and whether that
context is ever active is a decision in Blueprint logic somewhere. Nobody can hold those three levels in
their head across a real project, which is why "this action only exists on the keyboard" is usually
discovered by a certification reviewer, or by a player with a controller.

BindGuard answers five questions with names rather than numbers, draws them on screen, and returns an
exit code a build server can act on.

It never changes your input assets during a check. The only functions that write anything are the two
explicit fix helpers described in [§14](#14-blueprint-api), and they only run when you call them.

---

## 2. Supported engine and platforms

| | |
| --- | --- |
| **Engine version** | Unreal Engine **5.8** (`"EngineVersion": "5.8.0"`) |
| **Supported development platforms** | Windows (Win64) |
| **Supported target build platforms** | Windows (Win64) |
| **Module platform allow list** | `Win64` on both modules |
| **Engine plugin dependency** | Enhanced Input (ships with the engine, enabled automatically) |
| **Other marketplace dependencies** | None |
| **Third-party code** | None |
| **Network replicated** | No — BindGuard is a local diagnostic tool |
| **Project types** | C++ and Blueprint-only projects both work. A Blueprint-only project gets the console commands, the settings page, the HUD class and the full Blueprint API without adding a line of C++ |
| **Build configurations** | The runtime module is a normal `Runtime` module and is present in Development *and* Shipping. The report draws on `UCanvas`, not UMG, precisely so that it survives a cooked Shipping build |

**Why Win64 only.** The two modules carry `"PlatformAllowList": [ "Win64" ]`, which is what the shipped
package is built and verified against. Nothing in the source is Windows-specific — the rules are plain
C++ over `FKey` and `FName` — so widening the allow list is a `.uplugin` edit plus a build on the target
platform, but only Win64 is tested and only Win64 is claimed.

---

## 3. Installation

### From Fab (recommended)

1. Add BindGuard to your library in Fab and install it for **Unreal Engine 5.8** from the Epic Games
   Launcher (*Unreal Engine → Library → Fab Library → Install to Engine*).
2. Open your project and go to **Edit → Plugins**, search for `BindGuard`, tick **Enabled**.
3. Restart the editor when prompted. Enhanced Input is a dependency and is enabled with it.

### Into a project (source drop)

1. Copy the `BindGuard` folder into `<YourProject>/Plugins/BindGuard/`, so that
   `<YourProject>/Plugins/BindGuard/BindGuard.uplugin` exists.
2. If your project is Blueprint-only, right-click the `.uproject` → **Generate Visual Studio project
   files** once, so the module can be compiled. (A Blueprint-only project will offer to build the plugin
   on the next editor start; accept.)
3. Start the editor. If it asks to rebuild `BindGuard` and `BindGuardEditor`, say yes.
4. **Edit → Plugins → BindGuard → Enabled**, restart.

### Verify the install

Open the editor console (`` ` ``) or the output log command box and type:

```
BindGuard.Scan
```

You should get a headline in the log, for example:

```
LogBindGuard: BindGuard: OK - actions 8 | contexts 2 | errors 0  warnings 0  info 2 | scan 0.1 ms | source asset registry
```

If the command does not exist, the plugin is not enabled or did not compile. There is also a menu entry
under **Tools → BindGuard** once the editor module has loaded.

### Building it yourself

```
RunUAT.bat BuildPlugin -Plugin="<...>\BindGuard\BindGuard.uplugin" -Package="<...>\Out" -Rocket -TargetPlatforms=Win64
```

This is the command the plugin is verified against. It builds with strict include validation and no
shared engine PCH, which is why every header in `Source/` names the engine headers it needs rather than
relying on a host project's PCH.

---

## 4. Quick start — five minutes

**Step 1 — get the report on screen.** Either set the HUD class on your game mode to `BindGuardHUD`:

*Project Settings → Maps & Modes → Selected GameMode → HUD Class → `BindGuardHUD`*

…or, if you already have your own HUD class and do not want to reparent it, leave it alone and turn on:

*Project Settings → Plugins → BindGuard → Report → **Auto Draw On Any HUD***

The two paths draw the identical panel through different hooks (`AHUD::DrawHUD` and
`AHUD::OnHUDPostRender`), and they know about each other, so they cannot draw twice.

**Step 2 — point the scan at your input assets.** By default the editor's asset walk covers `/Game`,
which is right for most projects. If your input assets live in a plugin or a specific folder, narrow or
redirect it:

*Project Settings → Plugins → BindGuard → Scanning → Scan Paths*

**Step 3 — press Play.** The scan runs automatically one second after begin play (see the caveat in
[§18](#18-troubleshooting) about editor PIE). Or press the console key and run `BindGuard.Scan` at any
time.

**Step 4 — read the top line.**

```
BindGuard  FAIL
actions 8 | contexts 2 | errors 3  warnings 2  info 3 | scan 0.1 ms | 1 excluded by settings
source asset registry   |   observed: contexts added in THIS session only - not proof that nothing ever adds them
[error] unbound        IA_Sprint
[error] keyboard only  IA_Crouch   in IMC_Default  C
[error] conflict       IA_Interact vs IA_Jump  in IMC_Default  SpaceBar
```

**Step 5 — name your deliberate exceptions.** The console key and the debug camera are supposed to be
keyboard-only. Say so once, in
*Project Settings → Plugins → BindGuard → Exemptions → Exempt Actions*, and they stop being errors —
while staying visible as `N excluded by settings`. See [§8](#8-using-the-exemption-list-properly).

**Step 6 — put it in the build.** One command, one exit code:

```
BindGuard.Gate
```

See [§10](#10-the-gate-and-the-json).

---

## 5. The demo map

`/BindGuard/BindGuard/Maps/L_BindGuardDemo`.

The demo ships eight input actions and two mapping contexts under `/BindGuard/BindGuard/Input/` that are
**deliberately wrong in exactly the ways the plugin finds**: `IA_Sprint` is bound nowhere, `IA_Crouch` is
on `C` and on no gamepad key, `IA_Jump` and `IA_Interact` are both on `SpaceBar` inside `IMC_Default`, and
nothing ever adds `IMC_Vehicle`, which leaves `IA_Handbrake` unreachable — plus enough correct bindings
(`IA_Move`, `IA_Look`) that the report is not made of nothing but errors.

`IA_OpenConsole` is keyboard-only *on purpose*, and it is listed under **Exempt Actions**. It is the
exemption list doing its honest job, and the report still counts it: `1 excluded by settings`.

The first scan reads **FAIL — 3 errors, 2 warnings, 3 info**. The panel on screen has five buttons:

| Button | What it does |
| --- | --- |
| **CHECK** | Runs a scan. `UBindGuardSubsystem::Scan`. |
| **FIX IT** | Maps `IA_Crouch` to a gamepad button, gives `IA_Sprint` a keyboard and a gamepad key, and moves `IA_Interact` off `SpaceBar` onto `E`. Then rescans: FAIL becomes WARN, and all three errors are gone. |
| **ADD THE VEHICLE CONTEXT** | Calls Enhanced Input's own `AddMappingContext` for `IMC_Vehicle`. The observer sees it, so the two *observed* findings clear and the verdict turns **OK**. |
| **EXEMPTIONS ON / OFF** | Toggles the exemption list for the session and rescans. Turning it off takes the green report back to **FAIL** on `IA_OpenConsole` — that is what the list costs you. |
| **WRITE report.json** | Writes `Saved/BindGuard/report.json`, the same file the gate writes. |

Two of those buttons are the demo. **FIX IT** is a real fix, not a staged one: `AddKeyMapping` genuinely
maps the key into the context and rebuilds the control mappings, so the next scan genuinely finds it —
the same call a rebinding screen makes in a shipping game. **ADD THE VEHICLE CONTEXT** is the other half:
it proves the observed checks are watching a real `AddMappingContext`, not guessing.

The keys **FIX IT** adds are instance-editable variables on `WBP_BindGuardDemoPanel`, so you can point it
at different keys without touching a graph.

There is no logic in the demo Blueprints that is not also available to you. `BP_BindGuardDemoHUD` derives
from `ABindGuardHUD` and does nothing but create the panel widget — the report draws itself.

**One project setting the demo needs.** Its input assets live in the plugin's content folder, not under
`/Game`, so `Config/DefaultGame.ini` points the editor's asset walk at them:

```ini
[/Script/BindGuard.BindGuardSettings]
+ScanPaths=(Path="/BindGuard/BindGuard/Input")
+ExemptActions=IA_OpenConsole
```

Remove both lines and the scan goes back to covering `/Game`, which is what your project wants.

---

## 6. The five checks

### 6.1 Unbound — an action nothing maps

An `UInputAction` asset that appears in no mapping context. Error by default: it cannot possibly work.

This is the check that needs the editor module. In a cooked build the only assets in memory are the ones
something loaded, and an action nothing binds is by definition an action nothing loaded. The editor's
asset registry walk is what makes the question answerable, which is why the report always names its
source — `asset registry` or `loaded objects`.

### 6.2 Device coverage — keyboard only, gamepad only

Every action must be reachable on every device the project requires. Keyboard/mouse and gamepad are both
required by default.

The device family is decided by asking the `FKey`:

```cpp
if (!Key.IsValid())      return EBindDevice::Other;          // an empty row is not a binding
if (Key.IsTouch())       return EBindDevice::Touch;
if (Key.IsGamepadKey())  return EBindDevice::Gamepad;
if (Key.IsMouseButton()) return EBindDevice::KeyboardMouse;
if (Key.IsGesture())     return EBindDevice::Other;
return EBindDevice::KeyboardMouse;                            // what is left is a keyboard key
```

Never from the name. Renaming `IA_Jump_KBM` to `IA_Jump` does not change a line of the report, and an
action honestly called `IA_Gamepad_Only` bound to the space bar is still reported.

Two details worth knowing:

* **Analog sticks and triggers are gamepad.** They are axes, not buttons, and a classifier built on
  names or on `IsAnalog` gets them wrong.
* **An invalid key never counts as coverage.** A mapping row with no key set is exactly the shape of the
  bug this plugin looks for, so it is not allowed to look like a binding.

Findings are named for the shape a person recognises: `keyboard only` when the action really is on
keyboard/mouse and really is missing the gamepad, `gamepad only` for the mirror image, and the general
`device gap` for anything else — because a finding that has to stretch its own name to fit is a finding
that gets argued with instead of fixed.

### 6.3 Conflict — two actions on one key

Grouped by **context**, **player-mappable profile**, **key** and **chord layer**. Two distinct actions in
one group is an error: both will be evaluated and both will fire.

Three deliberate decisions in that grouping:

* **Different contexts are not a conflict.** Reusing a key across contexts is the entire purpose of
  contexts. Cross-context sharing is still reported, as *information*, with the note that it only matters
  if both contexts are ever active at once — and its severity is clamped so it can never be raised to an
  error.
* **The chord layer separates bindings.** `Shift+E` and `E` are two bindings, not a collision. The chord
  layer is built from *Chorded Action* triggers, on the mapping and on the action, sorted so that the
  same chords in a different order still match.
* **Trigger types do not separate bindings.** *Pressed* and *Held* on one key both fire from the same
  press. Treating them as distinct would let a checker declare a real collision harmless, which is worse
  than not checking at all.

Chord *blockers* — the `UInputTriggerChordBlocker` the engine instantiates by itself — are excluded from
the chord layer. They are machinery, not authored intent.

### 6.4 Context never added — observed

A `UInputMappingContext` asset that nothing handed to `AddMappingContext` during the observed session.
Warning by default, because it is observed and not proven. See [§7](#7-what-the-plugin-cannot-know--static-against-observed).

### 6.5 Unreachable — observed

An action whose *every* context went unadded. Nothing could have triggered it.

An action in no context at all is reported as **unbound**, not as unreachable — one problem gets one
line, in the vocabulary that describes it best.

---

## 7. What the plugin **cannot** know — static against observed

This is the most important section in this document.

Three of the five checks are **static**. They read the assets, they are true whether or not anybody ever
presses Play, and they are true on a build server with no player and no world.

Two of them are **observed**, and they cannot be anything else. Whether a mapping context is added is a
decision made by Blueprint logic — a game mode, a pawn, a UI stack, a platform check, a difficulty
setting. There is no static analysis that settles it, and any tool that claims to compute it is guessing.

BindGuard's answer is to *watch*. It binds to
`UEnhancedInputLocalPlayerSubsystem::OnMappingContextAdded` on every local player, from `PreDefault` so
it is listening before the first `BeginPlay`, and writes down what actually happened.

That gives an honest answer with an honest limit, and the report states the limit every time it is drawn:

> `observed: contexts added in THIS session only — not proof that nothing ever adds them`

And when no session was watched at all — an editor scan, a build server — the two observed checks do not
return "clean". They **do not run**, and the report says so in words:

> `contexts actually added at runtime: NOT CHECKED, no session was observed`

That distinction is not pedantry. A report that quietly returned "0 contexts never added" from a scan
that could not check would be the single most misleading line this plugin could print, and a build
script that trusted it would be worse off than one with no checker at all. The JSON carries the same flag
(`observation.available`), so a build script can refuse a report that was not allowed to check what it
claims to.

**Practical advice:** run the observed half from a play session that actually reaches the parts of the
game you care about. `BindGuard.ResetObservation` clears the record so you can judge one section on its
own — start it as you enter the vehicle, and the vehicle context becomes a fair question.

---

## 8. Using the exemption list properly

Every real project has actions that are meant to break the rules: the console key, the debug camera, the
screenshot key. Without a way to say so, BindGuard reports twenty errors on its first run, the report
looks like noise, and the plugin gets switched off in the first hour. That is not a hypothetical failure
mode — it is the normal fate of a checker with no exemptions.

So the list is a first-class feature:

**Project Settings → Plugins → BindGuard → Exemptions**

* **Exempt Actions** — by asset *name* (`Debug_OpenConsole`), not by path, because the name is what
  appears in the report and what you will copy out of it. Exempts the action from the device
  requirements and from the unbound check.
* **Exempt Contexts** — contexts allowed never to be added. Only silences findings *about the context*;
  a collision inside an exempt context is still a collision.
* **Use Exemptions** — the master switch.

### The rule that makes it safe

An exempt action is **not skipped**. It is checked, its findings are produced, and then each one is
forced down to `info` and counted:

```
actions 8 | contexts 2 | errors 0  warnings 0  info 2 | scan 0.1 ms | 1 excluded by settings
```

The finding is still in the report, still names the action, and its sentence ends with
*"Excluded by settings — counted, shown, and not allowed to change the verdict."*

An exemption list that could hide its own effect would be a way to turn a report green by editing a
settings page. That is exactly what a gate exists to prevent, so it is not possible here.

### How to use it well

* Add an action **when you have decided** it is keyboard-only, not when it is inconvenient.
* Run `BindGuard.Exempt 0` once a milestone and read what comes back. That is the list's real cost.
* Watch the `excluded by settings` number in code review. If it grows without anybody deciding anything,
  the list has become a way of not fixing things.

---

## 9. The report

Drawn on `UCanvas` from `AHUD`, deliberately not in UMG. The build where a verdict matters most is the
cooked one about to be submitted, and a canvas report is there in a Shipping build with nothing else
standing.

```
BindGuard   FAIL
actions 8 | contexts 2 | errors 3  warnings 2  info 3 | scan 0.1 ms | 1 excluded by settings
source asset registry   |   observed: contexts added in THIS session only - not proof that nothing ever adds them
[error]   unbound          IA_Sprint
[error]   keyboard only    IA_Crouch      in IMC_Default   C
[error]   conflict         IA_Interact vs IA_Jump  in IMC_Default   SpaceBar
[warning] never added                     in IMC_Vehicle
[warning] unreachable      IA_Handbrake   in IMC_Vehicle
[info]    conflict         IA_Handbrake vs IA_Jump  in IMC_Vehicle / IMC_Default  SpaceBar
[info]    keyboard only    IA_OpenConsole in IMC_Default   Tilde  (excluded by settings)
```

Errors first, then warnings, then information. Within a severity the order is stable between runs, so two
reports can be diffed.

When there are no errors and no warnings the panel turns green **and prints what it checked**:

```
BindGuard   OK
actions 8 | contexts 2 | errors 0  warnings 0  info 2 | scan 0.1 ms | 1 excluded by settings
nothing to report. checked:
   actions that appear in no mapping context (static)
   keyboard/mouse coverage for every bound action (static)
   gamepad coverage for every bound action (static)
   two actions on one key inside one context (static)
   keys shared between two contexts (static, reported as information)
   contexts actually added at runtime (observed: 2 added in this session)
   actions left unreachable by an unadded context (observed, this session only)
   exemption list applied: 1 action(s), 0 context(s)
```

A checker that goes quiet when it is happy is indistinguishable from a checker that never ran. Any line
reading `NOT CHECKED` is drawn in warning colour, so a half-run check cannot pass for a clean one.

Panel placement is on the HUD class: `Panel Origin` (default `28, 90`) and `Panel Width` (default `900`),
both editable per instance and from Blueprint. The number of listed findings is capped by
**Max Report Rows** (default 16) and the panel says how many more there are.

---

## 10. The gate, and the JSON

```
BindGuard.Gate [path] [-noexit]
```

Scans, writes the report, and ends the process with:

| Code | Meaning |
| --- | --- |
| `0` | Nothing to report |
| `1` | Warnings only |
| `2` | At least one error |

The same three numbers as LocaleGuard, AssetWarden, WidgetLedger, LoadLens and HeapCensus, meaning the
same three things.

A report that could not be written exits `2`. A gate that did not run must never look like a gate that
passed.

`-noexit` reports without ending the process — what you want when typing it into the console.

From a build step, with no game:

```
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="BindGuard.Gate" -unattended -nop4 -nosplash -nullrhi
```

or from inside a packaged build's console. Both write the same file, and the file says which one it was.

A minimal CI step:

```bash
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="BindGuard.Gate" -unattended -nop4 -nosplash -nullrhi
CODE=$?
if [ "$CODE" -ge 2 ]; then echo "input bindings are broken"; cat Saved/BindGuard/report.json; exit 1; fi
if [ "$CODE" -eq 1 ]; then echo "input warnings — see report.json"; fi
```

### `Saved/BindGuard/report.json`

```json
{
  "tool": "BindGuard",
  "version": "1.0.0",
  "verdict": "fail",
  "exitCode": 2,
  "source": "asset registry",
  "actions": 8,
  "contexts": 2,
  "mappings": 11,
  "errors": 3,
  "warnings": 2,
  "info": 3,
  "excludedBySettings": 1,
  "scanMilliseconds": 0.1,
  "gatherMilliseconds": 2.4,
  "observation": { "available": true, "contextsAdded": 1 },
  "checks": [ "actions that appear in no mapping context (static)", "..." ],
  "findings": [
    {
      "kind": "keyboard only",
      "severity": "error",
      "action": "IA_Crouch",
      "context": "IMC_Default",
      "otherAction": "",
      "otherContext": "",
      "key": "C",
      "missingDevice": "gamepad",
      "crossContext": false,
      "excludedBySettings": false,
      "detail": "IA_Crouch is bound on keyboard and mouse (C in IMC_Default) and on no gamepad key. ..."
    }
  ]
}
```

The field names are hand-written rather than reflected out of the C++ struct, on purpose: a build script
greps them, which makes them a published interface that must not change because somebody renamed a
member.

`gatherMilliseconds` and `scanMilliseconds` are separate because one of them is your disk (loading input
assets) and the other is arithmetic. You are entitled to see which is which before putting this in a
build step.

**Check `observation.available` in your build script.** A report from a gate with no game behind it has
`false` there, and its `checks` list will not include the two observed lines. Accepting such a report as
"everything is fine" is the one mistake this format is designed to make impossible.

---

## 11. Console commands

| Command | What it does |
| --- | --- |
| `BindGuard.Scan` | Check now, print the headline |
| `BindGuard.Dump` | Every finding, its sentence, and the list of checks, to the log |
| `BindGuard.Show [0\|1]` | Show the on-screen report |
| `BindGuard.Hide` | Hide it |
| `BindGuard.Report [path]` | Scan and write the JSON report |
| `BindGuard.Gate [path] [-noexit]` | The gate |
| `BindGuard.Exempt [0\|1]` | Use the exemption list for this session, and rescan |
| `BindGuard.ResetObservation` | Forget which contexts were added |

`Scan`, `Dump`, `Report` and `Gate` work with a game running and without one. With a game they carry the
observed half; without one they are the static half and say so.

`Show`, `Hide`, `Exempt` and `ResetObservation` need a running game and say so if there is not one.

The editor also has **Tools → BindGuard → Scan** and **Tools → BindGuard → Write report**, which run the
same two code paths and show the result as a toast, with the full report in the Output Log.

---

## 12. Project settings reference

**Project Settings → Plugins → BindGuard** (stored in `Config/DefaultGame.ini` under
`[/Script/BindGuard.BindGuardSettings]`).

### Required Devices

| Setting | Default | Notes |
| --- | --- | --- |
| Require Keyboard Mouse | on | A gamepad-only action in a PC build is a feature a keyboard player cannot use |
| Require Gamepad | on | The console requirement. This is the one that matters |
| Require Touch | off | Turn on for a handset build |

### Exemptions

| Setting | Default | Notes |
| --- | --- | --- |
| Use Exemptions | on | Master switch |
| Exempt Actions | empty | By asset name |
| Exempt Contexts | empty | Only silences findings about the context itself |

### Severity

| Setting | Default |
| --- | --- |
| Unbound Severity | Error |
| Missing Device Severity | Error |
| Conflict Severity | Error |
| Cross Context Conflict Severity | Info — clamped to Warning at worst, never Error |
| Context Never Added Severity | Warning |
| Unreachable Severity | Warning |

### Report

| Setting | Default | Notes |
| --- | --- | --- |
| Show Report By Default | on | |
| Scan On Begin Play | on | |
| Auto Scan Delay Seconds | 1.0 | Time for the game to add its contexts before the first scan |
| Auto Draw On Any HUD | off | Draw through `AHUD::OnHUDPostRender` instead of reparenting your HUD |
| Max Report Rows | 16 | The panel says how many more there are |
| Report Path | `Saved/BindGuard/report.json` | Relative to the project directory |

### Scanning

| Setting | Default | Notes |
| --- | --- | --- |
| Scan Paths | empty → `/Game` | Content paths the editor's asset walk covers |
| Extra Contexts | empty | Contexts to load and check in a **cooked** build |
| Extra Actions | empty | Same |

`Extra Contexts` and `Extra Actions` only matter in a cooked build, where there is no asset registry walk
and BindGuard can otherwise only see what something already loaded. In the editor they are redundant and
harmless.

---

## 13. Class overview

### Runtime module `BindGuard`

| Type | Kind | What it is for |
| --- | --- | --- |
| `UBindGuardSubsystem` | `UGameInstanceSubsystem` | Watches `AddMappingContext`, runs scans, keeps the last report, draws the panel, fires `OnFindings`. On the game instance rather than the world so observation survives a map change |
| `ABindGuardHUD` | `AHUD` | Draws the report on `UCanvas`. Set it as your game mode's HUD class, or use **Auto Draw On Any HUD** and keep yours |
| `UBindGuardStatics` | `UBlueprintFunctionLibrary` | The rules as static, world-free functions, plus the Blueprint entry points and the two fix helpers |
| `UBindGuardSettings` | `UDeveloperSettings` | The settings page: required devices, exemption list, severities, report options, scan paths |
| `FBindGuardScanner` | plain struct, static | Turns input assets into plain structs, runs the analysis, writes the JSON, logs the report |
| `FBindGuardAssetSource` | plain class, static | The seam: a delegate the editor module fills in, with a loaded-objects fallback when it does not |

### Runtime data types

| Type | What it holds |
| --- | --- |
| `FBindGuardMapping` | One key, one action, one context, its chord layer, its profile, and both asset paths. The only thing the rules ever see |
| `FBindGuardFinding` | One problem: kind, severity, action, context, other action/context, key, missing device, cross-context flag, excluded flag, and the plain sentence |
| `FBindGuardRequirements` | What the project requires, flattened out of the settings so the rules never read a `UDeveloperSettings` |
| `FBindGuardScanInput` | Mappings, actions, contexts, observed contexts, and `bObservationAvailable` |
| `FBindGuardReport` | The findings, the counts behind the header line, both timings, the verdict, and the list of checks that actually ran |
| `EBindDevice` | `KeyboardMouse`, `Gamepad`, `Touch`, `Other` |
| `EBindSeverity` | `Info`, `Warning`, `Error` |
| `EBindVerdict` | `Ok`, `Warn`, `Fail` — and exit codes 0, 1, 2 |
| `EBindFindingKind` | `Unbound`, `KeyboardOnly`, `GamepadOnly`, `DeviceGap`, `Conflict`, `ContextNeverAdded`, `Unreachable` |

Every struct and enum above is `BlueprintType`, so a Blueprint can read a finding field by field and
build its own presentation.

### Editor module `BindGuardEditor`

| Type | What it is for |
| --- | --- |
| `FBindGuardEditorModule` | Registers the asset walk and the two **Tools → BindGuard** entries |
| `FBindGuardAssetScan` | The asset registry walk that makes "which action did nobody bind" answerable |

---

## 14. Blueprint API

All on `UBindGuardStatics` — one node per button.

**Running a check**

* `Scan Now` → the report
* `Get Last Report`, `Get Findings`, `Get Verdict`
* `Write Report (Path)` — empty path means the settings path

**The panel**

* `Set Report Visible`, `Is Report Visible`
* `Set Exemptions Enabled`, `Are Exemptions Enabled` — session only, does not touch the settings asset
* `Get Observed Contexts`

**The rules, callable directly**

* `Classify Key (FKey)` → `EBindDevice`
* `Find Unbound`, `Find Device Gaps`, `Find Conflicts`, `Find Context Gaps`
* `Judge (findings)` → `EBindVerdict`
* `Explain (finding)` → the plain sentence
* `Analyze (input, requirements)` → the whole report

**Formatting**

* `Format Headline`, `Format Finding`, `Device Name`, `Kind Name`, `Severity Name`, `Verdict Name`,
  `Verdict Exit Code`

**Fixing**

* `Add Key Mapping (Context, Action, Key)` — maps the key and rebuilds control mappings immediately
* `Remove Key Mapping (Context, Action, Key)`

The two fix nodes edit the context asset in memory. In the editor that marks the asset dirty, which is
correct and visible rather than hidden. They are the same calls a rebinding screen makes.

`UBindGuardSubsystem` also exposes `Scan`, `Get Report`, `Get Findings`, `Get Verdict`, `Write Report`,
`Get Observed Contexts`, `Get Active Contexts`, `Is Observing`, `Reset Observation`, and the
`On Findings` event, which fires after every scan with the report it produced.

**A typical Blueprint wiring** — a debug menu button that checks and colours itself:

```
[Button OnClicked]
  → Scan Now (World Context: self)          ── returns FBindGuardReport
  → Break FBindGuardReport
      → Verdict  → Switch on EBindVerdict
            Ok   → Set Colour (green)
            Warn → Set Colour (amber)
            Fail → Set Colour (red)
      → Findings → For Each → Explain → Append to a Text Block
```

---

## 15. C++ API and recipes

Add `BindGuard` to your module's dependencies:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "BindGuard" });
```

### The rules, with no engine standing

```cpp
#include "BindGuardStatics.h"

TArray<FBindGuardMapping> Mappings;
Mappings.Emplace(TEXT("IA_Jump"),   TEXT("IMC_Default"), EKeys::SpaceBar);
Mappings.Emplace(TEXT("IA_Crouch"), TEXT("IMC_Default"), EKeys::C);

FBindGuardRequirements Requirements;              // keyboard + gamepad required by default

const TArray<FBindGuardFinding> Gaps = UBindGuardStatics::FindDeviceGaps(Mappings, Requirements);
const EBindVerdict Verdict = UBindGuardStatics::Judge(Gaps);

for (const FBindGuardFinding& Finding : Gaps)
{
    UE_LOG(LogTemp, Warning, TEXT("%s"), *UBindGuardStatics::Explain(Finding));
}
// -> IA_Crouch is bound on keyboard and mouse (C in IMC_Default) and on no gamepad key. ...
```

No world, no subsystem, no asset registry, no player. This is the same code path the report and the gate
use, which is why it is the code path the automation tests cover.

### A whole scan from your own tool

```cpp
#include "BindGuardScanner.h"

const FBindGuardReport Report = FBindGuardScanner::RunWithProjectSettings(
    /*ObservedContexts=*/{}, /*bObservationAvailable=*/false);

FString FullPath;
FBindGuardScanner::WriteReportFile(Report, FString(), FullPath);   // empty path -> the settings path

if (Report.Verdict == EBindVerdict::Fail)
{
    UE_LOG(LogTemp, Error, TEXT("%d input errors. Report: %s"), Report.ErrorCount, *FullPath);
}
```

### Reacting to every scan in a game

```cpp
#include "BindGuardSubsystem.h"

void AMyDebugActor::BeginPlay()
{
    Super::BeginPlay();

    if (UBindGuardSubsystem* Guard = UBindGuardSubsystem::Get(this))
    {
        Guard->OnFindings.AddDynamic(this, &AMyDebugActor::HandleFindings);
        Guard->Scan();
    }
}

void AMyDebugActor::HandleFindings(const FBindGuardReport& Report)
{
    // Report.bObservationAvailable tells you whether the two observed checks ran at all.
    // Treat "false" as "not checked", never as "clean".
    if (!Report.bObservationAvailable)
    {
        return;
    }

    if (const UBindGuardSubsystem* Guard = UBindGuardSubsystem::Get(this))
    {
        for (const FName& Context : Guard->GetObservedContexts())
        {
            UE_LOG(LogTemp, Display, TEXT("context added this session: %s"), *Context.ToString());
        }
    }
}
```

### Fixing a binding at runtime — the rebinding-screen call

```cpp
#include "BindGuardStatics.h"

// Genuinely maps the key into the context and rebuilds control mappings, so it takes effect now.
UBindGuardStatics::AddKeyMapping(MyVehicleContext, MyHandbrakeAction, EKeys::Gamepad_FaceButton_Right);

// And the way back.
UBindGuardStatics::RemoveKeyMapping(MyVehicleContext, MyHandbrakeAction, EKeys::Gamepad_FaceButton_Right);
```

### Feeding your own asset source

If you have your own idea of which assets should be checked — a build tool, a plugin, a project-specific
manifest — bind it:

```cpp
#include "BindGuardAssetSource.h"

FBindGuardAssetSource::OnGather().BindStatic(&FMyScan::Gather);
FBindGuardAssetSource::SetSourceName(TEXT("my manifest"));
```

```cpp
void FMyScan::Gather(TArray<const UInputMappingContext*>& OutContexts,
                     TArray<const UInputAction*>& OutActions)
{
    // Fill both arrays. The pointers are used inside one synchronous call and never stored.
}
```

The name goes on the report header, because how complete a scan was is part of what it claims.

---

## 16. Architecture

**Two modules, and the arrow only points one way.**

| Module | Type | Loading phase | Contents |
| --- | --- | --- | --- |
| `BindGuard` | Runtime | `PreDefault` | Rules, settings, subsystem, HUD, console commands |
| `BindGuardEditor` | Editor | `PostEngineInit` | The asset registry walk, and two entries under Tools |

`PreDefault` for the runtime module is not arbitrary. The observer has to be listening before the first
`AddMappingContext`, and a player controller that adds its context in `BeginPlay` does it very early. A
context added before BindGuard started watching would be reported as never added — the observed half is
only worth having if it cannot miss the first frame.

**The runtime module never depends on the editor module.** The editor's asset walk is pushed into it
through `FBindGuardAssetSource`, a delegate. That is what keeps the report working in the packaged build,
which is where you most want it. With nothing registered, the runtime falls back to every input asset
currently loaded plus whatever the settings name, and the report says `source loaded objects` so nobody
mistakes a partial scan for a complete one.

**The rules are static functions over plain structs.** `ClassifyKey`, `FindUnbound`, `FindDeviceGaps`,
`FindConflicts`, `FindContextGaps` and `Judge` take arrays of `FBindGuardMapping` and `FName` and touch
no world, no subsystem, no player and no asset registry. That is the test surface, and it is the reason
there is exactly one copy of each rule: the subsystem, the console commands and the editor menu all call
these functions, so nothing can drift.

**Where the UObjects are.** `FBindGuardScanner` is the only place that touches input assets, and it
converts them to plain structs immediately. Nothing in this plugin holds a UObject pointer across a frame
boundary.

**No UMG.** The report is drawn on `UCanvas`. A plugin whose entire claim is a verdict about a build
cannot afford that verdict to depend on a widget asset surviving a cook and a UI stack being up. The demo
panel *is* a UMG asset, and it does nothing the Blueprint library does not expose to you.

---

## 17. Tests

`BindGuard.Rules.*` in **Window → Test Automation**, or:

```
UnrealEditor-Cmd.exe YourProject.uproject -ExecCmds="Automation RunTests BindGuard" -unattended -nop4 -nosplash
```

| Test | What it pins down |
| --- | --- |
| `ClassifyKeySortsEveryDeviceFamily` | Keyboard, mouse, mouse axis, face button, thumbstick axis, trigger axis, touch and an invalid key |
| `AnActionInNoContextIsUnbound` | Unbound by name; a mapping row with no key is not a binding |
| `OneContextIsAConflictTwoContextsAreNot` | Same context is an error, two contexts is information, different chords are not a conflict |
| `KeyboardOnlyIsFoundAndFixable` | Found — and *gone* once the gamepad binding is added. Plus the mirror case |
| `ExemptActionsAreSilencedButStillCounted` | No error, still in the report, still counted, and the master switch brings the error back |
| `JudgeFailsOnlyOnAnError` | Verdict and exit codes; an excluded finding cannot fail a gate |
| `ObservedChecksAreSkippedNotPassedWhenNothingWasWatched` | The honesty rule, in code |

They run in `CommandletContext` as well as `EditorContext`, so they are there on the build machine.

---

## 18. Troubleshooting

**The panel says `NOT RUN` and never scans by itself in editor PIE.**
The automatic scan hangs off `FCoreUObjectDelegates::PostLoadMapWithWorld`, which a duplicated PIE world
does not broadcast. In a **Standalone Game** or a packaged build the automatic scan fires as documented;
in editor PIE, press the panel's **CHECK** button or run `BindGuard.Scan`. Everything else — the
observation, the rules, the gate — is unaffected.

**The report says `source loaded objects` instead of `asset registry`.**
The editor module is not loaded. In a packaged build that is expected and correct: name the assets you
care about under **Extra Contexts** / **Extra Actions**, or run the static half from the editor.

**Every context is reported as "never added".**
The scan ran before the game added them. Raise **Auto Scan Delay Seconds**, or scan later — from a
button, or from `OnFindings` after you know your UI stack is up.

**`unbound` reports actions you know are bound.**
The asset walk did not reach them. Check **Scan Paths**: an empty list means `/Game` only, so input
assets living inside a plugin's content folder need their mount point added, exactly as the demo does.

**Twenty errors on the first run.**
That is the tool working. Read them once, decide which are deliberate, and name those under **Exempt
Actions**. Do not turn the checks off — the exemption list keeps them counted and visible.

**`BindGuard.Show` says it needs a running game.**
It does. Use `BindGuard.Dump` for the editor and the build server.

**The gate exits 2 but the report has no errors.**
The report file could not be written. A gate that did not run must never look like a gate that passed, so
an unwritable report is treated as a failure. Check the path under **Report Path** and its permissions.

---

## 19. Limits and known edges

* **Observation is per session, per process.** It survives a map change, because the subsystem lives on
  the game instance, but it does not survive a restart. That is a property of the question, not a
  shortcut.
* **The automatic scan does not fire in editor PIE.** See [§18](#18-troubleshooting). Standalone and
  packaged builds are unaffected; in PIE, scan from the button or the console.
* **The editor scan loads every input asset it finds.** The mappings, keys and chord triggers live inside
  the asset and are not in the registry tags, so there is no cheaper way that is also correct. The cost
  is measured and printed as `gatherMilliseconds` so you can decide for yourself.
* **A cooked build has no asset registry walk.** Without one, `unbound` can only be said about actions
  that are loaded. List anything you care about under **Extra Contexts** / **Extra Actions**, or run the
  static half from the editor, which is where it belongs anyway.
* **Chords, not triggers, separate two bindings on one key.** *Pressed* and *Held* on one key are still
  reported as a conflict, because both really do fire. If your design relies on that, the exemption list
  is the honest place to record the decision.
* **Actions are matched by asset name.** Two input actions with the same name in two folders are treated
  as one, and the report says the name once. Duplicate asset names are worth fixing for their own sake.
* **Enhanced Input only.** The legacy input system's action and axis mappings are not checked.
* **Win64 only, as shipped.** Nothing in the source is platform-specific, but the `PlatformAllowList` and
  the verified build cover Win64.

---

Support: <mailto:teufelsilvan@gmail.com>
Documentation: <https://wiki.teufel-engineering.com/en/BindGuard/documentation>
