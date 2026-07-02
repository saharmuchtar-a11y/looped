#include "WeaponHolderComponent.h"
#include "PassiveStackComponent.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/SimpleEnemy.h"
#include "Player/LoopedCharacter.h"
#include "Core/LoopedGameInstance.h"
#include "Data/PassiveCardData.h"
#include "Looped.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

UWeaponHolderComponent::UWeaponHolderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<USoundBase> SwooshSnd(TEXT("/Game/Audio/melee_swoosh.melee_swoosh"));
	if (SwooshSnd.Succeeded()) SwooshSound = SwooshSnd.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactSnd(TEXT("/Game/Audio/melee_impact.melee_impact"));
	if (ImpactSnd.Succeeded()) ImpactSound = ImpactSnd.Object;
}

void UWeaponHolderComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedPassiveStack = GetOwner()->FindComponentByClass<UPassiveStackComponent>();

	if (WeaponTable && StartingWeaponRowName != NAME_None)
	{
		EquipWeapon(StartingWeaponRowName); // applies the weapon visual too
	}
	else
	{
		// No weapon table/row assigned — use the FWeaponData defaults (a basic melee weapon) so the
		// player is never statless. Real weapons come from DT_Weapons; the old hardcoded-Branch POC
		// fallback is gone (stats now live entirely in the DataTable).
		UE_LOG(LogLoopedWeapons, Warning, TEXT("WeaponHolder: no WeaponTable/StartingWeaponRowName set — using default weapon stats."));
		ApplyWeaponVisual();
	}
}

bool UWeaponHolderComponent::EquipWeapon(FName WeaponRowName)
{
	if (!WeaponTable)
	{
		UE_LOG(LogLoopedWeapons, Warning, TEXT("EquipWeapon: WeaponTable is null, using POC defaults"));
		return false;
	}

	FWeaponData* Data = WeaponTable->FindRow<FWeaponData>(WeaponRowName, TEXT("EquipWeapon"));
	if (!ensure(Data))
	{
		UE_LOG(LogLoopedWeapons, Error, TEXT("EquipWeapon: '%s' not found in table"), *WeaponRowName.ToString());
		return false;
	}

	CurrentWeaponRowName = WeaponRowName;
	CachedWeaponData = *Data;

	ApplyWeaponVisual();

	OnWeaponChanged.Broadcast(WeaponRowName);
	UE_LOG(LogLoopedWeapons, Display, TEXT("Equipped weapon: %s (%s family)"),
		*Data->DisplayName.ToString(),
		*UEnum::GetValueAsString(Data->PrimaryFamily));
	return true;
}

void UWeaponHolderComponent::ApplyWeaponVisual()
{
	ALoopedCharacter* LC = Cast<ALoopedCharacter>(GetOwner());
	if (!LC) return;

	// Soft pointer → load on demand. Null/unset = clear the held weapon (hero shows unarmed).
	UStaticMesh* Mesh = CachedWeaponData.WeaponMesh.IsNull()
		? nullptr
		: CachedWeaponData.WeaponMesh.LoadSynchronous();
	LC->SetWeaponVisualMesh(Mesh);
}

const FWeaponData& UWeaponHolderComponent::GetCurrentWeaponData() const
{
	return CachedWeaponData;
}

EWeaponFamily UWeaponHolderComponent::GetCurrentFamily() const
{
	return CachedWeaponData.PrimaryFamily;
}

void UWeaponHolderComponent::StartFiring()
{
	bIsFiring = true;

	// Melee swing cadence: ignore the input if we swung within AttackSpeed seconds. This caps the
	// branch's hits/sec so spamming the button can't out-DPS the intended pace (click-fast-win fix).
	if (CachedWeaponData.HitType == EHitType::Melee)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastMeleeTime < FMath::Max(0.05f, CachedWeaponData.AttackSpeed))
		{
			return;
		}
		LastMeleeTime = Now;
	}

	FireWeapon();

	if (CachedWeaponData.HitType != EHitType::Melee && CachedWeaponData.FireRate > 0.0f)
	{
		const float Interval = 1.0f / CachedWeaponData.FireRate;
		GetWorld()->GetTimerManager().SetTimer(FireTimerHandle, this,
			&UWeaponHolderComponent::FireWeapon, Interval, true);
	}
}

void UWeaponHolderComponent::StopFiring()
{
	bIsFiring = false;
	GetWorld()->GetTimerManager().ClearTimer(FireTimerHandle);
}

