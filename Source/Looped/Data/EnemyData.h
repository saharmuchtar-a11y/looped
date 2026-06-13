#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EnemyData.generated.h"

// One row in DT_Enemies — an enemy ARCHETYPE. A placed/spawned enemy sets its EnemyTypeRow and
// BeginPlay applies this row over the EnemyBase defaults, so adding a new enemy type (or tuning
// an existing one) = editing a row, no code and no new Blueprint. Fields mirror the live
// AEnemyBase knobs they drive — every field here is actually applied in ApplyEnemyType().
USTRUCT(BlueprintType)
struct FEnemyTypeData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	// --- Stats ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxHealth = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MoveSpeed = 350.0f;

	// Uniform actor scale — reads as silhouette: big = tank, small = fast.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Scale = 1.0f;

	// Body tint (drives the enemy material + overlay base). HDR >1 channels glow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	FLinearColor EnemyColor = FLinearColor::Red;

	// --- Behavior ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bIsRanged = false;

	// Ranged enemy that ALSO melees when the player closes in (mini-elite feel).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bHybridMelee = false;

	// --- Combat ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float MeleeDamage = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float RangedDamage = 8.0f;

	// Seconds between ranged shots — lower = faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float RangedFireRate = 2.0f;

	// Melee tell length — shorter = harder to react to. Keep >= 0.3 so the tell stays readable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	float WindupDuration = 0.6f;

	// DT_ProjectileElements RowName for this archetype's shots (orb color + damage mult + status).
	// "None" = plain grey orb. Also the hook for themed rooms (fire room spawns Fire enemies).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat")
	FName ProjectileElement = FName(TEXT("None"));

	// --- Spawning (encounter generator — used when rooms roll their own enemy mix) ---
	// Budget cost: a room spends its difficulty budget on archetypes (Brute costs more than a Grunt).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawning")
	int32 SpawnCost = 1;

	// Relative pick weight inside the eligible pool. 0 = never rolled (placed/boss only).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawning")
	float SpawnWeight = 1.0f;

	// First floor this archetype can appear on (escalation lever for Floors 2-3).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawning")
	int32 MinFloor = 1;
};
