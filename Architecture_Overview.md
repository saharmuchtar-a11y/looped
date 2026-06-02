# LOOPED — Architecture Overview

**Audience:** Technical Director review.
**Scope:** Macro-structure only — data shapes, class tree, components, state ownership, coupling. No node-level logic.
**Method:** Documented strictly from the C++ source under `Source/Looped/` and verified Blueprint/level facts. Where a system is Blueprint-only or unverified, it is explicitly marked. Nothing here is theorized.

> **TD HEADLINE (read first):** The project runs **two parallel, divergent gameplay models**. A data-driven, GAS-based deck-builder architecture (DataTables + `PassiveStackComponent` + `WeaponHolderComponent` + `LoopedAttributeSet` + affinity/evolution) **exists as scaffolding but is effectively dead/unwired**. The game that actually plays runs on a **hardcoded "POC" model** (integer perk levels on the GameInstance + a non-GAS `POCHealth` float + hand-coded per-hit effects). These two models duplicate the same concepts (health, cards, damage) with no shared source of truth. This is the dominant source of technical debt and the #1 scalability blocker. Details in §6.

---

## 0. Project Context (orientation)

- **Engine:** UE 5.7, Windows. Single C++ module `Looped` (+ VibeUE editor plugin).
- **Genre:** FPS roguelite deck-builder ("Arm Monitor / Void Frequencies"). Lore canon lives in `looped_story.md`; design backlog lives outside the repo in the AI memory `project-looped-todo.md`.
- **Run flow (level routing, current):** `L_Hub → L_Room1 → L_Room2 → L_Room3 → L_Room4 → L_Merchant → L_Room5 → L_FinalBoss → L_Hub`.
  - `L_TestRoom/NewMap` (an older "mini-boss" room) is **orphaned** — nothing routes to it.
  - Routing is **string-based, per-level**: each combat room's `BP_CardRewardManager.PortalDestination` (an `FName`) names the next level; `L_Merchant` uses a placed `APortalActor.TargetLevelName`. There is **no central flow/run manager** — the chain is hand-edited level-by-level.

---

## 1. Data Structures

### 1a. Enums (C++)
| Enum | Values |
|---|---|
| `ECardRarity` | `Common`, `Rare`, `Epic`, `Cursed` |
| `EWeaponFamily` | `Blade`, `Gun`, `Void`, `Launcher`, `Energy` |
| `EHitType` | `Melee`, `Hitscan`, `Projectile` |
| `EEnemyAIState` | `Approach`, `Windup`, `Lunge`, `Recover`, `Kite`, `SpecialWindup`, `SpecialBurst`, `TeleportWindup` |
| `EShopGoodType` (in `HubMerchant.h`) | `NextRunCard`, `PermanentArtifact`, `PermanentUpgrade`, `InRunCard`, `Heal`, `CleanseCurse`, `ShardExchange` |

### 1b. DataTable Row Structs (the "intended" data-driven schema — see §6 for usage status)

**`FPassiveCardData : FTableRowBase`** — card definitions
`DisplayName: FText`, `Description: FText`, `Rarity: ECardRarity`, `EffectTags: FGameplayTagContainer`, `Magnitude: float`, `PrimaryAffinity: EWeaponFamily`, `bUniversalAffinity: bool`, `EvolutionTier1Threshold: int32`, `EvolutionTier1Bonus: float`, `EvolutionTier2Threshold: int32`, `EvolutionTier2Bonus: float`, `CardColor: FLinearColor`, `CardIcon: TSoftObjectPtr<UTexture2D>`

**`FWeaponData : FTableRowBase`** — weapon definitions
`DisplayName: FText`, `PrimaryFamily: EWeaponFamily`, `bIsHybrid: bool`, `SecondaryFamily: EWeaponFamily`, `HitType: EHitType`, `BaseDamage: float`, `AttackSpeed: float`, `Range: float`, `MagazineSize: int32`, `ReloadTime: float`, `FireRate: float`, `RecoilMagnitude: float`, `ScreenShakeMagnitude: float`, `FamilyColor: FLinearColor`, `WeaponMesh: TSoftObjectPtr<USkeletalMesh>`

