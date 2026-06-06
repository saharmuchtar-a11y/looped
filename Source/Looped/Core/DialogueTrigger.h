#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/DialogueData.h"
#include "DialogueTrigger.generated.h"

class USphereComponent;
class UUserWidget;
class UDataTable;
class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;

// Placeable event-room dialogue. Walks a DT_Dialogue node graph (branching choices) and applies each
// choice's outcome via the existing GameInstance / player systems. Drop one in an Event ("?") level,
// set DialogueTable + RootNode, and it fires when the player walks in.
UCLASS(Blueprintable)
class LOOPED_API ADialogueTrigger : public AActor
{
	GENERATED_BODY()

public:
	ADialogueTrigger();

	// Dialogue source table (rows of FDialogueNode) and the node to open on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	UDataTable* DialogueTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName RootNode;

	// If non-empty, BeginPlay picks ONE of these at random as RootNode — so a single reusable event
	// level (L_Event) shows a different event each visit. Add an event = add its root node here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FName> RandomRootNodes;

	// Auto-open the dialogue shortly after the level loads (event rooms) instead of waiting for the
	// player to walk into the trigger sphere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bAutoStartOnLoad = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	float TriggerRadius = 220.0f;

	// --- "?" marker: the thing the player walks up to. Hidden when the dialogue is done. ---
	// Defaults to the question_mark GLB (set in ctor); swap freely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Marker")
	UStaticMesh* MarkerMeshAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Marker")
	FVector MarkerScale = FVector(1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Marker")
	FVector MarkerOffset = FVector(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Marker")
	FLinearColor MarkerLightColor = FLinearColor(1.0f, 0.82f, 0.0f, 1.0f); // yellow

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Marker")
	float MarkerLightIntensity = 1500.0f;

	// Fire only once per level load (typical for a one-shot event room).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bOncePerLoad = true;

	// Open the dialogue at a specific node (also callable from BP / other triggers).
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void StartDialogue(FName NodeId);

	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void CloseDialogue();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Dialogue")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Dialogue")
	USphereComponent* Trigger;

	// The visible "?" marker the player approaches. Hidden in CloseDialogue.
	UPROPERTY(VisibleAnywhere, Category = "Dialogue|Marker")
	UStaticMeshComponent* MarkerMesh;

	UPROPERTY(VisibleAnywhere, Category = "Dialogue|Marker")
	UPointLightComponent* MarkerLight;

	// Hide/show the "?" marker (mesh + light) together.
	void SetMarkerVisible(bool bVisible);

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	// Choice button handlers (fixed Choice0..3 buttons in WBP_Dialogue).
	UFUNCTION() void OnChoice0();
	UFUNCTION() void OnChoice1();
	UFUNCTION() void OnChoice2();
	UFUNCTION() void OnChoice3();

	void ShowNode(const FDialogueNode& Node);
	void HandleChoice(int32 Index);
	void ApplyOutcome(const FDialogueChoice& Choice);

	UPROPERTY()
	UUserWidget* DialogueWidget = nullptr;

	UFUNCTION() void AutoStart();

	TArray<FDialogueChoice> CurrentChoices;
	FTimerHandle AutoStartTimer;
	bool bFired = false;
	bool bOpen = false;
};
