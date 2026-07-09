#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/WeaponData.h"
#include "WeaponHolderComponent.generated.h"

class UDataTable;
class UPassiveStackComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponHit, const FHitResult&, HitResult);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponChanged, FName, NewWeaponRowName);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LOOPED_API UWeaponHolderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponHolderComponent();

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnWeaponHit OnWeaponHit;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnWeaponChanged OnWeaponChanged;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Weapon")
	bool EquipWeapon(FName WeaponRowName);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Weapon")
	const FWeaponData& GetCurrentWeaponData() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Weapon")
	FName GetCurrentWeaponName() const { return CurrentWeaponRowName; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|Weapon")
	EWeaponFamily GetCurrentFamily() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Weapon")
	bool HasWeapon() const { return CurrentWeaponRowName != NAME_None; }

	void StartFiring();
	void StopFiring();

	// 0 = idle / not charging; 1 = full heavy charge. Used by POV Branch swell feedback.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Weapon")
	float GetMeleeChargeAlpha() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, Category = "Data")
	TObjectPtr<UDataTable> WeaponTable;

	UPROPERTY(EditDefaultsOnly, Category = "Defaults")
	FName StartingWeaponRowName = FName("Branch");

	// Global scalar on all outgoing player weapon damage. The weapon's own BaseDamage
	// (from the DataTable) is multiplied by this. 1.0 = full table damage; lower = weaker
	// player. Difficulty knob — tweakable in BP/defaults without touching the table.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 0.3f;

	// Hold M2 this long (seconds) before release counts as a heavy. Tap/release earlier = light.
	// Base is intentionally a beat longer so Fist of Steel (FoldedBreath) feels like a real cut.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.15"))
	float HeavyChargeTime = 0.85f;

	// Auto-fire heavy once charge reaches this (slightly above HeavyChargeTime). Prevents
	// holding forever; spam-hold still pays HeavyRecovery after the swing.
	// Kept ~0.20s above HeavyChargeTime so the ratio tracks when base charge is retuned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.2"))
	float HeavyAutoFireTime = 1.05f;

	// Damage / reach multipliers applied only to a heavy swing (cards still proc on hit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "1.0"))
	float HeavyDamageMult = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "1.0"))
	float HeavyRangeMult = 1.2f;

	// Post-heavy cadence gate (seconds). Longer than light AttackSpeed so hold-spam isn't free.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.1"))
	float HeavyRecovery = 0.55f;

	// --- Light-spam fatigue (Sahar: "I can spam it endlessly — makes the heavy useless") ---
	// Swings 1..N in a chain come at full speed; every light AFTER that pays extra recovery, so
	// sustained click-spam DPS drops below a charged-heavy rhythm. Heavies reset the chain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "1"))
	int32 LightChainFreeSwings = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "1.0"))
	float LightFatigueMultiplier = 1.9f;

	// Pause this long between swings and the chain forgives (fatigue resets).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.2"))
	float LightChainResetSeconds = 1.6f;

	// Heavy hits shove non-boss enemies back and interrupt their windup (see AEnemyBase::ApplyHeavyImpact).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.0"))
	float HeavyKnockbackSpeed = 420.0f;

	// Release inside this window right after the charge completes = PERFECT heavy (bonus damage,
	// harder knockback, deeper thunk). Scales with Fist of Steel's charge cut. Overholding into
	// auto-fire is always a normal heavy — the reward is for timing, not waiting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "0.0"))
	float PerfectReleaseWindow = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|MeleeCharge", meta = (ClampMin = "1.0"))
	float PerfectDamageBonus = 1.25f;

private:
	void FireWeapon();
	void PerformMeleeAttack();
	void PerformHitscanAttack();

	// Melee: begin charge on press; release (or auto at full) commits light/heavy swing.
	void BeginMeleeCharge();
	void CommitMeleeSwing(bool bHeavy);
	void CancelMeleeCharge();

	// Damage for one landed hit: weapon base x global multiplier, plus the Deadeye card's crit
	// roll (per target). bOutCrit reports whether this hit critted (for feedback/logging).
	float ComputeAttackDamage(bool& bOutCrit) const;

	// Effective heavy unlock / auto-fire times after Fist of Steel (FoldedBreath).
	float GetEffectiveHeavyChargeTime() const;
	float GetEffectiveHeavyAutoFireTime() const;

	// Light / heavy post-swing recovery after Agility (LoopCadence).
	float GetEffectiveLightRecovery() const;
	float GetEffectiveHeavyRecovery() const;

	// Loads CachedWeaponData.WeaponMesh and shows it on the owning hero's hand socket (none = hides it).
	void ApplyWeaponVisual();

	FName CurrentWeaponRowName = NAME_None;
	FWeaponData CachedWeaponData;

	bool bIsFiring = false;
	FTimerHandle FireTimerHandle;

	// Melee swing cadence gate: a swing is only allowed once AttackSpeed seconds have passed since
	// the last one. This caps the branch's hits/sec so mashing the button can't out-DPS the design
	// (the "click-fast-win" fix). Raise the weapon's AttackSpeed to make swings slower/heavier.
	float LastMeleeTime = -1000.0f;

	// Hold-to-charge melee (M2). Light on early release; heavy once past HeavyChargeTime.
	bool bMeleeCharging = false;
	float MeleeChargeStartTime = 0.0f;
	bool bNextSwingHeavy = false;
	float NextSwingRangeMult = 1.0f;
	float NextMeleeRecovery = 0.4f;
	int32 LightChainCount = 0;      // consecutive lights in the current chain (fatigue counter)
	bool bChargeReadyCued = false;  // "heavy ready" pop fired for the current charge
	bool bNextSwingPerfect = false; // released inside the perfect window

	// Combat SFX (loaded by path in BeginPlay). Swoosh on every swing, impact on a connecting hit.
	UPROPERTY()
	TObjectPtr<class USoundBase> SwooshSound;

	UPROPERTY()
	TObjectPtr<class USoundBase> ImpactSound;

	UPROPERTY()
	TObjectPtr<UPassiveStackComponent> CachedPassiveStack;
};
