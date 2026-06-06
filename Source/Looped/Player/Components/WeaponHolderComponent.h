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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Data")
	TObjectPtr<UDataTable> WeaponTable;

	UPROPERTY(EditDefaultsOnly, Category = "Defaults")
	FName StartingWeaponRowName = FName("Branch");

	// Global scalar on all outgoing player weapon damage. The weapon's own BaseDamage
	// (from the DataTable) is multiplied by this. 1.0 = full table damage; lower = weaker
	// player. Difficulty knob — tweakable in BP/defaults without touching the table.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 0.3f;

private:
	void FireWeapon();
	void PerformMeleeAttack();
	void PerformHitscanAttack();

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

	// Combat SFX (loaded by path in BeginPlay). Swoosh on every swing, impact on a connecting hit.
	UPROPERTY()
	TObjectPtr<class USoundBase> SwooshSound;

	UPROPERTY()
	TObjectPtr<class USoundBase> ImpactSound;

	UPROPERTY()
	TObjectPtr<UPassiveStackComponent> CachedPassiveStack;
};
