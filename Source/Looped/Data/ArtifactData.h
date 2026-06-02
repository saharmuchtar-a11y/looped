#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Data/PassiveCardData.h" // ECardRarity (reused)
#include "ArtifactData.generated.h"

// Where an artifact lives:
//   Run       — a relic found this run (Treasure rooms). Held in FRunState.AcquiredArtifacts,
//               survives OpenLevel, wiped on Hub entry. (THIS SPRINT.)
//   Permanent — a cross-run buff stored in the SaveGame (the existing Wing/GoldBar model).
//               Reserved so the two systems can share one table later.
UENUM(BlueprintType)
enum class EArtifactScope : uint8
{
	Run       UMETA(DisplayName = "Run (relic — wiped each run)"),
	Permanent UMETA(DisplayName = "Permanent (saved cross-run)")
};

// Un-leveled artifact/relic definition — mirrors FPassiveCardData but with no per-level array
// (artifacts are flat passive modifiers). The DataTable ROW NAME is the canonical artifact id.
// Effects are dispatched by EffectTags at the SAME re-derivation hooks cards already use
// (ApplyMaxHPMod / ApplyMovementMods / AddShards / AddEchoes / OnPlayerHitEnemy).
USTRUCT(BlueprintType)
struct FArtifactData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	ECardRarity Rarity = ECardRarity::Common;

	// Run (this sprint) vs Permanent (reserved). GrantRunArtifact only honors Run-scope rows.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EArtifactScope Scope = EArtifactScope::Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	TSoftObjectPtr<UTexture2D> Icon;

	// What the artifact does. MVP families: Artifact.MaxHP / MoveSpeed / Gravity / ShardGain /
	// EchoGain / DamageMult. Drives the apply hooks (wired in a later step).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FGameplayTagContainer EffectTags;

	// Single un-leveled tunable, interpreted by the primary EffectTag (flat add or multiplier).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	float Magnitude = 0.0f;

	// Common artifacts can drop from the start; gated ones need a meta unlock to appear.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression")
	bool bRequiresUnlock = false;

	// --- Cursed Bargain: a relic that grants its upside but also injects a run curse ---
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursed Bargain")
	bool bIsCursed = false;

	// Curse id injected via the run curse system when this artifact is acquired (only if bIsCursed).
	// Must match a curse FName the curse system understands (e.g. Frailty / Leaden / Bloodless).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cursed Bargain", meta = (EditCondition = "bIsCursed"))
	FName AssociatedCurseId = NAME_None;
};
