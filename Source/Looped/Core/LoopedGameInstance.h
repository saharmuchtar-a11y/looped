#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Data/PassiveCardData.h"
#include "Data/RoomRouting.h"
#include "Data/ArtifactData.h"
#include "LoopedGameInstance.generated.h"

class ULoopedSaveData;

// Travel-persistent, run-scoped state. Lives on the GameInstance — the only object that
// survives a hard OpenLevel — so the active run's health / currency / curses carry between
// rooms (the "amnesia fix"). NOT saved to disk; fully reset on Hub entry (new run) via
// ResetRunState(). Echoes is PERMANENT and lives in ULoopedSaveData — deliberately NOT here.
struct FRunState
{
	float CurrentHealth = 0.0f;
	float MaxHealth = 0.0f;
	int32 Shards = 0;                  // per-run currency
	int32 RunBonusMaxHP = 0;           // "Void Vigor" cache purchases — +max HP for THIS run only
	TSet<FName> ActiveCurses;          // run-scoped negative modifiers
	TArray<FName> AcquiredArtifacts;   // run-scoped relics found this run (Treasure rooms)
	bool bHealthInitialized = false;   // false until the first combat room seeds health

	// Once-per-run companion relic tokens (rescue system). Reset with the rest of the run state.
	bool bSecondWindUsed = false;      // Lysa — survive one lethal hit at 1 HP
	bool bCardRerollUsed = false;      // Mira — reroll one card reward
};

