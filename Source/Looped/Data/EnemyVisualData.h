#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EnemyVisualData.generated.h"

class USkeletalMesh;
class UAnimSequence;

// One VISUAL identity: a Meshy skeletal mesh + its single-node animation kit. Lives in
// DT_EnemyVisuals; picked by FLOOR + role ("F2_Melee", "Boss_F3", "Boss_F3_P2") — archetype
// rows keep owning STATS, the floor owns the LOOK (matches how Sahar's assets are organized:
// one melee/ranged/boss character per floor). Missing anims fall back to their neighbors.
USTRUCT(BlueprintType)
struct FEnemyVisualSet : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Attack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Cast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSoftObjectPtr<UAnimSequence> Death;

	// Mesh transform relative to the capsule. Meshy exports: centered pivot -> drop by the
	// capsule half-height; model front lines up at yaw 270 (the Vorr rule).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	FVector MeshRelLocation = FVector(0.0f, 0.0f, -90.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	float MeshRelYaw = 270.0f;

	// Extra mesh-only scale. Bosses usually keep 1.0 — their DT_Enemies row already scales the ACTOR.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	float MeshScale = 1.0f;

	// Floater kit (F3 ranged / Hexweaver): no walk/run locomotion, mesh raised + bobbed,
	// and EnemyBase grants bCanFloatOverHazards so they alone may path over lava/venom.
	// NOTE: DT row names are floor roles — F3_Ranged uses floor2ranged Hexweaver assets (and
	// F2_Ranged uses rangedfloor3 Gearbound). Folder names do NOT match the floor role.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Float")
	bool bFloats = false;

	// Added on top of MeshRelLocation.Z so the mesh clearly hovers above the capsule/floor.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Float", meta = (EditCondition = "bFloats"))
	float FloatHoverBoostZ = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Float", meta = (EditCondition = "bFloats"))
	float FloatBobAmplitude = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual|Float", meta = (EditCondition = "bFloats"))
	float FloatBobSpeed = 1.8f;
};