void UWeaponHolderComponent::FireWeapon()
{
	if (!bIsFiring) return;

	switch (CachedWeaponData.HitType)
	{
	case EHitType::Melee:
		PerformMeleeAttack();
		break;
	case EHitType::Hitscan:
		PerformHitscanAttack();
		break;
	case EHitType::Projectile:
		PerformHitscanAttack();
		break;
	}
}

float UWeaponHolderComponent::ComputeAttackDamage(bool& bOutCrit) const
{
	bOutCrit = false;
	float OutDamage = CachedWeaponData.BaseDamage * DamageMultiplier;

	// Deadeye card: chance for this hit to crit, values from the equipped card's per-level row
	// (Brittle-adjusted via GetEffectiveLevelData — same source as every other card effect).
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			if (const FPassiveCardLevel* Lv = GI->GetEffectiveLevelData(TEXT("Deadeye")))
			{
				if (Lv->CritChance > 0.0f && FMath::FRand() < Lv->CritChance)
				{
					OutDamage *= FMath::Max(1.0f, Lv->CritMultiplier);
					bOutCrit = true;
				}
			}
		}
	}
	return OutDamage;
}

void UWeaponHolderComponent::PerformMeleeAttack()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	APlayerController* PC = Cast<APlayerController>(Cast<APawn>(Owner)->GetController());
	if (!PC || !PC->PlayerCameraManager) return;

	// Whoosh on every swing (hit or miss) — sells the attack.
	if (SwooshSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, SwooshSound, Owner->GetActorLocation());
	}

	FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	FVector Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();
	FVector End = Start + Forward * CachedWeaponData.Range * 100.0f;

	TArray<FHitResult> Hits;
	FCollisionShape Shape = FCollisionShape::MakeSphere(80.0f);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	bool bConnected = false;
	if (GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, Shape, Params))
	{
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == Owner) continue;

			OnWeaponHit.Broadcast(Hit);

			bool bCrit = false;
			const float OutDamage = ComputeAttackDamage(bCrit);

			// Apply damage to enemy (check both types)
			AEnemyBase* Enemy = Cast<AEnemyBase>(HitActor);
			if (Enemy)
			{
				Enemy->TakeDamageFromPlayer(OutDamage, Owner);
				bConnected = true;
			}
			ASimpleEnemy* SimpleEnemy = Cast<ASimpleEnemy>(HitActor);
			if (SimpleEnemy)
			{
				SimpleEnemy->TakeDamageFromPlayer(OutDamage, Owner);
				bConnected = true;
			}

			if (CachedPassiveStack)
			{
				CachedPassiveStack->EvaluatePassives(Hit, CachedWeaponData.PrimaryFamily, HitActor);
			}

			UE_LOG(LogLoopedWeapons, Display, TEXT("Melee hit: %s for %.1f damage%s"),
				*HitActor->GetName(), OutDamage, bCrit ? TEXT(" (CRIT)") : TEXT(""));
		}
	}

	// Impact feedback ONCE per swing that connected: thunk sound. (No camera shake on landing a hit —
	// the enemy's red hit-flash already reads the connect clearly. Shake is kept for TAKING damage.)
	if (bConnected)
	{
		if (ImpactSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Owner->GetActorLocation());
		}
	}
}

void UWeaponHolderComponent::PerformHitscanAttack()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	APlayerController* PC = Cast<APlayerController>(Cast<APawn>(Owner)->GetController());
	if (!PC || !PC->PlayerCameraManager) return;

	FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	FVector Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();
	FVector End = Start + Forward * CachedWeaponData.Range * 100.0f;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params))
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor) return;

		OnWeaponHit.Broadcast(Hit);

		bool bCrit = false;
		const float OutDamage = ComputeAttackDamage(bCrit);

		AEnemyBase* Enemy = Cast<AEnemyBase>(HitActor);
		if (Enemy)
		{
			Enemy->TakeDamageFromPlayer(OutDamage, Owner);
		}

		if (CachedPassiveStack)
		{
			CachedPassiveStack->EvaluatePassives(Hit, CachedWeaponData.PrimaryFamily, HitActor);
		}

		UE_LOG(LogLoopedWeapons, Display, TEXT("Hitscan hit: %s for %.1f damage%s"),
			*HitActor->GetName(), OutDamage, bCrit ? TEXT(" (CRIT)") : TEXT(""));
	}
}
