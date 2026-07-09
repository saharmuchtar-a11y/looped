#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/TreasureTypes.h"
#include "Core/LoopedInteractable.h"
#include "TreasureChest.generated.h"

class USceneComponent;
class UBoxComponent;
class UStaticMeshComponent;
class UWidgetComponent;

// One pedestal in a treasure room. Rolls + displays its offer on BeginPlay. Claiming is a
// DELIBERATE press-E (ILoopedInteractable) — walking near no longer grabs it, which matters a
// lot in Dark Treasure rooms where every pedestal is a cursed bargain. Grants the reward and —
// once the room's pick budget is spent — LOCKS its siblings (N of X).
UCLASS(Blueprintable)
class LOOPED_API ATreasureChest : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	ATreasureChest();

	// Press-E claim: same guards the old walk-in accept used.
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return 260.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return (bTaken || bLocked) ? FText::GetEmpty() : FText::FromString(TEXT("claim this offer"));
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	ETreasureRewardType RewardType = ETreasureRewardType::CleanRelic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	int32 CardBundleCount = 3;

	// Designer hooks for VFX/sound.
	UFUNCTION(BlueprintImplementableEvent, Category = "Treasure")
	void OnPedestalTaken(FName RewardId);

	UFUNCTION(BlueprintImplementableEvent, Category = "Treasure")
	void OnPedestalLocked();

	// Confirm + grant this pedestal's offer. Public so an [Accept] UI button can call it later.
	UFUNCTION(BlueprintCallable, Category = "Treasure")
	void AcceptPedestal();

	// Re-roll the displayed offer (used when RewardType is set after SpawnActor/BeginPlay).
	UFUNCTION(BlueprintCallable, Category = "Treasure")
	void RerollOffer() { RollOffer(); }

	// Boss / elite / champion fight reward: swap the cube for Sahar's question_mark GLB and
	// despawn mesh + floating tag when claimed (treasure-room chests keep the chest look).
	UFUNCTION(BlueprintCallable, Category = "Treasure")
	void ApplyQuestionMarkRewardVisual();

	// When true, AcceptPedestal hides mesh + offer text (and disables collision) instead of "TAKEN".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Treasure")
	bool bDespawnWhenTaken = false;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UStaticMeshComponent> PedestalMesh;

	// Floating offer display. Screen-space so it always faces the camera (no mirroring). The
	// actual WBP (WBP_TreasureSign) is auto-linked in the ctor; it binds OfferName/OfferDescription/CurseWarningText.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UWidgetComponent> FloatingWidgetComp;

	// Inspection-card fields the floating widget binds separately. Updated by RollOffer /
	// AcceptPedestal / LockPedestal. (Pushed into NameText / DescText / CurseText blocks.)
	UPROPERTY(BlueprintReadOnly, Category = "Treasure")
	FText OfferName;

	UPROPERTY(BlueprintReadOnly, Category = "Treasure")
	FText OfferDescription;

	// Empty for clean offers; populated for Cursed Bargains (drives the warning block + its color).
	UPROPERTY(BlueprintReadOnly, Category = "Treasure")
	FText CurseWarningText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Treasure")
	TObjectPtr<UBoxComponent> TriggerBox;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// Make this pedestal inert + relabel (called on siblings when the budget is spent).
	UFUNCTION(BlueprintCallable, Category = "Treasure")
	void LockPedestal();

private:
	void RollOffer(); // BeginPlay: roll + show this pedestal's offer

	// Push OfferName/OfferDescription/CurseWarningText into the live widget's NameText/DescText/
	// CurseText blocks (with a combined fallback to the legacy single OfferText block).
	void PushOfferTextToWidget();

	FName RolledArtifactId = NAME_None; // CleanRelic / CursedRelic
	bool bTaken = false;
	bool bLocked = false;

	// Played when the player takes this pedestal's reward (/Game/Audio/button).
	UPROPERTY()
	TObjectPtr<class USoundBase> PickupSound;
};
