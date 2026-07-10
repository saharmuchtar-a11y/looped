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
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

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

float UWeaponHolderComponent::GetEffectiveHeavyChargeTime() const
{
	float T = HeavyChargeTime;
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			// Card "Fist of Steel" (FoldedBreath): Fraction = charge-time cut (0.25 = 25% faster unlock).
			if (const FPassiveCardLevel* Lv = GI->GetEffectiveLevelData(TEXT("FoldedBreath")))
			{
				T *= FMath::Clamp(1.0f - FMath::Max(0.0f, Lv->Fraction), 0.35f, 1.0f);
			}
		}
	}
	return FMath::Max(0.15f, T);
}

float UWeaponHolderComponent::GetEffectiveHeavyAutoFireTime() const
{
	const float BaseCharge = FMath::Max(0.05f, HeavyChargeTime);
	const float EffCharge = GetEffectiveHeavyChargeTime();
	// Keep the same headroom above unlock as the untuned defaults (AutoFire - Charge).
	const float Headroom = FMath::Max(0.05f, HeavyAutoFireTime - HeavyChargeTime);
	return EffCharge + Headroom * (EffCharge / BaseCharge);
}

float UWeaponHolderComponent::GetEffectiveLightRecovery() const
{
	// Hard floor independent of DT_Weapons — bad row data must never re-enable endless click-spam.
	float Rec = FMath::Max(0.30f, CachedWeaponData.AttackSpeed);
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			// Card "Agility" (LoopCadence): Fraction = light-swing cooldown cut (does little to heavies).
			if (const FPassiveCardLevel* Lv = GI->GetEffectiveLevelData(TEXT("LoopCadence")))
			{
				Rec *= FMath::Clamp(1.0f - FMath::Max(0.0f, Lv->Fraction), 0.40f, 1.0f);
			}
		}
	}
	return FMath::Max(0.05f, Rec);
}

float UWeaponHolderComponent::GetEffectiveHeavyRecovery() const
{
	float Rec = HeavyRecovery;
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			// Agility (LoopCadence) only lightly trims heavy recovery (~35% of the light cut).
			if (const FPassiveCardLevel* Lv = GI->GetEffectiveLevelData(TEXT("LoopCadence")))
			{
				Rec *= FMath::Clamp(1.0f - FMath::Max(0.0f, Lv->Fraction) * 0.35f, 0.55f, 1.0f);
			}
		}
	}
	return FMath::Max(0.10f, Rec);
}

float UWeaponHolderComponent::GetMeleeChargeAlpha() const
{
	if (!bMeleeCharging || !GetWorld()) return 0.0f;
	const float Held = GetWorld()->GetTimeSeconds() - MeleeChargeStartTime;
	return FMath::Clamp(Held / FMath::Max(0.05f, GetEffectiveHeavyChargeTime()), 0.0f, 1.0f);
}

void UWeaponHolderComponent::StartFiring()
{
	bIsFiring = true;

	// Melee: hold-to-charge. Tap/release early = light; hold past HeavyChargeTime = heavy.
	if (CachedWeaponData.HitType == EHitType::Melee)
	{
		BeginMeleeCharge();
		return;
	}

	FireWeapon();

	if (CachedWeaponData.FireRate > 0.0f)
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

	if (bMeleeCharging)
	{
		const float Held = GetWorld()->GetTimeSeconds() - MeleeChargeStartTime;
		const bool bHeavy = Held >= GetEffectiveHeavyChargeTime();
		CommitMeleeSwing(bHeavy);
	}
}

void UWeaponHolderComponent::BeginMeleeCharge()
{
	if (!GetWorld() || bMeleeCharging) return;

	const float Now = GetWorld()->GetTimeSeconds();
	// Pressing during the previous swing's recovery still ARMS the charge — the clock just starts
	// when recovery ends. Going for a heavy right after a swing wastes no input; quick taps inside
	// recovery still commit nothing (CommitMeleeSwing eats releases before the clock starts).
	MeleeChargeStartTime = FMath::Max(Now, LastMeleeTime + FMath::Max(0.05f, NextMeleeRecovery));
	bMeleeCharging = true;
	bChargeReadyCued = false;
	SetComponentTickEnabled(true);

	if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(GetOwner()))
	{
		LC->SetMeleeChargeVisual(0.0f);
	}
}

void UWeaponHolderComponent::CancelMeleeCharge()
{
	bMeleeCharging = false;
	SetComponentTickEnabled(false);
	if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(GetOwner()))
	{
		LC->SetMeleeChargeVisual(0.0f);
	}
}

