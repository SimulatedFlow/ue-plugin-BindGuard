# BindGuard — Every Action Bound, On Every Device

Enhanced Input manages your bindings. It does not check them. BindGuard checks them.

Documentation: <https://wiki.teufel-engineering.com/en/BindGuard/documentation>

---

## The five questions

1. **Which action did nobody bind?** An `UInputAction` that appears in no mapping context at all.
2. **Which action exists only on one device?** Bound on keyboard and mouse but not on the gamepad — the
   case a console submission fails first. The device is read off the `FKey` (`IsGamepadKey`,
   `IsMouseButton`, `IsTouch`), never off the name of the action.
3. **Which two actions are fighting over the same key?** Same key, same context, same chord layer.
   Different contexts sharing a key is *not* an error — that is what contexts are for — so it is reported
   as information, never as a failure.
4. **Which mapping context does nobody ever add?** Observed at runtime, because it cannot be computed.
5. **Which action is unreachable because its only context is never active?** From 1 and 4.

## What it cannot know, and says so

Whether `AddMappingContext` is called lives in Blueprint logic and cannot be worked out from assets.
BindGuard solves that by **watching**: it binds to `UEnhancedInputLocalPlayerSubsystem::OnMappingContextAdded`
and records what actually happened. The report is split in two and labelled:

* **static** — read from the assets, always true;
* **observed** — what happened *in this session*, with the explicit note that a context missing here was
  not added in this session and that this is not proof that nothing ever adds it.

When there is nothing to report, the panel turns green **and lists what was checked**, so "green" can
never be confused with "never ran".

## The gate

```
BindGuard.Gate [path] [-noexit]
```

Scans, writes `Saved/BindGuard/report.json`, and exits **0** (clean), **1** (warnings only), **2**
(errors). The same three return codes as LocaleGuard, AssetWarden, WidgetLedger, LoadLens and HeapCensus —
one convention, six tools.

## Console commands

| Command | What it does |
| --- | --- |
| `BindGuard.Scan` | Check now, print the headline |
| `BindGuard.Dump` | Every finding, its sentence, and what was checked, to the log |
| `BindGuard.Show` / `BindGuard.Hide` | The on-screen report |
| `BindGuard.Report [path]` | Write the JSON report |
| `BindGuard.Gate [path] [-noexit]` | The build-server gate |
| `BindGuard.Exempt [0\|1]` | Use the exemption list, or do not, for this session |
| `BindGuard.ResetObservation` | Forget what was observed, to judge one section of the game |

## The exemption list

Every project has actions that are meant to be keyboard-only. Name them in
**Project Settings → Plugins → BindGuard → Exempt Actions**. What is exempted is **still counted and
still shown** — the header line reads `2 excluded by settings` — so the list can never quietly make a
project look clean.

## Setup

1. Enable the plugin. Enhanced Input is required and is enabled with it.
2. Set the HUD class on your game mode to `BindGuardHUD`, **or** turn on *Auto Draw On Any HUD* in
   Project Settings and keep your own HUD.
3. Press Play. The report scans itself a second after begin play, so the game has time to add its
   contexts.

## Modules

| Module | Type | Why |
| --- | --- | --- |
| `BindGuard` | Runtime (PreDefault) | Observes, judges, draws. Exists in a cooked Shipping build. |
| `BindGuardEditor` | Editor (PostEngineInit) | Walks the asset registry so the static checks run with no game. |

The runtime module never depends on the editor module. The editor's asset walk is pushed in through a
delegate, which is why the report still works in the packaged build.

## Tests

`BindGuard.Rules.*` in the Session Frontend — seven automation tests over the pure rules: key
classification, unbound actions, one-context vs two-context conflicts, keyboard-only in both directions,
the exemption list being silenced but still counted, the verdict and its exit codes, and the observed
checks being *skipped* rather than reported clean when nothing was watched.

---

Win64. Source included. No third-party code.
Support: <mailto:teufelsilvan@gmail.com>

<!-- SF-STORE-BLOCK:BEGIN -->
## 🛒 Source-available — see before you buy

This repository contains the **full source** of a commercial Unreal Engine plugin. It is **source-available, not open source**: read it, evaluate it, then buy a license to use it. See **the Fab Content License Agreement / Unreal Engine EULA (purchase required)**.

**Get it / Buy:**
- **Buy on Fab** (this plugin): https://www.fab.com/listings/4add657e-71e6-4af0-92fc-3c513de3d3da
- Fab store — all our UE5 plugins: https://www.fab.com/sellers/Silvan%20Teufel

### 📬 **Free UE5 Snippet-Pack**

10 ready-to-use C++/Blueprint building blocks (subsystems, versioned saves, async nodes, editor tooling) — MIT licensed. Get it by joining the newsletter — plus a heads-up when something new ships. Double opt-in, unsubscribe in one click, no address sharing.

👉 **[Get the free pack](https://silvan.teufel-engineering.com/newsletter/plugins/?q=gh)**

_© 2026 Silvan Teufel. All rights reserved._
<!-- SF-STORE-BLOCK:END -->
