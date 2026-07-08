#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/DialogueData.h"
#include "Core/LoopedInteractable.h"
#include "DialogueTrigger.generated.h"

class USphereComponent;
class UUserWidget;
class UDataTable;
class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class UWidgetComponent;

// Placeable event-room dialogue. Walks a DT_Dialogue node graph (branching choices) and applies each
// choice's outcome via the existing GameInstance / player systems. Drop one in an Event ("?") level,
// set DialogueTable + RootNode. Starting is a deliberate press-E near the marker (a floating "[E]"
// tag says so) — no more accidental mid-fight event pops; bAutoStartOnLoad rooms still auto-open.
UCLASS(Blueprintable)
class LOOPED_API ADialogueTrigger : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	ADialogueTrigger();

	// Press-E near the marker — same guards the old walk-in start used.
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return TriggerRadius + 60.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return (bOpen || (bOncePerLoad && bFired)) ? FText::GetEmpty() : FText::FromString(TEXT("investigate"));
	}

	// Dialogue source table (rows of FDialogueNode) and the node to open on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	UDataTable* DialogueTable = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName RootNode;

	// If non-empty, BeginPlay picks ONE of these at random as RootNode — so a single reusable event
	// level (L_Event) shows a different event each visit. Add an event = add its root node here.
	// These are the STORY pool; the GameInstance pity roll may divert to the pools below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FName> RandomRootNodes;

	// FIGHT-category events (ambushes/duels). Picked when GI::RollEventCategory() lands on fight —
	// odds GROW each peaceful "?" (StS pity). Empty = category falls back to RandomRootNodes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FName> FightRootNodes;

	// TREASURE-category events (loot/windfalls). Same pity behavior as fights.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FName> TreasureRootNodes;

	// Auto-open the dialogue shortly after the level loads (event rooms) instead of waiting for the
	// player to walk into the trigger sphere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	bool bAutoStartOnLoad = false;

	// --- Optional per-trigger palette (casino felt: green/black/red). Applied to WBP_Dialogue
	// every open, defaults restored for untinted triggers via the same call. ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme")
	bool bThemeDialogue = false;

	// Standalone table panel (WBP_CasinoTable as a screen overlay) instead of mounting inside
	// the arm monitor — Sahar: casino games shouldn't live in the monitor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme")
	bool bStandaloneWidget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme", meta = (EditCondition = "bThemeDialogue"))
	FLinearColor ThemePanelColor = FLinearColor(0.015f, 0.09f, 0.035f, 0.96f); // felt green

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme", meta = (EditCondition = "bThemeDialogue"))
	FLinearColor ThemeSpeakerColor = FLinearColor(0.95f, 0.18f, 0.18f); // table red

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme", meta = (EditCondition = "bThemeDialogue"))
	FLinearColor ThemeBodyColor = FLinearColor(0.88f, 0.96f, 0.88f); // chalk on felt

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme", meta = (EditCondition = "bThemeDialogue"))
	FLinearColor ThemeButtonColor = FLinearColor(0.03f, 0.16f, 0.06f); // deep green

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue|Theme", meta = (EditCondition = "bThemeDialogue"))
	FLinearColor ThemeResultColor = FLinearColor(0.95f, 0.18f, 0.18f); // wins/losses read in red

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

	// Floating "[E]" prompt under the marker — the visible invitation to interact. Hidden along
	// with the marker (and never shown for auto-start rooms).
	UPROPERTY(VisibleAnywhere, Category = "Dialogue|Marker")
	UWidgetComponent* PromptTagComp;

	// Hide/show the "?" marker (mesh + light + [E] prompt) together.
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
	// Applies the choice's outcome and RETURNS the "what happened" line ("+50 Shards"). The caller
	// routes it: into the dialogue's gold ResultText while the talk continues, or as a screen
	// message when the choice closes the dialogue (so nothing ever overlaps the open panel).
	FString ApplyOutcome(const FDialogueChoice& Choice);
	void SetResultLine(const FString& Result);

	// Paint (or restore) the dialogue widget's palette per this trigger's theme flags.
	void ApplyDialogueTheme();

	// --- Blackjack hand state (BlackjackDeal/Hit/Stand act on these between choices) ---
	int32 BJPlayerTotal = 0;
	int32 BJDealerUp = 0;
	int32 BJWager = 0; // 0 = no hand in play

	// Choice texts already fired this room-load for bOncePerRoom choices (the bar's drinks).
	TSet<FString> UsedOnceChoices;

	// Card faces for the WBP_Dialogue CardArea ("?" = the dealer's facedown card). Labels are
	// display-only flavor (a drawn 10 may wear J/Q/K); the totals above carry the truth.
	TArray<FString> BJDealerLabels;
	TArray<FString> BJPlayerLabels;
	TArray<bool> BJDealerRed;
	TArray<bool> BJPlayerRed;
	void PushBJCard(bool bDealer, int32 Value);
	void UpdateBJCardArea(bool bShow);

	UPROPERTY()
	UUserWidget* DialogueWidget = nullptr;

	UFUNCTION() void AutoStart();

	// --- Fight events (EDialogueOutcome::StartFight) ---
	// Spawns Count enemies of the DT_Enemies archetype around the trigger once the dialogue closes.
	// Exit portals stay locked until every spawned enemy is dead; winning pays artifact + shards.
	void SpawnFight();
	UFUNCTION() void OnFightEnemyDied(class AEnemyBase* Enemy);
	void OnFightWon();

	// Finishing a talk opens the room's exits — UNLESS the level has enemies placed in it that
	// weren't spawned by a dialogue fight (a combat event). In that case keep exits locked and
	// open them only when the last of those dies. No wager reward — this is just the gate, so it
	// stays separate from the OnFightWon() wager path.
	void OpenExitsOrGateOnRoomEnemies();
	UFUNCTION() void OnRoomEnemyDied(class AEnemyBase* Enemy);
	int32 RoomEnemiesAlive = 0;

	bool bFightPending = false;          // set by ApplyOutcome, consumed by CloseDialogue
	FName PendingFightRow;               // DT_Enemies row ("Random" allowed — rolls per enemy)
	int32 PendingFightCount = 0;
	int32 FightEnemiesAlive = 0;
	FTimerHandle FightSpawnTimer;

	TArray<FDialogueChoice> CurrentChoices;
	FTimerHandle AutoStartTimer;
	bool bFired = false;
	bool bOpen = false;
};
