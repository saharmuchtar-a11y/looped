#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WeaponData.h"
#include "PassiveCardData.generated.h"

UENUM(BlueprintType)
enum class ECardRarity : uint8
{
	Common UMETA(DisplayName = "Common"),
	Rare   UMETA(DisplayName = "Rare"),
	Epic   UMETA(DisplayName = "Epic"),
	Cursed UMETA(DisplayName = "Cursed") // downside/hard-mode cards — kept for future use
};

// Per-level tuning for a card. Index 0 = Level 1. Only the fields relevant to the card's
// EffectTag are filled per row; the rest stay at their defaults. (Transcribed from the old
// hardcoded tuning tables during the data-consolidation refactor.)
USTRUCT(BlueprintType)
struct FPassiveCardLevel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage         = 0.0f; // burn/venom per-tick, spark per-hit
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Ticks          = 0;    // burn/venom DoT tick count
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SlowMultiplier = 1.0f; // venom (lower = stronger slow)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius         = 0.0f; // chainspark chain radius
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealAmount     = 0.0f; // lifesteal heal per hit
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveSpeedBonus = 0.0f; // speed perk (+fraction, e.g. 0.10 = +10%)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float GravityScale   = 1.0f; // gravity perk (multiplier)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlatMaxHP      = 0.0f; // maxhp perk (+flat HP)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CritChance     = 0.0f; // deadeye: 0..1 chance a weapon hit crits
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float CritMultiplier = 2.0f; // deadeye: damage x on a crit
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FreezeDuration = 0.0f; // frostbite: seconds frozen solid at full chill stacks
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 EchoInterval   = 0;    // echo: every Nth hit re-triggers the effect chain
	// Generic percent knob (0.10 = +10%). Interpreted by the card's mechanic: Rupture/Executioner/
	// HuntersMark/Overcharge/GlassCannon/Strength(WoundMemory) damage bonus, Momentum speed burst,
	// Agility(LoopCadence) light-recovery cut, Fist of Steel(FoldedBreath) charge-time cut, etc.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Fraction       = 0.0f;
	// Generic threshold fraction (0.25 = 25%). Interpreted by the mechanic (Executioner HP gate).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Threshold      = 0.0f;
};

// Card/perk definition. The DataTable ROW NAME is the canonical perk id (e.g. "BurnShot",
// "Venom", "MaxHP", "Speed", "Gravity", "ChainSpark", "Lifesteal") — it keys the SaveGame
// FName maps/sets and replaces every old hardcoded `Name == "..."` check.
USTRUCT(BlueprintType)
struct FPassiveCardData : public FTableRowBase
{
	GENERATED_BODY()

	// --- Identity (DisplayName + Description now drive the card-reward UI text) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	ECardRarity Rarity = ECardRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FLinearColor CardColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> CardIcon;

	// --- Classification / gating ---
	// Tags the effect family this perk drives (Burn/Venom/etc.). Drives effect dispatch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FGameplayTagContainer EffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	int32 MaxLevel = 5;

	// Common cards are offered from the start; gated cards need a meta unlock to fire.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	bool bRequiresUnlock = false;

	// --- Per-level tuning (replaces the old hardcoded arrays) ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	TArray<FPassiveCardLevel> Levels;

	// Per-level human-readable descriptions for the card UI (index 0 = Level 1), e.g. "+5 burn
	// damage". Optional — if a level has no entry, the UI falls back to the generic Description.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	TArray<FText> LevelDescriptions;

	// --- Affinity / evolution: KEPT for the deferred GAS combat pass (Phase 2); unused by live effects ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity")
	EWeaponFamily PrimaryAffinity = EWeaponFamily::Blade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affinity")
	bool bUniversalAffinity = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	int32 EvolutionTier1Threshold = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	float EvolutionTier1Bonus = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	int32 EvolutionTier2Threshold = 25;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Evolution")
	float EvolutionTier2Bonus = 1.0f;
};

// One equipped card in the player's run deck. The run deck is the single runtime source of
// truth for the player's build and is OWNED BY ULoopedGameInstance (so it survives hard
// OpenLevel travel between rooms). UPassiveStackComponent operates on it; it holds no copy.
USTRUCT(BlueprintType)
struct FPassiveSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName CardRowName = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	int32 Level = 0;            // 1..MaxLevel (the perk level for this run)

	UPROPERTY(BlueprintReadOnly)
	FPassiveCardData CachedData; // row snapshot cached on equip

	UPROPERTY(BlueprintReadOnly)
	int32 TriggerCount = 0;     // Phase 2 (evolution) — unused in Phase 1

	UPROPERTY(BlueprintReadOnly)
	int32 EvolutionTier = 0;    // Phase 2 (evolution) — unused in Phase 1

	bool IsEmpty() const { return CardRowName == NAME_None; }
};
