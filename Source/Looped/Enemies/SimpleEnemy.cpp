#include "SimpleEnemy.h"
#include "Looped.h"

ASimpleEnemy::ASimpleEnemy()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = MeshComp;

	MeshComp->SetCollisionProfileName(TEXT("Pawn"));
	MeshComp->SetGenerateOverlapEvents(true);
	MeshComp->SetSimulatePhysics(false);
}

void ASimpleEnemy::BeginPlay()
{
	Super::BeginPlay();
	CurrentHealth = MaxHealth;
	UE_LOG(LogLoopedAI, Display, TEXT("SimpleEnemy spawned with %.0f HP"), CurrentHealth);
}

void ASimpleEnemy::TakeDamageFromPlayer(float Damage, AActor* DamageSource)
{
	if (!IsAlive()) return;

	CurrentHealth -= Damage;

	// Shrink as damage feedback
	float Pct = FMath::Max(CurrentHealth / MaxHealth, 0.0f);
	float S = FMath::Lerp(0.4f, 1.0f, Pct);
	SetActorScale3D(FVector(S, S, S));

	UE_LOG(LogLoopedCombat, Display, TEXT("SimpleEnemy hit! %.1f damage, HP: %.0f/%.0f"), Damage, CurrentHealth, MaxHealth);

	if (CurrentHealth <= 0.0f)
	{
		UE_LOG(LogLoopedCombat, Display, TEXT("SimpleEnemy KILLED!"));
		SetLifeSpan(0.2f);
	}
}
