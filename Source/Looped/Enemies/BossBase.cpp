#include "Enemies/BossBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"

ABossBase::ABossBase()
{
	// Boss defaults — match the project's "wild and hard final boss" design intent.
	bIsBoss = true;
	bIsRanged = true;

	POCHealth = 300.0f;
	MoveSpeed = 400.0f;

	MeleeDamage = 12.0f;
	RangedDamage = 14.0f;
	RangedFireRate = 1.6f;
	RangedRange = 1800.0f;

	// Phase 1 kite tuning — keep boss in player melee-reach range so the fight is winnable.
	// Player branch weapon is ~200uu range and sprint covers ~900uu/s; these values let the
	// player sprint into melee in ~0.5s while still taking telegraphed ranged shots.
	KiteMinDist = 400.0f;
	KiteMaxDist = 700.0f;
	KiteBackpedalDistance = 180.0f;
	KiteStrafeDistance = 250.0f;

	// Slightly slower base Skyfall so the player can read it on a fresh fight.
	SpecialCooldown = 8.0f;
	SpecialBurstShots = 3;
	SpecialWindupDuration = 1.5f;
	SpecialBurstInterval = 0.3f;
	SpecialDamageMultiplier = 1.0f;

	// Phase 2 stays at the AEnemyBase defaults (70% threshold, ×1.5 damage, teleport).
	// Subclasses can tune per boss in BP defaults.

	EnemyColor = FLinearColor(1.0f, 0.05f, 0.05f, 1.0f); // deeper red than mooks — bosses LOOK different

	if (GetCapsuleComponent())
	{
		GetCapsuleComponent()->SetCapsuleHalfHeight(140.0f);
		GetCapsuleComponent()->SetCapsuleRadius(70.0f);
	}

	// Damage-receivable collision: AEnemyBase::VisualMesh defaults to NoCollision, and the
	// player's MeleeTrace line trace passes right through. BP_TestEnemy works because its
	// BP added a separate Cube static mesh with BlockAllDynamic. We need the equivalent here.
	// Setting the inherited VisualMesh to BlockAllDynamic matches the BP behavior.
	if (VisualMesh)
	{
		VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		VisualMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		VisualMesh->SetVisibility(false); // hide the gray cube — mannequin is the visible body now
	}

	// Manny mannequin mesh on the inherited Character mesh slot.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyMeshFinder(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
	if (MannyMeshFinder.Succeeded() && GetMesh())
	{
		GetMesh()->SetSkeletalMesh(MannyMeshFinder.Object);
	}
	static ConstructorHelpers::FClassFinder<UAnimInstance> UnarmedABPFinder(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (UnarmedABPFinder.Succeeded() && GetMesh())
	{
		GetMesh()->SetAnimInstanceClass(UnarmedABPFinder.Class);
	}
	if (GetMesh())
	{
		// Capsule half-height is 140 for boss — feet at -140 below capsule center.
		GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -140.0f));
		GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	}
}

void ABossBase::BeginPlay()
{
	Super::BeginPlay();
	// AEnemyBase::BeginPlay already handles bIsBoss → forces ranged + Kite state.
	// Nothing extra needed here right now; subclasses can add boss-specific spawn FX.
}
