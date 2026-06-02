#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Data/PassiveCardData.h"
#include "Data/WeaponData.h"
#include "PassiveStackComponent.generated.h"

class UDataTable;
class ULoopedGameInstance;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPassiveProc, const FGameplayTag&, EffectTag, float, Magnitude);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCardEquipped, int32, SlotIndex, FName, CardRowName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCardEvolved, int32, SlotIndex, FName, CardRowName, int32, NewTier);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class LOOPED_API UPassiveStackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPassiveStackComponent();

	static constexpr int32 MAX_PASSIVE_SLOTS = 6;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnPassiveProc OnPassiveProc;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnCardEquipped OnCardEquipped;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnCardEvolved OnCardEvolved;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Cards")
	bool EquipCard(FName CardRowName, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Cards")
	void RemoveCard(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Cards")
	void ClearAllSlots();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	const FPassiveSlot& GetSlot(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	int32 GetFirstEmptySlot() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	int32 GetEquippedCount() const;

	// Deck queries — operate on the persistent run deck (GameInstance-owned), NOT a local copy.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Cards")
	int32 FindCardLevel(FName CardId) const;

	// Equip (if new) or level up a card this run. Returns the new level.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Cards")
	int32 AddOrLevelCard(FName CardId);

	void EvaluatePassives(const FHitResult& Hit, EWeaponFamily WeaponFamily, AActor* HitActor);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Data")
	TObjectPtr<UDataTable> PassiveCardTable;

	UPROPERTY(EditDefaultsOnly, Category = "Balance")
	float MismatchedAffinityMultiplier = 0.5f;

private:
	// The deck lives on the GameInstance (survives hard OpenLevel). This component is a
	// behavior/API layer over it — it holds NO copy of the slots (single source of truth).
	TWeakObjectPtr<ULoopedGameInstance> CachedGI;
	TArray<FPassiveSlot>* GetDeck() const;

	float GetAffinityMultiplier(const FPassiveCardData& CardData, EWeaponFamily WeaponFamily) const;
	void CheckEvolution(int32 SlotIndex);
};
