#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MissionData.generated.h"

// The two guidance layers the monitor's State-1 CenterPanel shows:
//   Mission — persistent, progress-tracked goal ("Gather Mira's fragments 3/5"). Listed.
//   Hint    — one-line planning nudge ("Vorr holds Serin for ransom"). Only the single
//             highest-priority active hint shows, pinned as a header above the missions.
// v1 rule: hints are PLANNING hints only — things still true when the player stops to open
// the monitor. Reflex/moment hints ("gauge is full — press Q NOW") do NOT belong here; the
// panel is evaluated on monitor open, not live.
UENUM(BlueprintType)
enum class EMissionCategory : uint8
{
	Mission UMETA(DisplayName = "Mission (tracked goal)"),
	Hint    UMETA(DisplayName = "Hint (planning nudge)")
};

// How a mission row reads its progress (and how a hint decides to retire). Every branch is a
// cheap read of state the game ALREADY tracks — adding a condition here must never require a
// new counter. Resolved in ULoopedGameInstance::ResolveMissionCounter.
UENUM(BlueprintType)
enum class EMissionCondition : uint8
{
	None         UMETA(DisplayName = "None (always 0 — never completes)"),
	StatAtLeast  UMETA(DisplayName = "Named counter >= target"),
	HasArtifact  UMETA(DisplayName = "Owns permanent artifact (key)"),
	CardUnlocked UMETA(DisplayName = "Card unlocked (key)"),
	CurseActive  UMETA(DisplayName = "Curse active (key; None = count of all)")
};

// One row = one mission or hint. Fully data-driven: adding guidance is a DT_Missions row,
// never code. The DataTable ROW NAME is the canonical mission id.
USTRUCT(BlueprintType)
struct FMissionData : public FTableRowBase
{
	GENERATED_BODY()

	// Player-facing line. Tokens {cur} and {max} are replaced with live progress
	// ("Gather Mira's fragments — {cur}/{max}"). Neutral monitor voice for now; the tutorial
	// companion's voice pass re-skins these strings in one sweep when he ships.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	EMissionCategory Category = EMissionCategory::Mission;

	// Progress source (Mission) / retire condition (Hint — the hint hides once this completes,
	// e.g. the Serin-ransom hint retires when HasArtifact Serin).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	EMissionCondition ConditionType = EMissionCondition::None;

	// The stat/artifact/card/curse name ConditionType reads. See ResolveMissionCounter for the
	// accepted StatAtLeast keys (BossKills / RoomClears / TotalEnemyKills / MiraFragments / ...).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	FName ConditionKey = NAME_None;

	// The "/5". HasArtifact / CardUnlocked rows leave this at 1.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition")
	int32 TargetValue = 1;

	// Optional visibility gate — the row exists only while this passes (None = always).
	// This is what chain-gates spoilers: Mira's fragment mission only appears after Serin.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	EMissionCondition ActiveWhenType = EMissionCondition::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	FName ActiveWhenKey = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gate")
	int32 ActiveWhenValue = 1;

	// List order (lower = higher). Among hints, the lowest-priority-number active one wins the
	// single header slot.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	int32 Priority = 100;
};

// One evaluated row, ready for display — what EvaluateMissions() hands the monitor panel.
USTRUCT(BlueprintType)
struct FMissionStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FName RowId = NAME_None;

	// Title with {cur}/{max} already substituted.
	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FText DisplayText;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	EMissionCategory Category = EMissionCategory::Mission;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	int32 Current = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	int32 Target = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	bool bComplete = false;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	int32 Priority = 100;
};
