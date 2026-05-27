#include "PassiveStackComponent.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "Looped.h"

UPassiveStackComponent::UPassiveStackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Slots.SetNum(MAX_PASSIVE_SLOTS);
}

void UPassiveStackComponent::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogLoopedCards, Display, TEXT("PassiveStackComponent initialized with %d slots"), MAX_PASSIVE_SLOTS);
}

bool UPassiveStackComponent::EquipCard(FName CardRowName, int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= MAX_PASSIVE_SLOTS)
	{
		UE_LOG(LogLoopedCards, Error, TEXT("EquipCard: invalid slot index %d"), SlotIndex);
		return false;
	}

	if (!ensure(PassiveCardTable))
	{
		UE_LOG(LogLoopedCards, Error, TEXT("EquipCard: PassiveCardTable is null"));
		return false;
	}

	FPassiveCardData* Data = PassiveCardTable->FindRow<FPassiveCardData>(CardRowName, TEXT("EquipCard"));
	if (!ensure(Data))
	{
		UE_LOG(LogLoopedCards, Error, TEXT("EquipCard: card '%s' not found in table"), *CardRowName.ToString());
		return false;
	}

	FPassiveSlot& Slot = Slots[SlotIndex];
	Slot.CardRowName = CardRowName;
	Slot.CachedData = *Data;
	Slot.TriggerCount = 0;
	Slot.EvolutionTier = 0;

	OnCardEquipped.Broadcast(SlotIndex, CardRowName);
	UE_LOG(LogLoopedCards, Display, TEXT("Equipped '%s' to slot %d"), *Data->DisplayName.ToString(), SlotIndex);
	return true;
}

void UPassiveStackComponent::RemoveCard(int32 SlotIndex)
{
	if (SlotIndex >= 0 && SlotIndex < MAX_PASSIVE_SLOTS)
	{
		Slots[SlotIndex] = FPassiveSlot();
	}
}

void UPassiveStackComponent::ClearAllSlots()
{
	for (int32 i = 0; i < MAX_PASSIVE_SLOTS; i++)
	{
		Slots[i] = FPassiveSlot();
	}
	UE_LOG(LogLoopedCards, Display, TEXT("All passive slots cleared"));
}

const FPassiveSlot& UPassiveStackComponent::GetSlot(int32 SlotIndex) const
{
	static FPassiveSlot EmptySlot;
	if (SlotIndex >= 0 && SlotIndex < MAX_PASSIVE_SLOTS)
	{
		return Slots[SlotIndex];
	}
	return EmptySlot;
}

int32 UPassiveStackComponent::GetFirstEmptySlot() const
{
	for (int32 i = 0; i < MAX_PASSIVE_SLOTS; i++)
	{
		if (Slots[i].IsEmpty()) return i;
	}
	return -1;
}

int32 UPassiveStackComponent::GetEquippedCount() const
{
	int32 Count = 0;
	for (const FPassiveSlot& Slot : Slots)
	{
		if (!Slot.IsEmpty()) Count++;
	}
	return Count;
}

void UPassiveStackComponent::EvaluatePassives(const FHitResult& Hit, EWeaponFamily WeaponFamily, AActor* HitActor)
{
	if (!HitActor) return;

	TMap<FGameplayTag, float> TagMagnitudes;

	// Collect phase: iterate slots, check affinity, gather tag magnitudes
	for (int32 i = 0; i < MAX_PASSIVE_SLOTS; i++)
	{
		FPassiveSlot& Slot = Slots[i];
		if (Slot.IsEmpty()) continue;

		const float AffinityMult = GetAffinityMultiplier(Slot.CachedData, WeaponFamily);
		const float EvolutionBonus = (Slot.EvolutionTier >= 2) ? Slot.CachedData.EvolutionTier2Bonus :
		                             (Slot.EvolutionTier >= 1) ? Slot.CachedData.EvolutionTier1Bonus : 0.0f;
		const float EffectiveMagnitude = (Slot.CachedData.Magnitude + EvolutionBonus) * AffinityMult;

		for (const FGameplayTag& Tag : Slot.CachedData.EffectTags)
		{
			TagMagnitudes.FindOrAdd(Tag) += EffectiveMagnitude; // Additive within same tag
		}

		// Cards That Remember: increment trigger count
		Slot.TriggerCount++;
		CheckEvolution(i);
	}

	if (TagMagnitudes.Num() == 0) return;

	// Cross-type multiplier: multiplicative across different Effect tags
	float CrossMultiplier = 1.0f;
	for (const auto& Pair : TagMagnitudes)
	{
		CrossMultiplier *= (1.0f + Pair.Value * 0.01f);
	}

	// Apply phase: broadcast each tag proc
	for (const auto& Pair : TagMagnitudes)
	{
		const float FinalMagnitude = Pair.Value * CrossMultiplier;
		OnPassiveProc.Broadcast(Pair.Key, FinalMagnitude);

		UE_LOG(LogLoopedCards, VeryVerbose, TEXT("Passive proc: %s = %.2f (cross mult: %.2f)"),
			*Pair.Key.ToString(), FinalMagnitude, CrossMultiplier);
	}
}

float UPassiveStackComponent::GetAffinityMultiplier(const FPassiveCardData& CardData, EWeaponFamily WeaponFamily) const
{
	if (CardData.bUniversalAffinity) return 1.0f;
	if (CardData.PrimaryAffinity == WeaponFamily) return 1.0f;
	return MismatchedAffinityMultiplier;
}

void UPassiveStackComponent::CheckEvolution(int32 SlotIndex)
{
	FPassiveSlot& Slot = Slots[SlotIndex];
	int32 OldTier = Slot.EvolutionTier;

	if (Slot.TriggerCount >= Slot.CachedData.EvolutionTier2Threshold && Slot.EvolutionTier < 2)
	{
		Slot.EvolutionTier = 2;
	}
	else if (Slot.TriggerCount >= Slot.CachedData.EvolutionTier1Threshold && Slot.EvolutionTier < 1)
	{
		Slot.EvolutionTier = 1;
	}

	if (Slot.EvolutionTier != OldTier)
	{
		OnCardEvolved.Broadcast(SlotIndex, Slot.CardRowName, Slot.EvolutionTier);
		UE_LOG(LogLoopedCards, Display, TEXT("Card '%s' evolved to tier %d (triggers: %d)"),
			*Slot.CachedData.DisplayName.ToString(), Slot.EvolutionTier, Slot.TriggerCount);
	}
}
