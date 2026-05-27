#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "LoopedAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class LOOPED_API ULoopedAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	ULoopedAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FGameplayAttributeData BaseDamage;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, BaseDamage)

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FGameplayAttributeData DamageMultiplier;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, DamageMultiplier)

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FGameplayAttributeData CritChance;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, CritChance)

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FGameplayAttributeData CritMultiplier;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, CritMultiplier)

	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	FGameplayAttributeData AttackSpeed;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, AttackSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Movement")
	FGameplayAttributeData MoveSpeed;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, MoveSpeed)

	UPROPERTY(BlueprintReadOnly, Category = "Cards")
	FGameplayAttributeData PassiveProcRate;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, PassiveProcRate)

	// Meta attribute for incoming damage (not persisted)
	UPROPERTY(BlueprintReadOnly, Category = "Damage")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(ULoopedAttributeSet, IncomingDamage)
};