**`FEnemyData : FTableRowBase`** — enemy definitions
`DisplayName: FText`, `EnemyTypeTag: FGameplayTag`, `MaxHealth: float`, `MoveSpeed: float`, `SpawnCost: int32`, `Attacks: TArray<FEnemyAttackData>`, `PassivePuzzleTag: FGameplayTag`, `VoidShardDropMin: int32`, `VoidShardDropMax: int32`, `HealthOrbDropChance: float`, `EnemyMesh: TSoftObjectPtr<USkeletalMesh>`, `BehaviorTree: TSoftObjectPtr<UBehaviorTree>`

### 1c. Plain Structs (non-row)
**`FEnemyAttackData`** — `AttackName: FName`, `Damage: float`, `TellDuration: float`, `Range: float`, `Cooldown: float`, `AOERadius: float`
**`FPassiveSlot`** — `CardRowName: FName`, `CachedData: FPassiveCardData`, `TriggerCount: int32`, `EvolutionTier: int32` (+ `IsEmpty()`)

### 1d. DataTable ASSETS in use
- `PassiveStackComponent.PassiveCardTable` (UDataTable, row = `FPassiveCardData`), also referenced by BP `BP_CardRewardManager.PassiveCardTableRef`.
- `WeaponHolderComponent.WeaponTable` (UDataTable, row = `FWeaponData`), default starting row `FName("Branch")`.
- ⚠ **No `FEnemyData` table is consumed at runtime** — enemy stats live as hardcoded `UPROPERTY` defaults on `AEnemyBase` (see §6).

### 1e. SaveGame schema — `ULoopedSaveData : USaveGame` (slot `"LoopedPlayer"`, UserIndex 0)
`BossDeaths: int32`, `BossKills: int32`, `RunsCompleted: int32`, `TotalDeaths: int32`, `RoomClears: int32`, `TotalPlaytimeSeconds: float`, `FastestBossKillSeconds: float`, `TotalDamageDealt: float`, `TotalDamageTaken: float`, `PerksPickedByName: TMap<FName,int32>`, `HighestPerkLevel: TMap<FName,int32>`, `UnlockedCards: TSet<FName>`, `PerksEverMaxed: TSet<FName>`, `OwnedArtifacts: TSet<FName>`, `Echoes: int32`, `PendingNextRunCards: TArray<FName>`, `bMerchantVaultUnlocked: bool`, `PermanentCardChoiceBonus: int32`, `PermanentBonusMaxHP: int32`, `SchemaVersion: int32`

### 1f. GAS Attribute Set — `ULoopedAttributeSet` (FGameplayAttributeData)
`Health`, `MaxHealth`, `BaseDamage`, `DamageMultiplier`, `CritChance`, `CritMultiplier`, `AttackSpeed`, `MoveSpeed`, `PassiveProcRate`, `IncomingDamage` (meta).
⚠ **Initialized but bypassed by the POC health path** (see §4 / §6).

### 1g. ⚠ The data the LIVE game actually uses is NOT in any table or struct
The playable perk system is **hardcoded**, split across:
- `ULoopedGameInstance`: parallel `int32` fields `BurnShotLevel, VenomLevel, ChainSparkLevel, LifestealLevel, SpeedLevel, GravityLevel, MaxHPLevel`; transient `int32 Shards`; `TSet<FName> ActiveCurses`; plus `GetPerkColor/GetPerkRarity/GetPerkCardLabel/IsPerkAtMax/IncrementPerkLevel` — all big hardcoded `if (Name == ...)` chains keyed on string FNames.
- `ALoopedCharacter`: **mirror copies** of the same 7 perk-level ints, plus hardcoded per-tier tuning tables (`BurnDmg[]`, `VenomDmg[]`, `SpeedBonus[]`, `GravityScale[]`, …).
- The merchant catalogues and curse names are hardcoded string lists in `AHubMerchant` / `ULoopedGameInstance`.

---

## 2. Class Hierarchy

