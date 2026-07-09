# LOOPED — Rescue System (The Found Family) — Design Doc

Companion to `looped_story.md`. Started 2026-07-02 (Phase C step 9). Status: **design + foundation** — Lysa slice is the first build.

## Premise (canon, from looped_story.md §3)
You start alone with the Time Sphere. **Lysa, Brann, Serin, Mira** are lost in the dungeon. Rescuing one is a
one-time act; afterward they live at the Hub around the Time Sphere **permanently** and grant a specific
persistent buff. The Hub grows from lonely prison → base camp. This is the "loop remembers me" payoff.

## The flow (Sahar's 2026-06-12 revision: MISSIONS, not event rooms)
Meet a requirement → interact at the Hub → resolve the rescue → companion freed → permanent **RELIC** granted +
the companion appears at the Hub. **Each companion uses a DIFFERENT rescue method** (Sahar 2026-07-02) so each
one is memorable, not a reskin.

## ⭐ THE FIFTH (FIRST) COMPANION — the Tutorial rescue (Sahar 2026-07-03)
A NEW companion, rescued in the TUTORIAL — chronologically the FIRST rescue, before Lysa. He replaces
the earlier "free Vorr in the tutorial" idea (Vorr stays canon: the untouchable Hollow Broker).

- **The beat:** the tutorial starts WITHOUT the Arm Monitor (no middle-click, no deck UI). You find and
  free him; a DIALOGUE follows where he explains the monitor — then **he gives you the Arm Monitor**
  ("the screen on the hand"). He teaches: press MIDDLE CLICK to use it — it slows time and protects you
  (the monitor's existing open-state behavior: slow-mo + no-damage shield).
- **Framing (Sahar 2026-07-03): the monitor is THE GUIDE ITEM** — the thing that helps you through the
  loop and guides you through it. Not just a tool: your map, mentor, and shield in one.
- **His "relic" IS the monitor itself** — the biggest gift any companion gives, and automatically unique.

## 📟 MONITOR MISSIONS & HINTS (Sahar 2026-07-03 — extends the guide-item idea)
Inside the monitor (the middle-click hologram — the same UI that hosts card picking), add a
**MISSIONS + HINTS panel**: objectives and guidance shown "along the way".
- **Where:** monitor State 1's CenterPanel currently shows a placeholder — that becomes the MISSIONS
  panel (card drafts still take the center in State 2, dialogue still takes the whole monitor).
- **What:** live objectives with progress, auto-checked from game state — e.g. "Prove you persist —
  defeat The Looped (0/1)", "Lysa waits — her altar glows at camp", "Forge coils seated (2/3)",
  "Mira's fragments (3/5)" — plus a contextual HINT line.
- **Voice:** the missions/hints are written as the TUTORIAL COMPANION's guidance — he gave you the
  monitor, so it speaks with his voice forever. The guide-item promise made literal.
- **Data-driven:** DT_Missions rows (Id, DisplayText, HintText, condition: stat threshold / HasArtifact /
  counter), same table pattern as everything else. Rescue-chain missions come free from existing flags.
- **Chain becomes:** Tutorial companion → Lysa (first boss kill) → Brann → Serin → Mira.
- **Open:** his name/identity (monitor-maker? an old Hunter-engineer?) + model (Sahar's Meshy asset) +
  where in the tutorial level his rescue happens.
- **Tech sketch:** gate ToggleHologramMenu behind HasArtifact("<name>") (granted by his rescue dialogue);
  tutorial level + dialogue rows; he lives at the Hub after, like the others.

## The four companions — one distinct rescue each

| Companion | Identity | Rescue method (each DIFFERENT) | Unlock gate | Permanent buff (UNIQUE — nowhere else) |
|---|---|---|---|---|
| **Lysa** | Hunter-medic | **The Jailor** — COMBAT mission (`L_Rescue_Lysa`): a Warden mini-boss holds her cage; defeat it → cage opens → freed → portal home. | after **first boss kill** (SaveData already tracks this) | **Second Wind** — once per run, survive a lethal hit at **50% max HP** (was 1 HP; Sahar 2026-07-09 — DoT was finishing you) |
| **Brann** | Smith | **The Forge** — PUZZLE/ADVENTURE mission (`L_Rescue_Brann`): a sealed forge; solve its mechanisms (e.g. find + activate 3 forge-keys in sequence / route the heat) to open his cell. Light or no combat. | reach Floor 2 / N clears | **Forged Plate** — take **10% less damage**, always (the ONLY flat damage-reduction in the game) |
| **Serin** | Scout / thief | **Vorr's Ransom** — NON-combat bargain at the Hub (Vorr holds her as collateral): pay Echoes **+ accept a curse/sacrifice** (fits the "never free" rule + Vorr lore). No mission level. | Echoes threshold / vault unlocked | **Fence** — Vorr's shop prices **15% cheaper** (the ONLY discount in the game) |
| **Mira** | Frequency-reader | **The Scattered Frequency** — COLLECTION meta: "frequency echo" fragments drop in normal runs; collect N → she reforms at the Hub on your next return. No mission level. | collect N fragments over runs | **Reroll** — once per run, reroll a card reward for a fresh set of options (the ONLY reroll in the game) |

Distinct rescue MODES: **combat (Lysa) / puzzle-adventure (Brann) / bargain-sacrifice (Serin) / collection meta (Mira).**

> **⭐ UNIQUENESS RULE (Sahar 2026-07-02):** every companion buff must be obtainable NOWHERE else. It must NOT
> overlap vault metas (+card choice / +max HP / +start shards / +start blessing), blessings (Bloodstone MaxHP,
> Greedring shards, Featherweight gravity, EmberCore damage), the Wing (gravity) or GoldBar (+Echo income) relics,
> or any card. This ruled out the first draft (Lysa +MaxHP dup'd the vault; Mira +card-choice would push to 5
> cards). The four above each hook a system nothing else touches: **revive / damage-reduction / shop-discount /
> card-reroll.** Because they're unique, effects are BESPOKE `HasArtifact("Name")` hooks (like Wing/GoldBar), NOT
> the generic magnitude system — so they can never accidentally stack into a duplicate.

## Rewards = permanent companion RELICS
- Each companion is a **Permanent-scope `FArtifactData` row** in `DT_Artifacts` (Lysa/Brann/Serin/Mira).
- Rescue calls the existing `ULoopedGameInstance::GrantArtifact(FName)` → adds to `SaveData.OwnedArtifacts` + saves.
- `HasArtifact("Lysa")` then doubles as the "is-rescued" flag (drives Hub NPC visibility). No new save field needed.

## Tech plan / build phases

### Phase R1 — Foundation (data done; bespoke C++ hooks per buff)
- **DATA (done 2026-07-02, no build):** 4 Permanent-scope rows in `DT_Artifacts` (Lysa/Brann/Serin/Mira),
  Scope=Permanent → safe from `GrantRandomRunArtifact` (Run-only filter). EffectTags are UNIQUE marker tags
  (Artifact.SecondWind / Armor / Discount / Foresight) that the generic magnitude system does NOT sum — the
  effects are code, not magnitude, so they stay unique + un-stackable.
- **C++ (needs build):** each buff is a bespoke `HasArtifact("Name")` hook, exactly like Wing/GoldBar:
  - Lysa **Second Wind**: in `TakeDamageFromEnemy`, if a hit would be lethal and the once-per-run flag is
    unused, set HP to **50% max** + play `/Game/Audio/portal` + clear DoT + consume the flag (reset at run/Hub start).
    (Was 1 HP; Sahar 2026-07-09 — DoT was finishing you after the save.)
    **Crash fix (same day):** `IsAlive()` must use `POCCurrentHealth` (GAS Health is unsynced); death-cam
    ragdoll into hazards used to AV in `ApplyElementalStatus`. See memory `project-looped-lysa-second-wind`.
  - Brann **Forged Plate**: in `TakeDamageFromEnemy`, `if (HasArtifact("Brann")) Damage *= 0.9;` (before HP sub).
  - Serin **Fence**: in the shop cost calc, `if (HasArtifact("Serin")) Cost = ceil(Cost * 0.85);`.
  - Mira **Reroll**: in the card-reward draft, if `HasArtifact("Mira")` and the per-run reroll token is unused, show a
    Reroll button that redraws the offered cards + consumes the token (reset each run/Hub).

### Phase R2 — Lysa vertical slice (the template) — needs the R1 build
- **Level `L_Rescue_Lysa`** (dup the boss arena `L_FinalBoss` or a combat room): arena + a placeholder **caged
  companion** actor (Sahar supplies the Lysa model later, like Vorr/First Hunter) + a **Warden** mini-boss
  (reuse `EventChampion`/boss archetype) + placed exit portal (start-disabled).
- **Free-on-clear:** a small actor or a GameMode hook: when the Warden/room is cleared (bind `OnRoomCleared`),
  open the cage, `GrantArtifact("Lysa")`, show a beat, reveal the portal home. (Boss-room clear already spawns a
  hub portal — reuse that path.)
- **Hub altar** (`ARescueAltar` or reuse DialogueTrigger + a save-gate): near the Time Sphere; DARK until the
  first-boss-kill flag is set; touch when lit → `OpenLevel(L_Rescue_Lysa)`.
- **Hub companion NPC:** a placed Lysa actor hidden until `HasArtifact("Lysa")` (reuse the GuideEntity
  visibility-gate pattern). Appears at the Hub once rescued.

### Phase R3+ — the other three (each its own mini-project, distinct systems)
- **Brann** (PUZZLE-adventure): `L_Rescue_Brann` — a sealed-forge level built around a mechanism/sequence puzzle
  (find + activate forge-keys, route heat, open the cell). New puzzle actors (levers/sockets/sequence-gate);
  light or no combat. This is its own build.
- **Serin** (ransom): a Hub Vorr dialogue branch (reuse dialogue + curse + Echoes) — likely NO new level.
- **Mira** (collection): a fragment pickup + a `SaveData` counter + Hub reveal at threshold — NO mission level.

## Open / deferred
- Companion models/art (Sahar provides later — placeholders until then).
- Mira's "Reroll" needs a Reroll button wired into the card-reward draft UI + a per-run token — wire during R3.
- Exact unlock thresholds (Brann floor/clears, Serin Echoes, Mira fragment count) — tune in playtest.
- Whether rescue missions can be re-attempted on failure (default: yes, altar stays lit until rescued).

Related lore: `looped_story.md`. Plan: memory `project-looped-room-plan` (Phase C step 9).
