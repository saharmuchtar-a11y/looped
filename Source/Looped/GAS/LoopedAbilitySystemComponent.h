#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "LoopedAbilitySystemComponent.generated.h"

UCLASS()
class LOOPED_API ULoopedAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	ULoopedAbilitySystemComponent();

	void InitializeAttributes();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|GAS")
	void ApplyDamageToTarget(AActor* Target, float Damage);
};
