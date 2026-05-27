#include "LoopedAbilitySystemComponent.h"
#include "LoopedAttributeSet.h"
#include "Looped.h"

ULoopedAbilitySystemComponent::ULoopedAbilitySystemComponent()
{
	SetIsReplicatedByDefault(false);
}

void ULoopedAbilitySystemComponent::InitializeAttributes()
{
	if (!GetAttributeSet(ULoopedAttributeSet::StaticClass()))
	{
		GetOrCreateAttributeSubobject(ULoopedAttributeSet::StaticClass());
	}
}

void ULoopedAbilitySystemComponent::ApplyDamageToTarget(AActor* Target, float Damage)
{
	if (!ensure(Target))
	{
		UE_LOG(LogLoopedGAS, Error, TEXT("ApplyDamageToTarget: null target"));
		return;
	}

	UAbilitySystemComponent* TargetASC = Target->FindComponentByClass<UAbilitySystemComponent>();
	if (!ensure(TargetASC))
	{
		UE_LOG(LogLoopedGAS, Warning, TEXT("ApplyDamageToTarget: target %s has no ASC"), *Target->GetName());
		return;
	}

	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(GetOwner());

	FGameplayEffectSpec* Spec = new FGameplayEffectSpec();
	// For POC: direct attribute modification
	TargetASC->ApplyModToAttribute(ULoopedAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage);
}
