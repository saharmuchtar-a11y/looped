#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TutorialDirector.generated.h"

class ACompanionCage;
class APortalActor;
class AEnemyBase;
class ALoopedCharacter;

// The stages of the first-launch tutorial, in teach order (see looped_rescue_system.md):
// move -> chrono (Q) -> combat -> free Orin -> his dialogue grants the Arm Monitor ->
// middle-click teach -> missions/hints beat -> card draft (deck building) -> combos +
// relics lore -> portal home.
UENUM()
enum class ETutorialStage : uint8
{
	Movement,
	Chrono,
	Combat,
	Freed,        // cage open — waiting for the dialogue grant (HasArtifact(MonitorArtifact))
	Monitor,      // waiting for the first middle-click open
	MonitorClose, // let them read the missions panel; advance when they close it
	Cards,        // TriggerCardReward fired — waiting for the pick (hologram open -> closed)
	Done
};

// Scripted brain of L_Tutorial. Placed once in the level; walks the player through the
// teaching stages with reprompted center messages, spawns the combat wave, opens Orin's
// cage, and only reveals the exit portal after the full flow. The cage is set to puzzle
// mode (bOpenWhenEnemiesDead=false, bRevealPortalsOnOpen=false) — the director owns both.
UCLASS(Blueprintable)
class LOOPED_API ATutorialDirector : public AActor
{
	GENERATED_BODY()

public:
	ATutorialDirector();

	// Orin's cage (puzzle mode). The director opens it when the combat stage clears.
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	TObjectPtr<ACompanionCage> Cage;

	// The way home. Starts disabled; activated at Done. Null = activate every portal (cage-style fallback).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	TObjectPtr<APortalActor> ExitPortal;

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
	float MoveDistanceRequired = 800.0f;

	// Current instruction re-shows this often until its stage completes (center messages are transient).
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	float RepromptSeconds = 9.0f;

	// --- Instruction lines (per-instance tunable; Orin's actual voice lives in DT_Dialogue) ---
	UPROPERTY(EditAnywhere, Category = "Tutorial|Text", meta = (MultiLine = true)) FText MsgMovement;
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
	void SetPrompt(const FText& Text);
	void Say(const FText& Text, float Duration) const;
	void SpawnCombatWave();
	bool AllWaveEnemiesDead() const;
	void FireCardDraft();
	void FinishTutorial();

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

	// Cards-stage bookkeeping: the draft opens the hologram; the pick closes it.
	bool bDraftOpenSeen = false;

	TArray<TWeakObjectPtr<AEnemyBase>> WaveEnemies;

	FTimerHandle FinishTimerHandle;
};
