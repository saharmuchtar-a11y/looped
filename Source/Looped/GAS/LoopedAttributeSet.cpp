#include "LoopedAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "Looped.h"

ULoopedAttributeSet::ULoopedAttributeSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitBaseDamage(0.0f);
	InitDamageMultiplier(1.0f);
	InitCritChance(0.05f);
	InitCritMultiplier(2.0f);
	InitAttackSpeed(1.0f);
	InitMoveSpeed(700.0f);
	InitPassiveProcRate(1.0f);
	InitIncomingDamage(0.0f);
}

void ULoopedAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Single-player: no replication needed
}

void ULoopedAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMoveSpeedAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void ULoopedAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float DamageDone = GetIncomingDamage();
		SetIncomingDamage(0.0f);

		if (DamageDone > 0.0f)
		{
			const float NewHealth = GetHealth() - DamageDone;
			SetHealth(FMath::Clamp(NewHealth, 0.0f, GetMaxHealth()));

			UE_LOG(LogLoopedCombat, Verbose, TEXT("Damage applied: %.1f, Health: %.1f/%.1f"),
				DamageDone, GetHealth(), GetMaxHealth());

			if (GetHealth() <= 0.0f)
			{
				// Death handled by owning actor via delegate
			}
		}
	}
}
