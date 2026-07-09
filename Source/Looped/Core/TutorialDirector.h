#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TutorialDirector.generated.h"

class ACompanionCage;
class APortalActor;
class AEnemyBase;
class ALoopedCharacter;
class AGateActor;
class ALoopedLever;
class ADialogueTrigger;
class AActor;

// The stages of the first-launch tutorial, in teach order (Structure A — linear corridor):
// move+jump -> ride hazard platform -> pull lever -> chrono (Q) -> combat -> free Orin ->
// monitor open/close -> card draft -> portal home.
UENUM()
enum class ETutorialStage : uint8
{
	Movement,
	Hazard,       // ride the moving platform across the lava (reach HazardGoal)
	Lever,        // pull the training lever (opens Gate_ToArena)
	Chrono,
	Combat,
	Freed,        // cage open — waiting for the dialogue grant (HasArtifact(MonitorArtifact))
	Monitor,      // waiting for the first middle-click open
	MonitorClose, // let them read the missions panel; advance when they close it
	Cards,        // TriggerCardReward fired — waiting for the pick (hologram open -> closed)
	Done
};

// Scripted brain of L_Tutorial. Placed once in the level; walks the player through the
// teaching stages with reprompted center messages, opens corridor gates, spawns the combat
// wave, unlocks Orin's talk, and only reveals the exit portal after the full flow.
UCLASS(Blueprintable)
class LOOPED_API ATutorialDirector : public AActor
{
	GENERATED_BODY()

public:
	ATutorialDirector();

	// Orin's cage (puzzle mode). The director opens it when the combat stage clears.
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	TObjectPtr<ACompanionCage> Cage;

	// Orin's dialogue — locked until Freed so early E can't scramble the teach order.
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	TObjectPtr<ADialogueTrigger> OrinDialogue;

	// The way home. Starts disabled; activated at Done. Null = activate every portal (cage-style fallback).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	TObjectPtr<APortalActor> ExitPortal;

	// --- Corridor gates (closed at start; director Opens them as stages clear) ---
	// Opens when Movement completes → reveals the hazard crossing.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	TObjectPtr<AGateActor> GateAfterMove;

	// Opens when Hazard completes → reveals the lever alcove.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	TObjectPtr<AGateActor> GateAfterHazard;

	// Opens when Lever completes (also linked from the training lever) → reveals the arena.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	TObjectPtr<AGateActor> GateToArena;

	// Training lever — stage clears on HasBeenPulled().
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	TObjectPtr<ALoopedLever> TrainingLever;

	// Invisible/visible marker past the lava — player must get within HazardGoalRadius (uu).
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	TObjectPtr<AActor> HazardGoal;

	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	float HazardGoalRadius = 350.0f;

	// Also clear Hazard when the player's Y is at or south of this (world Y, corridor faces -Y).
	// Backup if the goal marker is missed while riding — default sits past the lava strip.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Gates")
	float HazardClearY = -650.0f;

	// Combat-wave enemies, spawned at the Combat stage (NOT pre-placed: they'd aggro during
	// the movement teach, and an empty level would trip the cage's no-enemies failsafe).
	UPROPERTY(EditAnywhere, Category = "Tutorial|Combat")
	TSubclassOf<AEnemyBase> EnemyClass;

	// DT_Enemies archetype applied to each spawn (ApplyEnemyType right after SpawnActor).
	UPROPERTY(EditAnywhere, Category = "Tutorial|Combat")
	FName EnemyTypeRow = TEXT("Voidling");

	// Where the wave appears — one enemy per point (TargetPoints placed in the level).
	UPROPERTY(EditAnywhere, Category = "Tutorial|Combat")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	// The artifact whose grant means "Orin gave you the monitor" (his dialogue's GrantArtifact).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	FName MonitorArtifact = TEXT("Orin");

	// 2D distance (uu) the player must cover in the movement stage (plus one jump).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	float MoveDistanceRequired = 900.0f;

	// Current instruction re-shows this often until its stage completes (center messages are transient).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	float RepromptSeconds = 9.0f;

	// --- Pacing (Sahar: "make it more linear"). A stage can't be *completed* until its
	// instruction has been on screen this long — stops the player blitzing past a teach beat.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Pacing")
	float MinStageSeconds = 3.0f;

	// After the player satisfies a stage, a short "well done" beat plays before the next
	// instruction — the flow reads as deliberate steps, not an instant chain.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Pacing")
	float ConfirmBeatSeconds = 2.0f;

	// Confirmation lines shown during the beat between action stages.
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgMoveDone;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgHazardDone;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgLeverDone;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgChronoDone;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgCombatDone;

	// --- Instruction lines (per-instance tunable; Orin's actual voice lives in DT_Dialogue) ---
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgMovement;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgHazard;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgLever;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgChrono;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgCombat;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgFreed;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgMonitor;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgMonitorPanel;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgCards;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgCombos;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgRelics;
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgPortal;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void EnterStage(ETutorialStage NewStage);
	// Latch a stage as satisfied: plays a confirm beat, then advances to Next after ConfirmBeatSeconds.
	void CompleteStage(ETutorialStage Next, const FText& Confirm);
	void SetPrompt(const FText& Text);
	void Say(const FText& Text, float Duration) const;
	void SpawnCombatWave();
	bool AllWaveEnemiesDead() const;
	void FireCardDraft();
	void FinishTutorial();
	void OpenGate(AGateActor* Gate) const;
	void LockOrinDialogue(bool bLocked) const;

	ALoopedCharacter* GetPlayerChar() const;

	ETutorialStage Stage = ETutorialStage::Movement;

	// Movement-stage tracking.
	FVector LastPlayerPos = FVector::ZeroVector;
	float DistanceMoved = 0.0f;
	bool bJumpSeen = false;
	bool bHavePlayerPos = false;

	// Reprompt bookkeeping.
	FText CurrentPrompt;
	double LastPromptTime = -100.0;

	// Pacing bookkeeping.
	double StageEnterTime = 0.0;      // when the current stage began (for MinStageSeconds gate)
	bool bStageCompleting = false;    // true during the confirm beat — freezes further advancing
	FTimerHandle StageAdvanceHandle;  // fires EnterStage(Next) after the beat

	// Cards-stage bookkeeping: the draft opens the hologram; the pick closes it.
	bool bDraftOpenSeen = false;

	TArray<TWeakObjectPtr<AEnemyBase>> WaveEnemies;

	FTimerHandle FinishTimerHandle;
};
