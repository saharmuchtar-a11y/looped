#include "WeaponHolderComponent.h"
#include "PassiveStackComponent.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/SimpleEnemy.h"
#include "Looped.h"
#include "Engine/World.h"

UWeaponHolderComponent::UWeaponHolderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWeaponHolderComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedPassiveStack = GetOwner()->FindComponentByClass<UPassiveStackComponent>();

	if (WeaponTable && StartingWeaponRowName != NAME_None)
	{
		EquipWeapon(StartingWeaponRowName);
	}
	else
	{
		// POC fallback: hardcoded Branch
		CurrentWeaponRowName = FName("Branch");
		CachedWeaponData.DisplayName = FText::FromString("Branch");
		CachedWeaponData.PrimaryFamily = EWeaponFamily::Blade;
		CachedWeaponData.HitType = EHitType::Melee;
		CachedWeaponData.BaseDamage = 15.0f;
		CachedWeaponData.AttackSpeed = 0.4f;
		CachedWeaponData.Range = 3.0f;
		CachedWeaponData.ScreenShakeMagnitude = 1.0f;
		UE_LOG(LogLoopedWeapons, Display, TEXT("POC: Branch equipped with hardcoded stats"));
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

	OnWeaponChanged.Broadcast(WeaponRowName);
	UE_LOG(LogLoopedWeapons, Display, TEXT("Equipped weapon: %s (%s family)"),
		*Data->DisplayName.ToString(),
		*UEnum::GetValueAsString(Data->PrimaryFamily));
	return true;
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

void UWeaponHolderComponent::PerformMeleeAttack()
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

	APlayerController* PC = Cast<APlayerController>(Cast<APawn>(Owner)->GetController());
	if (!PC || !PC->PlayerCameraManager) return;

	FVector Start = PC->PlayerCameraManager->GetCameraLocation();
	FVector Forward = PC->PlayerCameraManager->GetCameraRotation().Vector();
	FVector End = Start + Forward * CachedWeaponData.Range * 100.0f;

	TArray<FHitResult> Hits;
	FCollisionShape Shape = FCollisionShape::MakeSphere(80.0f);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	if (GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, Shape, Params))
	{
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == Owner) continue;

			OnWeaponHit.Broadcast(Hit);

			const float OutDamage = CachedWeaponData.BaseDamage * DamageMultiplier;

			// Apply damage to enemy (check both types)
			AEnemyBase* Enemy = Cast<AEnemyBase>(HitActor);
			if (Enemy)
			{
				Enemy->TakeDamageFromPlayer(OutDamage, Owner);
			}
			ASimpleEnemy* SimpleEnemy = Cast<ASimpleEnemy>(HitActor);
			if (SimpleEnemy)
			{
				SimpleEnemy->TakeDamageFromPlayer(OutDamage, Owner);
			}

			if (CachedPassiveStack)
			{
				CachedPassiveStack->EvaluatePassives(Hit, CachedWeaponData.PrimaryFamily, HitActor);
			}

			UE_LOG(LogLoopedWeapons, Display, TEXT("Melee hit: %s for %.1f damage"),
				*HitActor->GetName(), OutDamage);
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

		const float OutDamage = CachedWeaponData.BaseDamage * DamageMultiplier;

		AEnemyBase* Enemy = Cast<AEnemyBase>(HitActor);
		if (Enemy)
		{
			Enemy->TakeDamageFromPlayer(OutDamage, Owner);
		}

		if (CachedPassiveStack)
		{
			CachedPassiveStack->EvaluatePassives(Hit, CachedWeaponData.PrimaryFamily, HitActor);
		}

		UE_LOG(LogLoopedWeapons, Display, TEXT("Hitscan hit: %s for %.1f damage"),
			*HitActor->GetName(), OutDamage);
	}
}