```
UObject
├── AActor
│   ├── APortalActor                 (C++)  trigger → OpenLevel(TargetLevelName)
│   ├── ABossRoomExit                (C++)  spawns hub portal after enemies die
│   ├── ACardUnlockTrigger           (C++)  parkour/secret unlock + artifact grant
│   ├── AHubMerchant                 (C++)  Vorr — hub shop / vault / in-run cache (mode flag)
│   ├── ASimpleEnemy                 (C++)  ⚠ legacy/unused; separate from AEnemyBase
│   ├── [BP] BP_CardRewardManager    (Blueprint-only Actor — core run/card flow lives here)
│   └── [BP] BP_TimeSphere           (Blueprint-only Actor — mesh + RotatingMovement; secret trigger)
│
├── ACharacter
│   ├── ALoopedCharacter  : IAbilitySystemInterface   (C++)  the player
│   │   └── [BP] BP_LoopedCharacter
│   └── AEnemyBase        : IAbilitySystemInterface   (C++)  all enemies
│       ├── ABossBase                (C++)  forces ranged+boss flags; spawned directly by GameMode
│       │   └── [BP] BP_BossEnemy    (intended data-only subclass)
│       └── [BP] BP_TestEnemy, BP_RangedEnemy
│
├── AGameModeBase
│   └── ALoopedRunGameMode           (C++)
│       └── [BP] BP_LoopedRunGameMode (set as DefaultGameMode on every level)
│
├── UGameInstance
│   └── ULoopedGameInstance          (C++)  ← de-facto central state hub (see §4)
│
├── USaveGame
│   └── ULoopedSaveData              (C++)
│
├── UActorComponent
│   ├── UPassiveStackComponent       (C++)  GAS deck system — ⚠ not wired to live play
│   └── UWeaponHolderComponent       (C++)  DataTable weapon system
│
├── UAbilitySystemComponent
│   └── ULoopedAbilitySystemComponent (C++)
│
├── UAttributeSet
│   └── ULoopedAttributeSet          (C++)
│
└── UWorldSubsystem
    └── USloMoManager                (C++)  global time-dilation / bullet-time
```

UMG widgets (Blueprint `UUserWidget` subclasses, referenced from C++ by asset path + element name): `WBP_CardReward`, `WBP_Merchant`, `WBP_ArtifactsHUD`, `WBP_BossHUD`, `WBP_RoomHUD`, `WBP_DeathScreen`, `WBP_CenterMessage`, `WBP_Crosshair`.

---

## 3. Component Architecture

| Component (class) | Type | Held by | Status |
|---|---|---|---|
| `FirstPersonCamera` (`UCameraComponent`) | engine | `ALoopedCharacter` | live |
| `WeaponBranchMesh` (`UStaticMeshComponent`) | engine | `ALoopedCharacter` | live (the actual melee weapon visual, socketed to `hand_r`) |
| `ULoopedAbilitySystemComponent` | GAS | `ALoopedCharacter` **and** `AEnemyBase` | initialized; mostly bypassed (§4) |
| `ULoopedAttributeSet` | GAS | held via ASC on `ALoopedCharacter` | initialized; bypassed |
| `UPassiveStackComponent` | functional | `ALoopedCharacter` | ⚠ **present but not driving gameplay** — `EquipCard()` requires a `PassiveCardTable` row; the live perks aren't in the table, so it fails by design every pick |
| `UWeaponHolderComponent` | functional | `ALoopedCharacter` | partially live (owns `DamageMultiplier`, starting weapon, GAS-path firing → `PassiveStackComponent.EvaluatePassives`) — parallel to the branch-trace melee actually used |
| `VisualMesh` (`UStaticMeshComponent`) | engine | `AEnemyBase` | live (enemy body + dynamic material for tells) |
| `RifleMesh` (`UStaticMeshComponent`) | engine | `AEnemyBase` | live (shown only if `bIsRanged`) |
| `HPBarWidget` (`UWidgetComponent`) | engine | `AEnemyBase` | live |
| `SceneRoot` / `Mesh` / `Trigger`(`USphereComponent`) | engine | `AHubMerchant` | live |
| `TriggerBox`(`UBoxComponent`) / `PortalMesh` | engine | `APortalActor` | live |

**Notable absences vs. a typical deck-builder:** there is **no** `InventoryComponent`, `DeckManagerComponent`, `CombatComponent`, or dedicated time component on an actor. The closest equivalents are: deck → `UPassiveStackComponent` (dormant) + GameInstance int fields (live); time → `USloMoManager` (a world subsystem, not a component); combat → split between `UWeaponHolderComponent` (dormant GAS path) and inline logic in `ALoopedCharacter` / `AEnemyBase`.

---

## 4. State Management

State is **fragmented across three stores plus a pawn-side mirror**. There is **no `PlayerState`** in use.

