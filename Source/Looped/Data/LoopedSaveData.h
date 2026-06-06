#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "LoopedSaveData.generated.h"

/**
 * Persistent per-machine save data. Single slot ("LoopedPlayer").
 * Loaded by ULoopedGameInstance on Init. Saved on key events (death, boss kill, hub return).
 *
 * Schema is intentionally broad so future unlocks/achievements/stats can plug in
 * without a save format migration. Add new fields at the bottom — never remove or
 * reorder existing ones (would break old saves).
 */
UCLASS()
class LOOPED_API ULoopedSaveData : public USaveGame
{
	GENERATED_BODY()

public:
	// --- Run outcomes ---
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 BossDeaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 BossKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 RunsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 TotalDeaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 RoomClears = 0;

	// --- Time ---
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	float TotalPlaytimeSeconds = 0.0f;

	// Best boss kill time in seconds (room enter -> boss death). 0 = no kill yet.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	float FastestBossKillSeconds = 0.0f;

	// Best full-run time in seconds (run start -> run completed). 0 = no completed run yet.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	float FastestRunSeconds = 0.0f;

	// --- Combat ---
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	float TotalDamageDealt = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	float TotalDamageTaken = 0.0f;

	// --- Build history ---
	// Per-perk pick count across all runs (FName key, lifetime count value).
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TMap<FName, int32> PerksPickedByName;

	// Per-perk highest level ever reached.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TMap<FName, int32> HighestPerkLevel;

	// --- Unlocks ---
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TSet<FName> UnlockedCards;

	// Perks that have reached max level at least once — recorded the FIRST time each maxes.
	// Generic by perk FName, so any current OR future perk is tracked automatically.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TSet<FName> PerksEverMaxed;

	// Permanent cross-run artifacts the player owns (e.g. "Wing"). Apply effects at run start.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TSet<FName> OwnedArtifacts;

	// Echoes — PERMANENT cross-run currency (banked on room clear, spent at the Hub merchant).
	// The per-run "Shards" currency is transient and lives on the GameInstance, not here.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 Echoes = 0;

	// Cards bought from the merchant to start the NEXT RUN holding (one-time boon).
	// Applied + cleared at run start. Saved so a purchase survives quitting before the run.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	TArray<FName> PendingNextRunCards;

	// --- Permanent merchant vault (progression-gated) ---
	// Set true once the player has earned access (first boss kill). The vault section then opens.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	bool bMerchantVaultUnlocked = false;

	// Permanent meta-upgrades bought from the vault.
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 PermanentCardChoiceBonus = 0; // +N extra card-reward choices, forever

	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 PermanentBonusMaxHP = 0;      // +N starting max HP, forever

	// Schema version — bump if fields change in a load-breaking way (currently 1).
	UPROPERTY(BlueprintReadOnly, Category = "LOOPED|Stats")
	int32 SchemaVersion = 1;

	static constexpr const TCHAR* SlotName = TEXT("LoopedPlayer");
	static constexpr int32 UserIndex = 0;
};
