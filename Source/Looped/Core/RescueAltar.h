#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "RescueAltar.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWidgetComponent;

// Hub launcher for a companion rescue mission (see looped_rescue_system.md). Three states,
// evaluated at BeginPlay:
//   RESCUED  — companion relic owned → the altar is gone (the companion stands at the Hub instead).
//   DARK     — gate not met → dim, unlabeled; touching it only whispers that something must be proven.
//   LIT      — gate met → glows in the companion's color, labeled "RESCUE: <NAME>"; touch to travel
//              to the mission level.
// One altar per companion; everything is per-instance data so the same class serves all four.
UCLASS(Blueprintable)
class LOOPED_API ARescueAltar : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	ARescueAltar();

	// Press-E launch — walking near never yanks you out of the Hub anymore. Dark altars answer
	// E with the cold whisper (and offer no prompt, keeping their mystery).
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return 260.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return bLit ? FText::FromString(FString::Printf(TEXT("begin %s's rescue"), *CompanionDisplayName.ToString()))
		            : FText::GetEmpty();
	}

	// DT_Artifacts RowName of the companion relic — owning it means "already rescued".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FName CompanionArtifact = FName(TEXT("Lysa"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FText CompanionDisplayName;

	// Level opened when a lit altar is touched.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FName MissionLevel = FName(TEXT("L_Rescue_Lysa"));

	// Gate: require the first boss kill (same proof-of-persistence gate as Vorr's vault).
	// Flip off for altars gated some other way (or not at all).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	bool bRequireVaultUnlock = true;

	// --- Chain gates (the approved rescue ladder — each companion opens the next) ---
	// Relic that must ALREADY be owned before this altar lights (e.g. Brann requires "Lysa").
	// None = no chain requirement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FName RequiredArtifact = NAME_None;

	// Lifetime boss kills required before this altar lights (e.g. Brann = 3). 0 = no requirement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	int32 RequiredBossKills = 0;

	// Shown center-screen when the mission launches (the fade-out beat).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue", meta = (MultiLine = true))
	FText LaunchMessage;

	// Shown (rate-limited) when a DARK altar is touched.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue", meta = (MultiLine = true))
	FText ColdMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue|FX")
	FLinearColor LitColor = FLinearColor(0.2f, 0.9f, 1.0f, 1.0f);

	// Seconds between the launch message and the OpenLevel (lets the beat land).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	float TravelDelay = 1.2f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	// Placeholder pedestal — swap for real altar art later (like Vorr / the First Hunter).
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> AltarMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

	// Floating "RESCUE: LYSA" tag (WBP_NameTag, screen-space — same as portals/Vorr).
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> NameTagComp;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

private:
	void LaunchMission();

	bool bLit = false;
	bool bTraveling = false;
	double LastColdMessageTime = -100.0; // rate-limits the dark-altar whisper
	FTimerHandle TravelTimerHandle;
};
