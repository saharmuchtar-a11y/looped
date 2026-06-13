#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

// A visible, dodgeable enemy shot. Blocks on world geometry (so walls = real cover), passes through
// enemies, and damages only the player. Replaces the old instant hitscan ranged attack.
UCLASS()
class LOOPED_API AEnemyProjectile : public AActor
{
	GENERATED_BODY()

public:
	AEnemyProjectile();

	// Launch along the spawn's forward vector at Speed, dealing Damage to the player on hit. ElementId
	// (a DT_ProjectileElements RowName) sets the orb's color + a damage multiplier (color by class).
	void Init(float InDamage, float InSpeed, FName InElementId = FName(TEXT("None")));

protected:
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UProjectileMovementComponent> Movement;

	// Additive glow material instance for the orb (color set from the element row).
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> OrbMID;

	float Damage = 8.0f;

	// On-hit status from the element row (e.g. Ice → "Slow"), delivered to the player on impact.
	FName StatusEffect = NAME_None;
	float StatusMagnitude = 0.0f;
	float StatusDuration = 0.0f;

	// Blocking world hit (wall/prop) — die so geometry acts as cover.
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);

	// Pawn overlap — only the player takes damage; enemies (incl. the firer) are passed through.
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
};
