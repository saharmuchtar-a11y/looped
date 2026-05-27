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
	PivotOffer       UMETA(DisplayName = "Pivot Offer")
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

private:
	bool bIsSlowMo = false;
	TSet<ESloMoTrigger> ActiveTriggers;
	TMap<ESloMoTrigger, FTimerHandle> TriggerTimers;

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
