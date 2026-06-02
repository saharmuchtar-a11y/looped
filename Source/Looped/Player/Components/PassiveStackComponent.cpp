#include "PassiveStackComponent.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "Core/LoopedGameInstance.h"
#include "Looped.h"

UPassiveStackComponent::UPassiveStackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPassiveStackComponent::BeginPlay()
{
	Super::BeginPlay();
	// The deck is owned by the GameInstance (persists across hard OpenLevel). Cache it.
	if (UWorld* World = GetWorld())
	{
		CachedGI = World->GetGameInstance<ULoopedGameInstance>();
	}
	UE_LOG(LogLoopedCards, Display, TEXT("PassiveStackComponent ready (deck-backed by GameInstance, %d slots cap)"), MAX_PASSIVE_SLOTS);
}

TArray<FPassiveSlot>* UPassiveStackComponent::GetDeck() const
{
	return CachedGI.IsValid() ? &CachedGI->RunDeck : nullptr;
}

bool UPassiveStackComponent::EquipCard(FName CardRowName, int32 /*SlotIndex*/)
{
	// Slot index is obsolete now that the deck is a dynamic, GI-owned array; this just
	// equips/levels the card. Kept for the legacy Blueprint call site.
	const int32 NewLevel = AddOrLevelCard(CardRowName);
	if (NewLevel <= 0) return false;

	if (TArray<FPassiveSlot>* Deck = GetDeck())
	{
		const int32 Idx = Deck->IndexOfByPredicate([&](const FPassiveSlot& S){ return S.CardRowName == CardRowName; });
		if (Idx != INDEX_NONE) OnCardEquipped.Broadcast(Idx, CardRowName);
	}
	return true;
}

void UPassiveStackComponent::RemoveCard(int32 SlotIndex)
{
	if (TArray<FPassiveSlot>* Deck = GetDeck())
	{
		if (Deck->IsValidIndex(SlotIndex)) Deck->RemoveAt(SlotIndex);
	}
}

void UPassiveStackComponent::ClearAllSlots()
{
	if (CachedGI.IsValid()) CachedGI->ClearRunDeck();
	UE_LOG(LogLoopedCards, Display, TEXT("Run deck cleared"));
}

const FPassiveSlot& UPassiveStackComponent::GetSlot(int32 SlotIndex) const
{
	static FPassiveSlot EmptySlot;
	const TArray<FPassiveSlot>* Deck = GetDeck();
	if (Deck && Deck->IsValidIndex(SlotIndex)) return (*Deck)[SlotIndex];
	return EmptySlot;
}

int32 UPassiveStackComponent::GetFirstEmptySlot() const
{
	const TArray<FPassiveSlot>* Deck = GetDeck();
	if (!Deck) return -1;
	return (Deck->Num() < MAX_PASSIVE_SLOTS) ? Deck->Num() : -1;
}

int32 UPassiveStackComponent::GetEquippedCount() const
{
	const TArray<FPassiveSlot>* Deck = GetDeck();
	return Deck ? Deck->Num() : 0;
}

int32 UPassiveStackComponent::FindCardLevel(FName CardId) const
{
	return CachedGI.IsValid() ? CachedGI->GetCardLevel(CardId) : 0;
}

int32 UPassiveStackComponent::AddOrLevelCard(FName CardId)
{
	return CachedGI.IsValid() ? CachedGI->AddOrLevelCard(CardId) : 0;
}

// --- Phase 2 (GAS combat) territory — iterates the same GI-owned deck. Untouched logic. ---

void UPassiveStackComponent::EvaluatePassives(const FHitResult& Hit, EWeaponFamily WeaponFamily, AActor* HitActor)
{
	if (!HitActor) return;
	TArray<FPassiveSlot>* Deck = GetDeck();
	if (!Deck) return;

	TMap<FGameplayTag, float> TagMagnitudes;

	for (int32 i = 0; i < Deck->Num(); ++i)
	{
		FPassiveSlot& Slot = (*Deck)[i];
		if (Slot.IsEmpty()) continue;

		const float AffinityMult = GetAffinityMultiplier(Slot.CachedData, WeaponFamily);
		const float EvolutionBonus = (Slot.EvolutionTier >= 2) ? Slot.CachedData.EvolutionTier2Bonus :
		                             (Slot.EvolutionTier >= 1) ? Slot.CachedData.EvolutionTier1Bonus : 0.0f;
		// NOTE: live magnitude now comes from FPassiveCardData.Levels (Phase 1); evolution
		// scaling here is Phase-2 territory and intentionally inert until that pass.
		const float BaseMag = Slot.CachedData.Levels.IsValidIndex(Slot.Level - 1) ? Slot.CachedData.Levels[Slot.Level - 1].Damage : 0.0f;
		const float EffectiveMagnitude = (BaseMag + EvolutionBonus) * AffinityMult;

		for (const FGameplayTag& Tag : Slot.CachedData.EffectTags)
		{
			TagMagnitudes.FindOrAdd(Tag) += EffectiveMagnitude;
		}

		Slot.TriggerCount++;
		CheckEvolution(i);
	}

	if (TagMagnitudes.Num() == 0) return;

	float CrossMultiplier = 1.0f;
	for (const auto& Pair : TagMagnitudes) CrossMultiplier *= (1.0f + Pair.Value * 0.01f);

	for (const auto& Pair : TagMagnitudes)
	{
		OnPassiveProc.Broadcast(Pair.Key, Pair.Value * CrossMultiplier);
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
	TArray<FPassiveSlot>* Deck = GetDeck();
	if (!Deck || !Deck->IsValidIndex(SlotIndex)) return;
	FPassiveSlot& Slot = (*Deck)[SlotIndex];
	const int32 OldTier = Slot.EvolutionTier;

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
	}
}
