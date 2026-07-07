#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "CompanionNPC.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;
class UUserWidget;
class APlayerController;

// A rescued companion living at the Hub around the Time Sphere (see looped_rescue_system.md).
// Hidden until the companion's relic is owned; then they stand here permanently — the Hub grows
// from lonely prison to base camp ("the loop remembers me"). Placeholder figure until Sahar
// supplies the real model (Vorr / First Hunter pattern). Walk close for a greeting line.
UCLASS(Blueprintable)
class LOOPED_API ACompanionNPC : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	ACompanionNPC();

	// Press-E to TALK: rotates through LoreLines (deeper stories than the walk-up greeting).
	// Falls back to GreetingLines when no lore is set. Trainers (Lysa) open their TRAINING
	// MENU instead — a real page, since skills will multiply (Sahar keeps the "train with" tag).
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual FText GetInteractPrompt() const override;

	// --- Companion ROLE v1: chrono-skill trainer (Lysa, the skill merchant) ---
	// When set, E opens WBP_Trainer (Vorr-shop pattern: widget by path, rows bound C++-by-name,
	// walk-out auto-closes). Row 0 sells permanent chrono-gauge ranks until
	// ULoopedGameInstance::MaxSkillGaugeRanks; future skills claim the other rows.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Training")
	bool bOffersChronoTraining = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Training")
	int32 TrainingCostBase = 300;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Training")
	int32 TrainingCostStep = 150;

	// --- Companion ROLE v2: mission travel (Orin, the tutorial keeper) ---
	// When set, E travels to this level instead of talking — hub Orin replays the tutorial
	// ("E to replay tutorial", Sahar 2026-07-06). Wins over the trainer branch. None = off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Travel")
	FName TravelOnInteractLevel = NAME_None;

	// Prompt verb for the travel role ("Press [E] to <this>").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|Travel")
	FText TravelPromptVerb;

	// DT_Artifacts RowName whose ownership means "rescued" — the visibility gate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FName CompanionArtifact = FName(TEXT("Lysa"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	FText CompanionDisplayName;

	// Rotating greeting lines shown when the player walks up (rate-limited).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	TArray<FText> GreetingLines;

	// Deeper story lines told on press-E, in order (their history, the dungeon, the loop). The
	// tag advertises it ("<NAME>  [E]"). Empty = E reuses GreetingLines.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	TArray<FText> LoreLines;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion|FX")
	FLinearColor CompanionColor = FLinearColor(0.2f, 0.9f, 1.0f, 1.0f);

	// Turn (yaw only) to watch the player — same behavior as Vorr / the First Hunter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	bool bFacePlayer = true;

	// Degrees added so a real model's FRONT lines up (Meshy exports tend to need 270, like Vorr).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	float FaceYawOffset = 270.0f;

	// Gentle hover-bob (Mira: she reformed from fragments — she floats). Amplitude in uu.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	bool bBobUpDown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	float BobAmplitude = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Companion")
	float BobSpeed = 1.6f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	// Placeholder figure — swap for the real companion model later.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> NameTagComp;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

private:
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	int32 NextGreetingIndex = 0;
	int32 NextLoreIndex = 0;
	double LastGreetingTime = -100.0;

	// The mesh's authored rest Z, captured at BeginPlay — the bob oscillates around it.
	float BobRestZ = 0.0f;

	// --- Trainer menu state (Vorr-shop pattern) ---
	void OpenTrainingMenu(class ALoopedCharacter* Player);
	void CloseTrainingMenu();
	void RefreshTrainingMenu();
	void TryTrain(int32 SlotIndex);
	int32 NextTrainingCost() const; // cost of the NEXT rank (base + step * ranks owned)

	UFUNCTION() void OnTrain0();
	UFUNCTION() void OnTrainerClose();

	UFUNCTION()
	void OnTriggerEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY()
	TObjectPtr<UUserWidget> TrainerWidget;

	UPROPERTY()
	TObjectPtr<APlayerController> TrainerPC;

	bool bTrainerOpen = false;
};