UCLASS()
class LOOPED_API ULoopedGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Meta")
	int32 GetHunterRank() const { return HunterRank; }

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Meta")
	void AddRunXP(int32 XP);

	// --- Persistent stats (broadcast to disk on key events) ---
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TObjectPtr<ULoopedSaveData> Stats;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void SaveStats();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddBossDeath();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddBossKill(float KillTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddPlayerDeath();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddRoomClear();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddRunCompleted();

	// Finishing a run faster than this (seconds) unlocks the Speed card. Tunable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Stats")
	float FastRunUnlockSeconds = 120.0f;

	// Wall-clock stamp (FPlatformTime::Seconds) of when the current run started. Survives OpenLevel
	// (the GI persists across travel). Run-scoped, not saved. 0 = no run in progress.
	double RunStartRealTime = 0.0;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddPlaytime(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddDamageDealt(float Amount);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void AddDamageTaken(float Amount);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void RecordPerkPicked(FName PerkName, int32 NewLevel);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void UnlockCard(FName CardName);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Stats")
	bool IsCardUnlocked(FName CardName) const;

	// True if this perk has reached max level at least once (ever, across all runs).
	// Use for secret gates (e.g. first max Gravity) and achievements.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Stats")
	bool HasPerkEverMaxed(FName PerkName) const;

	// How many distinct perks have ever been maxed.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Stats")
	int32 GetMaxedPerkCount() const;

	// --- Artifacts (permanent cross-run buffs) ---
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	void GrantArtifact(FName ArtifactName);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	bool HasArtifact(FName ArtifactName) const;

	// Newline-separated "Name — effect" list of owned permanent relics for the HUD (empty if none).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	FText GetOwnedArtifactsLabel() const;

	// Short "what it does" line for a curse (shown on pickup + in the monitor). Empty if unknown.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Curses")
	FText GetCurseDescription(FName Curse) const;

	// --- Discovery (feeds the First Hunter's codex; recorded on first encounter, saved to disk) ---
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Discovery")
	void RecordEnemySeen(FName EnemyTypeRow);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Discovery")
	bool IsEnemySeen(FName EnemyTypeRow) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Discovery")
	bool IsCurseSeen(FName Curse) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Discovery")
	bool IsBlessingSeen(FName ArtifactId) const;

	// Artifact "GoldBar": multiplies Echoes income while owned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Artifacts")
	float GoldBarEchoesMultiplier = 1.1f;

	// Run relic "BerserkerFetish": outgoing damage x this while the player's HP is at/under the
	// threshold. Bespoke HasRunArtifact hook (applied in AEnemyBase::TakeDamageFromPlayer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Artifacts")
	float BerserkerDamageMult = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Artifacts")
	float BerserkerHPThreshold = 0.3f;

	// Console test-grant: open the ~ console and type e.g. "GrantArtifactCheat Wing".
	UFUNCTION(Exec)
	void GrantArtifactCheat(FName ArtifactName);

	// --- Echoes (permanent cross-run currency) ---
	UFUNCTION(BlueprintPure, Category = "LOOPED|Currency")
	int32 GetEchoes() const;

	// Add Echoes to the permanent bank (persists immediately).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	void AddEchoes(int32 Amount);

	// Spend Echoes if affordable. Returns true and deducts on success, false if too poor.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	bool SpendEchoes(int32 Amount);

	// Console test-grant: open the ~ console and type e.g. "AddEchoesCheat 500".
	UFUNCTION(Exec)
	void AddEchoesCheat(int32 Amount);

	// --- Shards (TEMPORARY per-run currency; NOT saved, reset every run like perks) ---
	// Earned in-run (enemy kills / room clears), spent at in-run merchant rooms (later),
	// lost on death/reset. Lore: dungeon loot the loop's reset erases.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Currency")
	int32 GetShards() const { return CurrentRunState.Shards; }

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	void AddShards(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	bool SpendShards(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	void ResetShards();

	UFUNCTION(Exec)
	void AddShardsCheat(int32 Amount);

	// --- Merchant next-run card boons (bought with Echoes, applied once at run start) ---
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	void AddPendingNextRunCard(FName Card);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Currency")
	bool IsCardPendingNextRun(FName Card) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Currency")
	TArray<FName> GetPendingNextRunCards() const;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Currency")
	void ClearPendingNextRunCards();

	// DEV: wipe the entire save back to fresh (Echoes, artifacts, unlocks, stats, ever-maxed).
	// Also resets the live run perks. Bound to the merchant's "RESET SAVE (DEV)" button.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Dev")
	void WipeSave();

	UFUNCTION(Exec)
	void WipeSaveCheat();

	// --- Curses (run-scoped negative modifiers; NOT saved, reset every run like perks) ---
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Curses")
	void AddCurse(FName Curse);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Curses")
	void RemoveCurse(FName Curse);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Curses")
	bool HasCurse(FName Curse) const;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Curses")
	void ClearCurses();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Curses")
	TArray<FName> GetActiveCurses() const;

	UFUNCTION(Exec) void AddCurseCheat(FName Curse);
	UFUNCTION(Exec) void ClearCursesCheat();

	// --- "?" room category odds (StS-style rolling pity probabilities) ---
	// Each "?" event room rolls its CATEGORY here: 0 = story event, 1 = fight, 2 = treasure.
	// Fight/treasure shares GROW each "?" that doesn't produce them and reset when they hit, so
	// long peaceful streaks make the next "?" increasingly dangerous (and lucky streaks rarer).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Events")
	int32 RollEventCategory();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Events")
	float EventFightBaseShare = 0.12f;     // base odds a "?" is a fight

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Events")
	float EventFightPityStep = 0.05f;      // added per "?" without a fight (caps at 0.5)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Events")
	float EventTreasureBaseShare = 0.13f;  // base odds a "?" is treasure/loot

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Events")
	float EventTreasurePityStep = 0.04f;   // added per "?" without treasure (caps at 0.4)

	// Pity counters (transient; survive room travel within the run).
	int32 EventRoomsSinceFight = 0;
	int32 EventRoomsSinceTreasure = 0;

	// Curse magnitudes (tunable). Each applies only while its curse FName is active this run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseTitheMultiplier = 0.5f;     // "Tithe"    — currency gain x this (0 = none)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseFrailtyDamageMult = 1.5f;   // "Frailty"  — incoming damage x this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseBloodlessHealMult = 0.0f;   // "Bloodless"— healing x this (0 = no heals)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseLeadenSpeedMult = 0.8f;     // "Leaden"   — walk speed x this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseLeadenGravityMult = 1.35f;  // "Leaden"   — gravity scale x this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseDecayPerSecond = 2.0f;      // "Decay"    — HP lost per second during a run

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseMarkedEnemySpeedMult = 1.4f;// "Marked"   — enemy move speed x this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseWeaknessDamageMult = 0.75f; // "Weakness" — YOUR outgoing damage x this

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Curses")
	float CurseVolatileSelfDamage = 10.0f; // "Volatile" — enemy death pops also hit YOU for this within the pop radius

	// ("Static" — card effects only fire every other landed hit — has no magnitude; the
	//  hit-parity logic lives in ALoopedCharacter::OnPlayerHitEnemy.)

	// Card-reward choices to offer. Base 3, + permanent vault bonus, - 1 if "Scarcity" cursed.
	// The card-reward Blueprint should read this when building the offer.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Curses")
	int32 GetCardChoiceCount() const;

	// --- Permanent merchant vault (progression-gated section) ---
	// True once unlocked (first boss kill). The vault section opens in Vorr's shop.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	bool IsPermanentVaultUnlocked() const;

	// Permanent meta-upgrades sold in the vault (each is one-time; checked via Has* below).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Merchant")
	void GrantPermanentCardChoice();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	bool HasPermanentCardChoice() const;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Merchant")
	void GrantPermanentMaxHP(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	bool HasPermanentMaxHP() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	int32 GetPermanentBonusMaxHP() const;

	// Vault meta "Deep Pockets" — every run starts with +N Shards.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Merchant")
	void GrantPermanentStartingShards(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	bool HasPermanentStartingShards() const;

	// Vault meta "Keepsake" — every run starts with one random Blessing.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Merchant")
	void GrantPermanentStartingBlessing();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Merchant")
	bool HasPermanentStartingBlessing() const;

	// DEV: force the vault open for testing.
	UFUNCTION(Exec) void UnlockVaultCheat();

	// Print a one-shot stats summary as on-screen debug messages (dev-only).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Stats")
	void DumpStatsToScreen();

	// --- Death screen persistence across level change ---
	// The death screen needs to survive OpenLevel("L_Hub") — widgets die with their world,
	// so we track active state on the GameInstance (lives across levels) and re-create the
	// widget in the new world's LoopedCharacter::BeginPlay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|UI")
	float DeathScreenDuration = 4.0f;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void StartDeathScreen();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void EndDeathScreen();

	UFUNCTION(BlueprintPure, Category = "LOOPED|UI")
	bool IsDeathScreenActive() const { return bDeathScreenActive; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|UI")
	float GetDeathScreenRemainingSeconds() const;

	// --- Perk level ints REMOVED (data consolidation). The active build now lives in RunDeck;
	//     per-card level cap comes from the FPassiveCardData row's MaxLevel. ---

	// --- Run room progress (persists across level loads within a single run) ---
	// Which combat room of the current run the player is in (1-based). 0 = in Hub / run not started.
	// Reset to 0 on Hub entry, incremented by the run GameMode when a combat room loads.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Run")
	int32 CurrentRunRoom = 0;

	// Number of combat rooms in a run before the boss. Drives the "Room X of N" HUD readout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Run")
	int32 RoomsPerRun = 5;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	int32 IncrementPerkLevel(FName PerkName);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	int32 GetPerkLevel(FName PerkName) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	bool IsPerkAtMax(FName PerkName) const;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	TArray<FName> GetEligibleCards(const TArray<FName>& InAll) const;

	// The cards to OFFER for one card-reward: N distinct eligible cards drawn from InPool,
	// where N = GetCardChoiceCount() clamped to how many are eligible. Drives the adaptive
	// card-reward menu (Foresight +1 / Scarcity -1). Returns 1..4 names (or fewer if the pool is small).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	TArray<FName> GetCardOffer(const TArray<FName>& InPool) const;

	// Returns a player-friendly label for the card UI, e.g. "BurnShot — Lv 1 → 2" or "BurnShot — NEW".
	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	FText GetPerkCardLabel(FName PerkName) const;

	// Card flavor color, used by WBP_CardReward to tint the card label per perk family.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	FLinearColor GetPerkColor(FName PerkName) const;

	// Rarity tier for a perk/card. Single source of truth for the live perk system
	// (the FPassiveCardData DataTable is separate scaffolding). Used by the card UI
	// and, later, by offer-frequency weighting.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	ECardRarity GetPerkRarity(FName PerkName) const;

	// Clear all perk levels (called when starting a fresh run / player death).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	void ResetPerks();

	// --- Run Deck: single runtime source of truth for the player's active build. ---
	// Owned HERE (not on the Character/PlayerState) so it survives hard OpenLevel travel
	// between rooms — the GameInstance is the only object that persists across travel.
	// Run-scoped: cleared on Hub entry / new run. NOT saved (run-only build).
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Deck")
	TArray<FPassiveSlot> RunDeck;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Deck")
	int32 GetCardLevel(FName CardId) const;

	// Equip the card (if new) or level it up, clamped to the row's MaxLevel. Returns new level.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Deck")
	int32 AddOrLevelCard(FName CardId);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Deck")
	void ClearRunDeck();

	// Card definition row by id (nullptr if missing / table unset).
	const FPassiveCardData* FindCardRow(FName CardId) const;

	// Per-level tuning row for an EQUIPPED card at its effective level (Brittle curse = one tier
	// weaker, min 1). Null if the card isn't equipped / has no data. Single source for every
	// effect that reads card magnitudes (character hit chain, weapon crit roll, ...).
	const FPassiveCardLevel* GetEffectiveLevelData(FName CardId) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Deck")
	UDataTable* GetCardTable() const { return CardTable; }

	// --- Card level-description pipeline (drives card-reward / shop / wrist-UI text) ---
	// Exact description for a specific level (LevelDescriptions[Level-1]; falls back to the row's
	// generic Description if that level has no entry). Level is 1-based.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	FText GetCardDescriptionForLevel(FName CardId, int32 Level) const;

	// Short "what this card does" blurb for the card-draft UI — the row's generic Description.
	// Level-agnostic on purpose (the draft shows what the card is, not the per-level delta).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	FText GetCardDraftDescription(FName CardId) const;

	// The card's frame/art texture (DT row CardIcon, loaded). Null if unset. Drives the card-draft art.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	class UTexture2D* GetCardIcon(FName CardId) const;

	// "Current: X  ->  Upgrade: Y" preview for upgrading from CurrentLevel. Returns "MAX LEVEL"
	// when already capped. CurrentLevel 0 = not yet owned (previews Level 1 as the upgrade).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	FText GetCardUpgradePreviewText(FName CardId, int32 CurrentLevel) const;

	// --- Persistent run health (write-through; survives OpenLevel, reset on Hub) ---
	// The Character keeps POCCurrentHealth/POCMaxHealth as its live working values and mirrors
	// them here on every mutation (SyncHealthToRunState). On (re)spawn it restores from here so
	// health carries between rooms instead of snapping back to full. bHealthInitialized stays
	// false until the first combat room seeds it, which is how a fresh run starts at full HP.
	void SetRunHealth(float Current, float Max)
	{
		CurrentRunState.CurrentHealth = Current;
		CurrentRunState.MaxHealth = Max;
		CurrentRunState.bHealthInitialized = true;
	}
	float GetRunHealth() const { return CurrentRunState.CurrentHealth; }
	float GetRunMaxHealth() const { return CurrentRunState.MaxHealth; }
	bool IsRunHealthInitialized() const { return CurrentRunState.bHealthInitialized; }

	// --- Rescued-companion relic tokens (once per run; see looped_rescue_system.md) ---
	// Lysa "Second Wind": the Character consumes this when a lethal hit is survived at 1 HP.
	bool IsSecondWindUsed() const { return CurrentRunState.bSecondWindUsed; }
	void MarkSecondWindUsed() { CurrentRunState.bSecondWindUsed = true; }

	// Mira "Reroll": the card-draft UI shows a reroll button while this is true (R3 wires the UI).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	bool CanUseCardReroll() const { return HasArtifact(TEXT("Mira")) && !CurrentRunState.bCardRerollUsed; }

	// Consumes the reroll token. Returns false (and does nothing) if it isn't available.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	bool ConsumeCardReroll()
	{
		if (!CanUseCardReroll()) return false;
		CurrentRunState.bCardRerollUsed = true;
		return true;
	}

	// "Void Vigor" — merchant-bought +max HP for THIS run only (folded into the Character's
	// ApplyMaxHPMod). Public accessors so callers never touch the private run state directly.
	int32 GetRunBonusMaxHP() const { return CurrentRunState.RunBonusMaxHP; }
	void AddRunBonusMaxHP(int32 Amount) { CurrentRunState.RunBonusMaxHP += Amount; }

	// Full new-run wipe: clears the run deck AND the run state (health flag, shards, curses).
	// Called on Hub entry. Echoes / artifacts / unlocks (the save) are deliberately untouched.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void ResetRunState();

	// --- Data-driven run routing (Phase 3) ------------------------------------------------
	// The generated room order for the current run, rolled in ResetRunState() from the routing
	// config (pools + RunLayout). Lives on the GameInstance so it survives hard OpenLevel.
	// CurrentPathIndex: -1 = Hub / pre-run; 0..N-1 = the room the player is currently in.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Run")
	TArray<FRoomNode> CurrentRunPath;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Run")
	int32 CurrentPathIndex = -1;

	// Roll a fresh CurrentRunPath from the routing config. Random pool draws avoid back-to-back
	// level repeats. Called from ResetRunState() — exactly one fresh path per run.
	void GenerateRunPath();

	// Hub "enter the loop" portal: set the index to the first node and return its level name.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	FName BeginRunPath();

	// Room-exit portal: advance to the next node and return its level name. Safety fallback:
	// returns "L_Hub" if there is no next node / the path is empty (never soft-locks a run).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	FName AdvanceToNextRoom();

	// Atomic "advance + travel": advances the run path one step and immediately OpenLevels the
	// resolved level. Single source of truth for room-exit travel — used by BP_CardRewardManager
	// (replaces its Open Level node) so the path index and the level load can never desync.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run", meta = (WorldContext = "WorldContextObject"))
	void TravelToNextRoom(const UObject* WorldContextObject);

	// Reveals + configures the room's exit portals (the choice fork). Called by BP_CardRewardManager
	// after the card draft (combat rooms) and by the GameMode on entering a non-combat run room.
	// Picks 2 distinct room types from DT_RoomTypes and assigns one to each placed fork portal — or,
	// once RunRoomsEntered has hit RunLengthBeforeBoss, configures a single Boss portal instead.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void ActivateRoomExitPortals();

	// --- Choice forks (Step 2): data-driven room types from DT_RoomTypes -------------------------
	// Pick `Count` DISTINCT, fork-offerable room types, weighted by each row's Weight. Never repeats
	// a type within the result (the one hard rule). Returns the chosen type RowNames.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	TArray<FName> GenerateForkChoices(int32 Count = 2);

	// Enter a room of the given type: draws a random level from that type's pool (no back-to-back
	// repeat), appends a FRoomNode (with the row's MappedType) to CurrentRunPath, advances the index
	// and the rooms-entered counter, and returns the level name for the portal to OpenLevel.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	FName EnterRoomType(FName RoomTypeId);

	// True once the player has cleared enough rooms that the next exit must be the Boss.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	bool IsBossNext() const { return RunRoomsEntered >= RunLengthBeforeBoss; }

	// Look up a row in DT_RoomTypes (nullptr if missing / table unset).
	const struct FRoomTypeData* FindRoomType(FName RoomTypeId) const;

	// Non-boss rooms entered before the Boss is forced (Sahar: 9 rooms, boss is #10).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Run")
	int32 RunLengthBeforeBoss = 9;

	// Rooms entered this run (1 at the first room from BeginRunPath, ++ each fork). Gates the boss.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Run")
	int32 RunRoomsEntered = 0;

	// RowName of the type to force as the boss exit (non-offerable row in DT_RoomTypes).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Run")
	FName BossRoomTypeId = FName(TEXT("Boss"));

	// Ends the active run path (CurrentPathIndex = -1) so the NEXT level loaded is treated as the
	// Hub / pre-run. Called the moment a run ends (boss clear / death / win) — the Hub's
	// GameMode::BeginPlay runs before the player's BeginPlay resets state, so without this it would
	// read the stale Boss node and spawn a boss in the Hub.
	void EndRunPath();

	// --- Run-scoped artifacts (relics found in Treasure rooms) ------------------------------
	// Held in FRunState.AcquiredArtifacts: lives on the GameInstance, so it survives hard
	// OpenLevel travel between rooms (no amnesia), and is wiped by ResetRunState() each new run —
	// the exact same mechanism that carries the run deck and health. (Effect application is wired
	// in a later step; this step is the data + holding foundation.)
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	void GrantRunArtifact(FName ArtifactId);

	// Treasure-room reward: rarity-weighted random draw from DT_Artifacts (Run-scope, unlocked,
	// not already held this run). Grants it (incl. any cursed-bargain injection) and returns its
	// id for UI — NAME_None if nothing eligible.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	FName GrantRandomRunArtifact();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	bool HasRunArtifact(FName ArtifactId) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	TArray<FName> GetRunArtifacts() const;

	// Artifact definition row by id (nullptr if missing / table unset).
	const FArtifactData* FindArtifactRow(FName ArtifactId) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts")
	UDataTable* GetArtifactTable() const { return ArtifactTable; }

	// Preview-roll a Run artifact WITHOUT granting (pedestal display). bCursed selects the
	// cursed vs clean subset. Returns NAME_None if nothing eligible.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	FName RollRunArtifact(bool bCursed) const;

	// Grant N random eligible cards into the run deck (the Card Bundle treasure pedestal).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Artifacts")
	void GrantRunCardBundle(int32 Count);

	// --- Aggregated artifact modifiers (held run relics x DT_Artifacts) ---
	// Read by the re-derivation hooks. Additive families return a sum; multiplier families a product.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactFlatMaxHP() const;   // Artifact.MaxHP     (sum)
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactSpeedBonus() const;  // Artifact.MoveSpeed (sum)
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactGravityMult() const; // Artifact.Gravity   (product)
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactShardMult() const;   // Artifact.ShardGain (product)
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactEchoMult() const;    // Artifact.EchoGain  (product)
	UFUNCTION(BlueprintPure, Category = "LOOPED|Artifacts") float GetArtifactDamageMult() const;  // Artifact.DamageMult(product)

	// --- Treasure room "N of X" pick budget (resets per treasure room) ---
	// Default 1 = take one pedestal, the rest lock. A relic could raise this later.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Treasure")
	int32 MaxAllowedPicks = 1;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Treasure")
	int32 CurrentRoomPicks = 0;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Treasure")
	void ResetTreasurePicks();                       // call on entering a treasure room

	UFUNCTION(BlueprintPure, Category = "LOOPED|Treasure")
	bool CanPickTreasure() const { return CurrentRoomPicks < MaxAllowedPicks; }

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Treasure")
	void RegisterTreasurePick();                     // increment after a confirmed pick

	// The node the player is currently in (default-constructed if in the Hub / pre-run).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	FRoomNode GetCurrentRoomNode() const;

	// Count of Combat-type rooms in the generated run (drives the "Room X / N" HUD readout).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	int32 GetCombatRoomCount() const;

	// True when the player is inside a generated run room (not the Hub / pre-run).
	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	bool IsInRunRoom() const { return CurrentPathIndex >= 0; }

private:
	int32 HunterRank = 0; // 0=F, 1=E, 2=D, 3=C, 4=B, 5=A, 6=S
	int32 CurrentXP = 0;

	bool bDeathScreenActive = false;
	double DeathScreenStartedAtRealTime = 0.0; // real time (FPlatformTime::Seconds), not world time

	// Travel-persistent, run-scoped state (health / shards / active curses). Transient (NOT in
	// the save) — wiped on Hub entry / new run via ResetRunState(). See FRunState above.
	FRunState CurrentRunState;

	// Card definition table, loaded by path in Init(). Single source of card data.
	UPROPERTY()
	TObjectPtr<UDataTable> CardTable;

	// Run routing config (pools + run layout), loaded by path in Init(). Drives GenerateRunPath().
	UPROPERTY()
	TObjectPtr<URunRoutingConfig> RoutingConfig;

	// Data-driven room TYPES (DT_RoomTypes, rows = FRoomTypeData), loaded by path in Init(). Source
	// for the 2-portal choice forks (Step 2). Adding a room type = add a row — no code change.
	UPROPERTY()
	TObjectPtr<UDataTable> RoomTypeTable;

	// Artifact/relic definition table, loaded by path in Init(). Single source of artifact data.
	UPROPERTY()
	TObjectPtr<UDataTable> ArtifactTable;

	void LoadOrCreateStats();
	void EvaluateUnlocksAfterStatChange();

	// Aggregate Magnitude across held run artifacts whose EffectTags contain TagName.
	float SumArtifactMagnitude(FName TagName) const;      // additive families  (0 if none)
	float ProductArtifactMagnitude(FName TagName) const;  // multiplier families (1 if none)
};