void UWeaponHolderComponent::CommitMeleeSwing(bool bHeavy)
{
	if (!bMeleeCharging) return;

	const float Now = GetWorld()->GetTimeSeconds();
	// Released while the previous swing's recovery still runs (charge clock hasn't started yet) —
	// no swing. This is the anti-spam gate: taps inside recovery do nothing, holds pass through.
	if (Now < MeleeChargeStartTime)
	{
		CancelMeleeCharge();
		return;
	}

	bMeleeCharging = false;
	SetComponentTickEnabled(false);

	// Fatigue chain: count consecutive lights (a real pause or a heavy resets it).
	if (bHeavy)
	{
		LightChainCount = 0;
	}
	else
	{
		const bool bChainAlive = (Now - LastMeleeTime) <= (LightChainResetSeconds + FMath::Max(0.0f, NextMeleeRecovery));
		LightChainCount = bChainAlive ? LightChainCount + 1 : 1;
	}

	bNextSwingHeavy = bHeavy;
	// Perfect release: let go inside the window right after the charge completed. Auto-fire
	// commits land past the window by construction (auto headroom 0.20 > window 0.15; both
	// scale with Fist of Steel's charge cut, so the card can't turn overholds into perfects).
	const float EffCharge = GetEffectiveHeavyChargeTime();
	const float WindowScale = EffCharge / FMath::Max(0.05f, HeavyChargeTime);
	bNextSwingPerfect = bHeavy
		&& ((Now - MeleeChargeStartTime) <= EffCharge + PerfectReleaseWindow * WindowScale);
	NextSwingRangeMult = bHeavy ? HeavyRangeMult : 1.0f;
	float Recovery = bHeavy ? GetEffectiveHeavyRecovery() : GetEffectiveLightRecovery();
	if (!bHeavy && LightChainCount > LightChainFreeSwings)
	{
		Recovery *= FMath::Max(1.0f, LightFatigueMultiplier); // tired arms — spam pays for itself
		if (ALoopedCharacter* TiredLC = Cast<ALoopedCharacter>(GetOwner()))
		{
			TiredLC->NotifyFatiguedSwing(); // droopy arc — the anti-spam must READ on screen
		}
	}
	NextMeleeRecovery = Recovery;
	LastMeleeTime = Now;

	if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(GetOwner()))
	{
		LC->SetMeleeChargeVisual(0.0f);
	}

	// FireWeapon requires bIsFiring; release path clears it first — force one committed swing.
	const bool bWasFiring = bIsFiring;
	bIsFiring = true;
	FireWeapon();
	bIsFiring = bWasFiring;

	UE_LOG(LogLoopedWeapons, Display, TEXT("Melee %s%s (chain=%d recovery=%.2f)"),
		bHeavy ? TEXT("HEAVY") : TEXT("light"),
		bNextSwingPerfect ? TEXT(" PERFECT") : TEXT(""),
		LightChainCount, NextMeleeRecovery);

	bNextSwingHeavy = false;
	bNextSwingPerfect = false;
	NextSwingRangeMult = 1.0f;
}

void UWeaponHolderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bMeleeCharging || !GetWorld()) return;

	const float Held = GetWorld()->GetTimeSeconds() - MeleeChargeStartTime;
	const float ChargeT = GetEffectiveHeavyChargeTime();
	const float Alpha = FMath::Clamp(Held / FMath::Max(0.05f, ChargeT), 0.0f, 1.0f);

	if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(GetOwner()))
	{
		LC->SetMeleeChargeVisual(Alpha);
		// One-shot release cue the instant the heavy unlocks — Branch pop + a sharp tick.
		if (!bChargeReadyCued && Held >= ChargeT)
		{
			bChargeReadyCued = true;
			LC->NotifyHeavyReady();
			if (SwooshSound)
			{
				UGameplayStatics::PlaySound2D(this, SwooshSound, 0.45f, 1.65f);
			}
		}
	}

	// Full charge: auto-commit heavy so holding forever isn't a stall.
	if (Held >= GetEffectiveHeavyAutoFireTime())
	{
		CommitMeleeSwing(true);
		bIsFiring = false;
	}
}

