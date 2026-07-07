# LOOPED — Companions Arc 2 + The Loopwright Twist (Plan of Action)

Designed 2026-07-07 with Sahar. Four new companions + the story twist (Orin caused the loop,
`looped_story.md` §5). This doc is the build plan; the story doc is the canon.

**Verdicts locked so far:** Nyra = "GREAT IDEA" · Dax = "sounds cool" · Korr+Loophound = "ultra mega cool" ·
Curse Broker = pivoted from the rejected curse-cleanser (curses are run-scoped, cleansing is worthless) ·
Voss & Tamsin = CUT (weak / Second Wind already = Lysa). Twist = "weave it into the story" (done).

---

## The rules this roster obeys
- **Uniqueness rule** (looped_rescue_system.md): every companion perk hooks a system NOTHING else touches.
  Existing: revive (Lysa) / damage-reduction (Brann) / shop-discount (Serin) / card-reroll (Mira).
  New: **path agency (Nyra) / execute (Dax) / combat pet (Korr) / voluntary-curse "heat" (Broker)**.
- **Distinct rescue MODES**, never repeating an old one. Existing: combat / puzzle / bargain / collection / scripted-tutorial.
  New: **follow-the-trail (Nyra) / duel (Dax) / trust (Korr) / carry-the-burden (Broker)**.
- **Cleanse never free** rule respected (hound's blood-tasting = max-HP sacrifice option).
- Rescues live on **Floors 2–3** → reasons to descend deep even on doomed runs.

## The four

### 1. NYRA, the Cartographer — Floor 2 (build first, cheapest)
- **Who:** a scout who went in to map the loop and got lost inside it. Her maps never chart the same dungeon
  twice — which is how she realized the maze is *authored* (quiet twist breadcrumb: someone DESIGNED this).
- **Rescue — FOLLOW THE TRAIL:** once gated (reach Floor 2), her chalk marks appear on fork portals.
  Follow her mark at 3 forks in one run → the path bends to her hideout room → freed → hub.
  The routing system itself is the mission.
- **Perk — ROUTE MAP + REDRAW:** monitor shows the whole remaining floor (room types + boss position +
  which "?" is which) + once per run swap an upcoming room for another from the pool.
- **Synergy:** Fogbound curse fogs her map too (curse stays scary post-rescue).
- **Tech:** monitor UI panel (missions-panel patterns) + CurrentRunPath read/mutate + trail flags on PortalActor. No new AI.

### 2. THE CURSE BROKER — Floor 2/3 (build second; the endgame system)
- **Who:** a thing that EATS curses — but curses only ripen inside a living host who carries them through a
  full descent. He doesn't cleanse. He **buys**. You're the only oven walking.
- **Rescue — CARRY THE BURDEN:** his sealed jar drops deep in a run; holding it curses you; carry it through
  to the end of the run → he's freed at the hub. The rescue teaches his mechanic.
- **Service — CONTRACTS (the "heat" system):** at his hub stand, voluntarily take curses (from the 13) into
  the next run; each pays out Echoes at run end, scaling with severity/stack count. Post-Looped endgame + replayability.
- **Twist thread:** he calls curses "the Loopwright's debt" — he's a scavenger living off the loop-maker's leavings.
- **Tech:** cheap — AddCurse exists, IronWill already counts stacks, payout hooks at run end. Dialogue + data + one GI method.

### 3. DAX, the Rival Hunter — arena/camp (build third)
- **Who:** a Hunter whose monitor CRACKED (can't use cards — pure skill for 80 years). Convinced YOU are part
  of the trap. Post-twist he reads as having been right — about the wrong Hunter.
- **Rescue — THE DUEL:** clean 1v1, no adds (boss-as-event tech from the event-ideas backlog). Beating him
  does NOT bring him home — too proud. He appears mid-run instead (camp "?" event: banter, wagers).
  Only after you kill The Looped does he move to the hub.
- **Service — SPARRING PIT:** hub training dummy (DPS/build tester) + rematch duels at rising tiers for rewards.
- **Perk — HUNTER'S INSTINCT (execute):** enemies below ~10% HP die instantly to your next hit.
- **Tech:** duel arena + boss-without-run-end flow + camp event + dummy actor. Moderate.

### 4. KORR + THE LOOPHOUND — Floor 3 (build last; needs friendly-AI tech)
- **Who:** a beast-tamer; the hound is the only creature that stayed loyal through every reset.
- **Rescue — TRUST (violence = fail state):** the hound guards wounded Korr and can't be killed — damage it
  and it flees, rescue resets to next run. Earn it: feed it, or let it taste your blood (max-HP sacrifice).
  It calms → leads you to Korr. The only rescue in a shooter where the gun is the wrong answer.
- **Hub:** hound wanders; **"Press [E] to pet the loophound."** Non-negotiable.
- **Service — THE PET:** opt in at the kennel pre-run; fights light, draws aggro; at 0 HP it LIMPS HOME
  (out for the run, never dies). Korr trains it with Echoes (bite / aggro radius / chilling howl) —
  companion-rank pattern like Lysa's chrono training.
- **Tech:** friendly combat AI = same toolbox as the F2 smarter-AI phase. Build AFTER that phase; hound rides it.

## Twist rollout (rides alongside, mostly dialogue-cheap)
- **T1 Breadcrumbs (anytime, cheap):** maker's-mark line in the final-arena/boss flavor; 2-3 Orin dialogue slips
  ("you always ask that"); "Loopwright's debt" phrasing in curse events + Broker lines.
- **T2 Suspicion (gate: all 4 original companions home):** one hub Mira conversation — the loop's hum sounds *built*.
- **T3 Confession (gate: first Looped kill):** post-victory reset → Orin waiting at the Time Sphere with the truth.
  Justifies why the game keeps looping after "winning." Save flag: bLoopwrightRevealed.
- **T4 The Debt (post-beta, parked):** true-ending quest — unmake the loop at its anchor (the Sphere), forgive or not.

## Build order & gates
0. **NOW: full-descent playtest + tuning first** (floors arc).
0.5. **THEN: BETTER ENEMIES phase (Sahar 2026-07-07: "i want better enemys first")** — the smarter-AI pass
   (flank/kite/coordinate, floor variants, not stat inflation). This ALSO builds the friendly-AI toolbox the
   Loophound needs. Companions arc starts only after this.
1. Nyra (F2 gate) → 2. Broker (jar drops F2+; chain-gate after Nyra?) → 3. Dax (gate: TBD — after N descents?)
   → 4. Korr+hound (after F2 smarter-AI phase). T1 breadcrumb rows can land with ANY of these (pure data).
- Models = Sahar's Meshy lane: Nyra, Broker, Dax, Korr, the hound (+ jar prop, chalk-mark decal, dummy).
- Each companion = Permanent FArtifactData row + HasArtifact("Name") bespoke hook, exactly like the first four.

## Parked / spare concepts
- **Tamsin → corpse runs** (grave spawns next run where you died, holding a cut of lost shards) — spare 5th if ever wanted.
- Voss the Resonator — cut (weak).
- "The architect is a DIFFERENT companion / red-herring structure" — cut; Orin is canon (the name was the gun).
