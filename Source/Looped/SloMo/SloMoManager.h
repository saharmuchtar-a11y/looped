#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SloMoManager.generated.h"

UENUM(BlueprintType)
enum class ESloMoTrigger : uint8
{
	CardSelection    UMETA(DisplayName = "Card Selection"),
	ActiveAbility    UMETA(DisplayName = "Active Ability"),
	MidCombatInspect UMETA(DisplayName = "Mid-Combat Inspect"),
	PivotOffer       UMETA(DisplayName = "Pivot Offer"),
	SkillDodge       UMETA(DisplayName = "Skill Dodge (Chrono)") // Q skill: player slowed too, just less
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSloMoStateChanged, bool, bIsSlowMo);

UCLASS()
class LOOPED_API USloMoManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|SloMo")
	FOnSloMoStateChanged OnSloMoStateChanged;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|SloMo")
	void RequestSloMo(ESloMoTrigger Trigger, float Duration = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|SloMo")
	void ReleaseSloMo(ESloMoTrigger Trigger);

	UFUNCTION(BlueprintPure, Category = "LOOPED|SloMo")
	bool IsSlowMo() const { return bIsSlowMo; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|SloMo")
	float GetCurrentTimeDilation() const;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	float TargetTimeDilation = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "Settings")
	float TransitionDuration = 0.2f;

	// --- SkillDodge (Q chrono skill) profile: a milder slow the PLAYER shares ---
	// World runs at SkillDilation; the player's exemption is fractional: net player speed =
	// Dilation^(1 - Fraction). Fraction 1 = fully exempt (menus, the old behavior); 0 = slowed
	// like the world. 0.66 at 0.35 world ≈ player at 0.70 — shoot/get shot, just slower.
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Skill")
	float SkillDilation = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Settings|Skill")
	float SkillPlayerExemptFraction = 0.66f;

private:
	bool bIsSlowMo = false;
	TSet<ESloMoTrigger> ActiveTriggers;
	TMap<ESloMoTrigger, FTimerHandle> TriggerTimers;

	// Active profile (recomputed on request/release): strongest slow among active triggers wins.
	float CurrentTargetDilation = 0.15f;
	float CurrentExemptFraction = 1.0f;
	void RecomputeProfile();

	// Smooth ramp state
	float TransitionStartRealTime = 0.0f;
	float TransitionStartDilation = 1.0f;
	bool bTransitioningToSlow = false;
	FTimerHandle TransitionTimerHandle;

	void EnterSloMo();
	void ExitSloMo();
	void UpdateTimeDilation(float Alpha);
	void StepTransition();
};