void UWeaponHolderComponent::FireWeapon()
{
	if (!bIsFiring) return;

	// The hero body / POV Branch swings with every shot — the hit is finally VISIBLE.
	if (ALoopedCharacter* HeroLC = Cast<ALoopedCharacter>(GetOwner()))
	{
		if (bNextSwingHeavy)
		{
			HeroLC->NotifyHeavyMeleeSwing();
		}
		HeroLC->PlayHeroAttackAnim(CachedWeaponData.HitType == EHitType::Melee);
		// POV stick slash (and classic unarmed montage). Melee waits for charge release.
		HeroLC->PlayRandomAttackAnim();
	}

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
	if (bNextSwingHeavy)
	{
		OutDamage *= HeavyDamageMult;
		if (bNextSwingPerfect)
		{
			OutDamage *= FMath::Max(1.0f, PerfectDamageBonus); // timed release pays extra
		}
	}

	// Crit roll: Deadeye card chance (Brittle-adjusted) + the Whetstone blessing's flat bonus.
	// Whetstone works alone (no Deadeye equipped) at the default x2 multiplier.
	if (const UWorld* World = GetWorld())
	{
		if (const ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>())
		{
			// Card "Strength" (WoundMemory): flat damage mult on every Branch hit (light + heavy).
			if (const FPassiveCardLevel* WoundLv = GI->GetEffectiveLevelData(TEXT("WoundMemory")))
			{
				OutDamage *= 1.0f + FMath::Max(0.0f, WoundLv->Fraction);
			}

			// Curse "DullBlade": crits simply do not happen this run.
			if (GI->HasCurse(TEXT("DullBlade")))
			{
				return OutDamage;
			}

			const FPassiveCardLevel* Lv = GI->GetEffectiveLevelData(TEXT("Deadeye"));
			const float CritChance = (Lv ? Lv->CritChance : 0.0f) + GI->GetArtifactCritChance();
			const float CritMult = Lv ? Lv->CritMultiplier : 2.0f;
			if (CritChance > 0.0f && FMath::FRand() < CritChance)
			{
				OutDamage *= FMath::Max(1.0f, CritMult);
				bOutCrit = true;
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
	const float RangeUU = CachedWeaponData.Range * 100.0f * NextSwingRangeMult;
	FVector End = Start + Forward * RangeUU;

	TArray<FHitResult> Hits;
	const float SphereRadius = bNextSwingHeavy ? 100.0f : 80.0f;
	FCollisionShape Shape = FCollisionShape::MakeSphere(SphereRadius);
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Owner);

	bool bConnected = false;
	TSet<const AActor*> ProcessedActors;
	if (GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Pawn, Shape, Params))
	{
		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || HitActor == Owner) continue;

			// SweepMulti reports one hit PER COMPONENT — an enemy's capsule + visual mesh both
			// respond, so the same enemy landed 2-3 hit results per swing and silently took
			// double/triple damage (+ card procs + stacked hurt sounds). Pay each actor ONCE.
			bool bAlreadyProcessed = false;
			ProcessedActors.Add(HitActor, &bAlreadyProcessed);
			if (bAlreadyProcessed) continue;

			OnWeaponHit.Broadcast(Hit);

			bool bCrit = false;
			const float OutDamage = ComputeAttackDamage(bCrit);

			// Apply damage to enemy (check both types)
			AEnemyBase* Enemy = Cast<AEnemyBase>(HitActor);
			if (Enemy)
			{
				// Heavy: shove + windup interrupt BEFORE damage — airborne enemies also skip their
				// on-hit dodge roll, so the knockback owns the moment (bosses shrug it off).
				if (bNextSwingHeavy && HeavyKnockbackSpeed > 0.0f)
				{
					Enemy->ApplyHeavyImpact(Forward, HeavyKnockbackSpeed * (bNextSwingPerfect ? 1.35f : 1.0f));
				}
				Enemy->TakeDamageFromPlayer(OutDamage, Owner);
				// THE real on-hit pipeline (deck Burn/Venom/Lifesteal/Cryo + ChainSpark + Echo +
				// StaticCapacitor + Static-curse gating). Built to replace the POC BP click chain,
				// but that chain was its only caller — the weapon is now its one true entry point.
				if (ALoopedCharacter* HitLC = Cast<ALoopedCharacter>(Owner))
				{
					HitLC->OnPlayerHitEnemy(Enemy);
				}
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

			UE_LOG(LogLoopedWeapons, Display, TEXT("Melee hit: %s for %.1f damage%s%s"),
				*HitActor->GetName(), OutDamage,
				bNextSwingHeavy ? TEXT(" (HEAVY)") : TEXT(""),
				bCrit ? TEXT(" (CRIT)") : TEXT(""));
		}
	}

	// Impact feedback ONCE per swing that connected. Lights keep the plain thunk + enemy red
	// flash (deliberately no shake inflation on every poke); a HEAVY that lands gets the full
	// weight package: deeper/louder thunk, micro hitstop, small camera kick — perfect most of all.
	if (bConnected)
	{
		if (ImpactSound)
		{
			const float Vol   = bNextSwingHeavy ? (bNextSwingPerfect ? 1.35f : 1.20f) : 1.0f;
			const float Pitch = bNextSwingHeavy ? (bNextSwingPerfect ? 0.70f : 0.80f) : 1.0f;
			UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Owner->GetActorLocation(), Vol, Pitch);
		}
		if (bNextSwingHeavy)
		{
			if (ALoopedCharacter* HeavyLC = Cast<ALoopedCharacter>(Owner))
			{
				HeavyLC->DoMeleeHitstop(bNextSwingPerfect ? 0.09f : 0.06f);
				HeavyLC->AddCameraShake(bNextSwingPerfect ? 1.0f : 0.7f);
			}
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
			// Same real on-hit pipeline as melee (deck effects + ChainSpark + Echo + relics).
			if (ALoopedCharacter* HitLC = Cast<ALoopedCharacter>(Owner))
			{
				HitLC->OnPlayerHitEnemy(Enemy);
			}
		}

		if (CachedPassiveStack)
		{
			CachedPassiveStack->EvaluatePassives(Hit, CachedWeaponData.PrimaryFamily, HitActor);
		}

		UE_LOG(LogLoopedWeapons, Display, TEXT("Hitscan hit: %s for %.1f damage%s"),
			*HitActor->GetName(), OutDamage, bCrit ? TEXT(" (CRIT)") : TEXT(""));
	}
}