**A. `ULoopedGameInstance` — de-facto central state hub (survives `OpenLevel` within a run).**
Holds **run-scoped** state: the 7 perk-level ints, `Shards` (per-run currency), `ActiveCurses` set, `CurrentRunRoom`, death-screen timing, and a live pointer to the loaded `Stats` (the SaveGame object). Resets perks/curses/Shards on Hub entry. This is what carries "active build" between rooms.

**B. `ULoopedSaveData` (SaveGame, single slot) — permanent cross-run state.**
Echoes, owned artifacts, unlocked cards, ever-maxed perks, pending next-run cards, vault unlock + permanent upgrades, and all lifetime stats. Loaded on `GameInstance::Init`, saved on key events.

**C. `ALoopedCharacter` (the pawn) — transient per-life state + a redundant mirror.**
Authoritative **player health is `POCCurrentHealth: float` on the pawn** (NOT GAS, NOT persisted). The pawn also **mirrors** all 7 perk-level ints from the GameInstance in `BeginPlay` for BP convenience.

**How it's passed between levels/rooms:** every room is a separate `OpenLevel`. The pawn and its `POCCurrentHealth` are **destroyed and recreated each room** (→ known bug: HP resets to full between rooms). Build/run state survives only because it lives on the **GameInstance** (B/A), which the new pawn re-reads in `BeginPlay`. Permanent unlocks/currency survive via the **SaveGame**.

**Health (duplicated):** GAS `Health`/`MaxHealth` exist and are initialized on the player ASC, but live damage flows through `ALoopedCharacter::TakeDamageFromEnemy(POCCurrentHealth)`. Enemies use yet another scheme: `AEnemyBase` has both `POCHealth`/`CurrentHealth` floats and a GAS ASC; `ULoopedAbilitySystemComponent::ApplyDamageToTarget` exists alongside `AEnemyBase::TakeDamageFromPlayer`.

**Deck / active passives:** **not persisted and not authoritative.** The GAS `UPassiveStackComponent.Slots` array is the intended deck store but isn't driven by the live pick flow. "Active passives" in play = the GameInstance perk-level ints.

**Weapon inventory:** `UWeaponHolderComponent` tracks a single `CurrentWeaponRowName` (default `"Branch"`); there is no multi-weapon inventory and it isn't persisted.

---

## 5. System Dependencies (coupling map)

Direction `A → B` = A directly references/depends on B.

- `ALoopedCharacter → ULoopedGameInstance` — **tight.** Perks, currency, curses, artifacts, death-screen, movement mods. Pervasive `GetGameInstance<ULoopedGameInstance>()` calls.
- `ALoopedRunGameMode → ULoopedGameInstance` — **tight.** Stats, Echoes/Shards awards, run-room counter, boss kill/death recording.
- `AHubMerchant → ULoopedGameInstance` **and** `→ ALoopedCharacter` — **tight.** Spends Echoes/Shards, grants artifacts/upgrades, heals the pawn, calls `IncrementPerkLevel`, removes curses.
- `ALoopedCharacter → AEnemyBase` — **tight.** Hit application iterates/casts enemies directly (`OnPlayerHitEnemy`, ChainSpark `TActorIterator`), bypassing GAS.
- `ALoopedCharacter → USloMoManager` — moderate (world subsystem lookup for bullet-time).
- `AEnemyBase → ULoopedGameInstance` — moderate. Reads the `Marked` curse to scale speed.
- `AEnemyBase → ALoopedCharacter` (player pawn) — tight (AI targeting, contact damage).
- `ACardUnlockTrigger → ULoopedGameInstance` — moderate (UnlockCard / GrantArtifact).
- `ABossRoomExit → AEnemyBase` — moderate (binds `OnEnemyDied`).
- `ALoopedRunGameMode → ABossBase / AEnemyBase` — spawns boss, iterates enemies, drives Boss/Room HUDs.
- `UWeaponHolderComponent → UPassiveStackComponent` — the **dormant** GAS combat path (`EvaluatePassives` on weapon hit).
- `[BP] BP_CardRewardManager →` (fan-out, **most-coupled node in the project**): `ALoopedCharacter`/`ULoopedGameInstance` (`GetEligibleCards`, `GetCardOffer`, `GetCardChoiceCount`, `IncrementPerkLevel`), `WBP_CardReward` (by widget-element name strings), `USloMoManager`, `UPassiveStackComponent.EquipCard` (the failing legacy call), `APortalActor`/`OpenLevel`, and the `PassiveCardTable`.
- **C++ ↔ Blueprint coupling by string:** C++ creates widgets by hardcoded asset path (`/Game/UI/WBP_*`) and reads/sets their children by **element-name string** via `GetWidgetFromName(...)`. Renaming a widget element silently breaks the binding. Same brittle string contract for level routing (`PortalDestination` FNames).

