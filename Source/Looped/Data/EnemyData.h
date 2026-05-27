#pragma once

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "BehaviorTree/BehaviorTree.h"
#include "EnemyData.generated.h"

USTRUCT(BlueprintType)
struct FEnemyAttackData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName AttackName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Damage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float TellDuration = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Range = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Cooldown = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float AOERadius = 0.0f;
};

USTRUCT(BlueprintType)
struct FEnemyData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag EnemyTypeTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	float MoveSpeed = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	int32 SpawnCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<FEnemyAttackData> Attacks;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	FGameplayTag PassivePuzzleTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 VoidShardDropMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	int32 VoidShardDropMax = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	float HealthOrbDropChance = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> EnemyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI")
	TSoftObjectPtr<UBehaviorTree> BehaviorTree;
};
