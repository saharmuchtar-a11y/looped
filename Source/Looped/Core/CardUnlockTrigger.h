#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CardUnlockTrigger.generated.h"

class UBoxComponent;
class UUserWidget;

/**
 * Drop-in trigger volume that permanently unlocks a card when the player enters it.
 * Used at the Hub parkour summit to unlock the Gravity card (reaching the top is the gate).
 * Reusable for other secret unlocks — just set CardToUnlock per-instance.
 */
UCLASS()
class LOOPED_API ACardUnlockTrigger : public AActor
{
	GENERATED_BODY()

public:
	ACardUnlockTrigger();

	// Card row/perk name to unlock on overlap (e.g. "Gravity"). Leave None to skip.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED")
	FName CardToUnlock = TEXT("Gravity");

	// Artifact to grant on overlap (e.g. "Wing"). Leave None to skip.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED")
	FName ArtifactToGrant = NAME_None;

	// Optional gate: only fires if this perk has ever been maxed (e.g. "Gravity").
	// Leave None for no gate. Used for the secret chain (max Gravity -> sphere grants artifact).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED")
	FName RequiredMaxedPerk = NAME_None;

	// Centered, death-screen-style message shown ONCE — only the first time the card is
	// unlocked (never again, since the unlock is saved). Leave blank for no message.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED")
	FText UnlockMessage = FText::FromString(TEXT("I feel lighter now..."));

	UPROPERTY(EditAnywhere, Category = "LOOPED")
	float MessageDuration = 3.5f;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED")
	TSubclassOf<UUserWidget> MessageWidgetClass;

protected:
	UPROPERTY(VisibleAnywhere, Category = "LOOPED")
	TObjectPtr<UBoxComponent> Trigger;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	bool bFired = false;
};
