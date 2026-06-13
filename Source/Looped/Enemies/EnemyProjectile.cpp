#include "EnemyProjectile.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/LoopedCharacter.h"
#include "Enemies/EnemyBase.h"
#include "Data/ProjectileTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DataTable.h"
#include "Looped.h"

AEnemyProjectile::AEnemyProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = 4.0f; // self-clean if it never hits anything

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(18.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);   // walls = cover
	Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);  // props
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);        // player/enemies (filtered)
	Collision->SetNotifyRigidBodyCollision(true);
	Collision->OnComponentHit.AddDynamic(this, &AEnemyProjectile::OnHit);
	Collision->OnComponentBeginOverlap.AddDynamic(this, &AEnemyProjectile::OnOverlap);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.35f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
	// Additive glow material so the orb reads as energy; element row tints it (color by class).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> OrbMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (OrbMat.Succeeded())
	{
		Mesh->SetMaterial(0, OrbMat.Object);
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->InitialSpeed = 1800.0f;
	Movement->MaxSpeed = 1800.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->ProjectileGravityScale = 0.0f; // straight shot, dodge by strafing
}

void AEnemyProjectile::Init(float InDamage, float InSpeed, FName InElementId)
{
	Damage = InDamage;

	// Resolve the element (color by class + damage scaling) from DT_ProjectileElements.
	FLinearColor OrbColor(0.5f, 0.5f, 0.5f, 1.0f); // default grey orb
	if (UDataTable* ElemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_ProjectileElements.DT_ProjectileElements")))
	{
		if (const FProjectileElementData* Row = ElemTable->FindRow<FProjectileElementData>(InElementId, TEXT("ProjectileElement"), false))
		{
			OrbColor = Row->OrbColor;
			Damage = InDamage * Row->DamageMultiplier;
			// Carry the element's on-hit status — delivered to the player in OnOverlap.
			StatusEffect = Row->StatusEffect;
			StatusMagnitude = Row->StatusMagnitude;
			StatusDuration = Row->StatusDuration;
		}
	}

	if (Mesh)
	{
		OrbMID = Mesh->CreateDynamicMaterialInstance(0);
		if (OrbMID)
		{
			OrbMID->SetVectorParameterValue(TEXT("OverlayColor"), OrbColor);
		}
	}

	if (Movement)
	{
		Movement->InitialSpeed = InSpeed;
		Movement->MaxSpeed = InSpeed;
		Movement->Velocity = GetActorForwardVector() * InSpeed;
	}

	// Pass straight through the FIRER's own collision. Mooks have no-collision cubes, but the BOSS
	// uses a BlockAllDynamic proxy for melee hits — without this its own body blocks/destroys the
	// projectile on the spawn frame (invisible shot, no damage).
	if (AActor* Own = GetOwner())
	{
		if (Collision) Collision->IgnoreActorWhenMoving(Own, true);
	}
}

void AEnemyProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// Pass through enemy bodies (incl. the firer's collision proxy); only real world geometry stops it.
	if (Cast<AEnemyBase>(OtherActor)) return;

	// Hit blocking geometry — destroy so walls/props are real cover.
	Destroy();
}

void AEnemyProjectile::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!OtherActor || OtherActor == this) return;

	// Pass straight through enemies (including the firer); only the player takes a hit.
	if (Cast<AEnemyBase>(OtherActor)) return;

	if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor))
	{
		Player->TakeDamageFromEnemy(Damage);
		// Elemental on-hit status (Ice chills, Fire burns, Venom poisons) — no-op when the row has none.
		if (!StatusEffect.IsNone())
		{
			Player->ApplyElementalStatus(StatusEffect, StatusMagnitude, StatusDuration);
		}
		Destroy();
	}
}