---

## 6. Technical Debt & Scalability Verdict (unvarnished)

1. **Two divergent gameplay models (CRITICAL).** The GAS/data-driven stack (`UPassiveStackComponent`, `UWeaponHolderComponent`, `ULoopedAttributeSet`, `FPassiveCardData`/`FWeaponData` tables, affinity + evolution) is the "designed" architecture but is **not what plays**. The live game is the hardcoded GameInstance-int + `POCHealth` model. `UPassiveStackComponent::EquipCard` **fails for every live card** (none exist in the table) — it was recently downgraded from an `ensure()` crash to a silent no-op. You are carrying, compiling, and partially maintaining a second full combat/card system that does nothing. **Decision required:** commit to GAS and migrate the live perks into it, or delete the GAS scaffolding. Keeping both is ongoing tax.

2. **Cards are not data-driven despite the DataTable scaffolding (CRITICAL for scalability).** Adding/﻿editing one card today requires touching **multiple hardcoded sites**: `ULoopedGameInstance` (`IncrementPerkLevel`, `GetPerkColor`, `GetPerkRarity`, `GetPerkCardLabel`, `IsPerkAtMax`, eligible/unlock rules) **and** `ALoopedCharacter` (mirror int + per-tier float tables) **and** merchant catalogues. This directly violates the project's own stated rule ("new content = new data, not new code"). A card/perk Data Asset or table that everything reads from is the single highest-leverage refactor.

3. **State is fragmented with a redundant mirror.** Perk levels exist on the GameInstance **and** are copied onto the pawn. Health is a non-persisted pawn float that resets every room (active bug). No `PlayerState`. There is no single "RunState" / "PlayerProgression" object — responsibility is smeared across GameInstance + SaveGame + pawn.

4. **Health is implemented three ways** (player GAS attrs, player `POCCurrentHealth`, enemy `CurrentHealth`/`POCHealth` + ASC). Damage application is bespoke per side, GAS effects unused.

5. **Run flow is brittle, decentralized string routing.** No flow manager: the level chain is per-level `FName` portal destinations edited by hand. Inserting `L_Merchant` required manually rerouting `L_Room4`. This will not scale to room-pathing / branching (a planned feature) without a central run/graph manager.

6. **`AHubMerchant` is a growing monolith.** Hub shop + permanent vault + in-run cache are one class switched by a `bInRunShop` flag, with goods identified by hardcoded string IDs and a section button repurposed by mode. Adding good-types/sections compounds this.

7. **UI is debug text + string-bound widgets.** Currencies/perks/curses render via `AddOnScreenDebugMessage` (dev-only on-screen text), not real HUD widgets. Card/merchant widgets are coupled to C++ by element-name strings (fragile; already caused a runtime crash this cycle).

8. **`AEnemyBase` is a ~400-line god-class** holding melee + ranged + boss phase-2 + teleport + telegraph + frenzy + alert + death-pop, all via `UPROPERTY` flags rather than composition or the `FEnemyData` table that exists for exactly this. `ASimpleEnemy` is dead legacy code alongside it.

9. **No automated tests, no Behavior Trees in use** (the `FEnemyData.BehaviorTree` field is unused; AI is a hand-rolled state machine in `Tick`).

**Bottom line:** the foundations of a clean, data-driven, GAS-backed deck-builder were laid and then abandoned in favor of a hardcoded prototype that has since accreted currency, curses, artifacts, and two merchants on top of it. It is functional and demoable, but every new card/enemy/room currently costs code edits in multiple files, and the unused "real" architecture is a standing liability. Prioritize: (a) one source of truth for card/perk data, (b) collapse the dual health/combat model, (c) a central run/state manager before room-branching is attempted.

---

*Generated as a point-in-time snapshot of the `Looped` C++ module and verified Blueprint/level facts. Implementation/node-level detail intentionally omitted per scope.*
