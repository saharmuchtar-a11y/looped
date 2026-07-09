#include "TutorialDirector.h"
#include "Core/CompanionCage.h"
#include "Core/PortalActor.h"
#include "Core/LoopedGameInstance.h"
#include "Core/GateActor.h"
#include "Core/LoopedLever.h"
#include "Core/DialogueTrigger.h"
#include "Enemies/EnemyBase.h"
#include "Player/LoopedCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Looped.h"

ATutorialDirector::ATutorialDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.15f; // state polling — no need for per-frame work

	// Defaults are the shipping lines; every one is per-instance tunable in the level.
	// Voice: short, plain words — no "glowing ground", no invented jargon walls.
	MsgMovement     = FText::FromString(TEXT("MOVE — [W][A][S][D].   JUMP — [SPACE]."));
	MsgHazard       = FText::FromString(TEXT("Lava burns. Ride the platform across."));
	MsgLever        = FText::FromString(TEXT("Pull the lever — [E]. It opens the way."));
	MsgChrono       = FText::FromString(TEXT("Press [Q] — time slows. Use it to dodge."));
	MsgCombat       = FText::FromString(TEXT("Enemies! [LEFT CLICK] to fight."));
	MsgFreed        = FText::FromString(TEXT("He is free. Talk to him — [E]."));
	MsgMonitor      = FText::FromString(TEXT("Press [MIDDLE CLICK] — open your Arm Monitor."));
	MsgMonitorPanel = FText::FromString(TEXT("Objectives and hints live here. Close it when ready."));
	MsgCards        = FText::FromString(TEXT("Pick a card. Your deck is your build."));
	MsgCombos       = FText::FromString(TEXT("Some cards work better together. Watch for pairs."));
	MsgRelics       = FText::FromString(TEXT("Relics hide in deeper rooms. Keep what you find."));
	MsgPortal       = FText::FromString(TEXT("Touch the Sphere — it takes you home."));

	MsgMoveDone     = FText::FromString(TEXT("Good."));
	MsgHazardDone   = FText::FromString(TEXT("You made it across."));
	MsgLeverDone    = FText::FromString(TEXT("Open. Remember levers like this."));
	MsgChronoDone   = FText::FromString(TEXT("Nice. Hold that feeling."));
	MsgCombatDone   = FText::FromString(TEXT("Clear. Well fought."));
}

void ATutorialDirector::BeginPlay()
{
	Super::BeginPlay();

	// Orin stays silent until Freed — stops early E grant scrambling the teach order.
	LockOrinDialogue(true);
	// Lever stays dead until the Lever stage — through-wall E was toggling Gate_ToArena early
	// (desync / crash) while Gate_AfterHazard was still closed.
	if (TrainingLever) { TrainingLever->SetInteractionEnabled(false); }

	// Replays run the FULL flow on purpose (hub Orin's "E to replay tutorial" — it stays a
	// practice space). With the monitor already owned, the Freed stage self-advances on its
	// next poll (dialogue grant is idempotent), so nothing blocks or double-grants.
	// On replay the exit portal ALSO stands open from the start — practice must never trap
	// you (Sahar: "no way to leave the tutorial"). Fresh saves earn it at Done; if one bails
	// through a stray exit anyway, the Hub's routing just bounces them back in.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasArtifact(MonitorArtifact) && ExitPortal)
		{
			ExitPortal->ActivatePortal();
		}
	}
	EnterStage(ETutorialStage::Movement);
}

void ATutorialDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ALoopedCharacter* Player = GetPlayerChar();
	if (!Player) return;

	// During a confirm beat the stage is settled — hold the screen and wait for the timer.
	if (bStageCompleting) return;

	// Transient center messages fade — re-show the current instruction until its stage lands.
	const double Now = FPlatformTime::Seconds();
	if (!CurrentPrompt.IsEmpty() && Now - LastPromptTime > RepromptSeconds)
	{
		LastPromptTime = Now;
		Say(CurrentPrompt, 4.0f);
	}

	// A teach beat can't be *completed* until its instruction has been up long enough to read.
	const bool bStageReady = (Now - StageEnterTime) >= MinStageSeconds;

	switch (Stage)
	{
	case ETutorialStage::Movement:
	{
		const FVector Pos = Player->GetActorLocation();
		if (bHavePlayerPos)
		{
			const float Step = FVector::Dist2D(Pos, LastPlayerPos);
			if (Step < 500.0f) { DistanceMoved += Step; } // ignore teleport spikes
		}
		LastPlayerPos = Pos;
		bHavePlayerPos = true;
		if (Player->JumpCurrentCount > 0) { bJumpSeen = true; }

		if (bStageReady && DistanceMoved >= MoveDistanceRequired && bJumpSeen)
		{
			OpenGate(GateAfterMove); // immediate — don't wait for confirm beat
			CompleteStage(ETutorialStage::Hazard, MsgMoveDone);
		}
		break;
	}

	case ETutorialStage::Hazard:
	{
		if (!bStageReady) break;
		const FVector Pos = Player->GetActorLocation();
		const bool bPastLava = Pos.Y <= HazardClearY;
		const bool bAtGoal = HazardGoal
			&& FVector::Dist2D(Pos, HazardGoal->GetActorLocation()) <= HazardGoalRadius;
		if (bPastLava || bAtGoal)
		{
			// Open NOW — don't wait for the confirm-beat timer (Sahar: "wall won't go down").
			OpenGate(GateAfterHazard);
			CompleteStage(ETutorialStage::Lever, MsgHazardDone);
		}
		break;
	}

	case ETutorialStage::Lever:
		if (bStageReady && TrainingLever && TrainingLever->HasBeenPulled())
		{
			OpenGate(GateToArena); // immediate — director owns this gate (lever has no LinkedGates)
			CompleteStage(ETutorialStage::Chrono, MsgLeverDone);
		}
		else if (bStageReady && !TrainingLever)
		{
			UE_LOG(LogLoopedRun, Warning, TEXT("[Tutorial] No TrainingLever set — skipping lever stage."));
			OpenGate(GateToArena);
			CompleteStage(ETutorialStage::Chrono, MsgLeverDone);
		}
		break;

	case ETutorialStage::Chrono:
		if (bStageReady && Player->IsChronoSkillActive())
		{
			CompleteStage(ETutorialStage::Combat, MsgChronoDone);
		}
		break;

	case ETutorialStage::Combat:
		if (AllWaveEnemiesDead())
		{
			CompleteStage(ETutorialStage::Freed, MsgCombatDone);
		}
		break;

	case ETutorialStage::Freed:
		// Orin's dialogue [E] carries the grant (GrantArtifact outcome) — the monitor moment.
		if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasArtifact(MonitorArtifact))
			{
				EnterStage(ETutorialStage::Monitor);
			}
		}
		break;

	case ETutorialStage::Monitor:
		if (Player->IsHologramOpen())
		{
			EnterStage(ETutorialStage::MonitorClose);
		}
		break;

	case ETutorialStage::MonitorClose:
		if (!Player->IsHologramOpen())
		{
			EnterStage(ETutorialStage::Cards);
		}
		break;

	case ETutorialStage::Cards:
		// TriggerCardReward re-opened the hologram in reward mode; the pick closes it.
		if (Player->IsHologramOpen()) { bDraftOpenSeen = true; }
		else if (bDraftOpenSeen)
		{
			EnterStage(ETutorialStage::Done);
		}
		break;

	case ETutorialStage::Done:
	default:
		break;
	}
}

void ATutorialDirector::CompleteStage(ETutorialStage Next, const FText& Confirm)
{
	if (bStageCompleting) return; // one latch per stage
	bStageCompleting = true;

	// Stop reprompting the instruction; play the confirmation, then advance after the beat.
	CurrentPrompt = FText::GetEmpty();
	if (!Confirm.IsEmpty())
	{
		Say(Confirm, ConfirmBeatSeconds + 0.5f);
	}

	TWeakObjectPtr<ATutorialDirector> WeakThis(this);
	GetWorldTimerManager().SetTimer(StageAdvanceHandle, FTimerDelegate::CreateLambda([WeakThis, Next]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->EnterStage(Next);
		}
	}), FMath::Max(0.1f, ConfirmBeatSeconds), false);
}

void ATutorialDirector::EnterStage(ETutorialStage NewStage)
{
	Stage = NewStage;
	bStageCompleting = false;
	StageEnterTime = FPlatformTime::Seconds();
	UE_LOG(LogLoopedRun, Display, TEXT("[Tutorial] Stage -> %d"), static_cast<int32>(NewStage));

	switch (NewStage)
	{
	case ETutorialStage::Movement:
		SetPrompt(MsgMovement);
		break;

	case ETutorialStage::Hazard:
		OpenGate(GateAfterMove); // idempotent if already opened from CompleteStage
		SetPrompt(MsgHazard);
		break;

	case ETutorialStage::Lever:
		OpenGate(GateAfterHazard);
		if (TrainingLever) { TrainingLever->SetInteractionEnabled(true); }
		SetPrompt(MsgLever);
		break;

	case ETutorialStage::Chrono:
		OpenGate(GateToArena);
		SetPrompt(MsgChrono);
		break;

	case ETutorialStage::Combat:
		SetPrompt(MsgCombat);
		SpawnCombatWave();
		break;

	case ETutorialStage::Freed:
		if (Cage) { Cage->OpenCage(); }
		LockOrinDialogue(false);
		SetPrompt(MsgFreed);
		break;

	case ETutorialStage::Monitor:
		// Exit opens only at Done (FinishTutorial). Replays already stand open from BeginPlay.
		SetPrompt(MsgMonitor);
		break;

	case ETutorialStage::MonitorClose:
		SetPrompt(FText::GetEmpty());
		Say(MsgMonitorPanel, 7.0f);
		break;

	case ETutorialStage::Cards:
		SetPrompt(FText::GetEmpty());
		Say(MsgCards, 5.0f);
		bDraftOpenSeen = false;
		FireCardDraft();
		break;

	case ETutorialStage::Done:
	{
		SetPrompt(FText::GetEmpty());
		Say(MsgCombos, 5.5f);
		// Combos beat first, relics beat second, then the portal — paced, not a wall of text.
		TWeakObjectPtr<ATutorialDirector> WeakThis(this);
		GetWorldTimerManager().SetTimer(FinishTimerHandle, FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->Say(WeakThis->MsgRelics, 5.5f);
				WeakThis->FinishTutorial();
			}
		}), 5.5f, false);
		SetActorTickEnabled(false);
		break;
	}
	}
}

void ATutorialDirector::SetPrompt(const FText& Text)
{
	CurrentPrompt = Text;
	LastPromptTime = FPlatformTime::Seconds();
	if (!Text.IsEmpty())
	{
		Say(Text, 5.0f);
	}
}

void ATutorialDirector::Say(const FText& Text, float Duration) const
{
	if (ALoopedCharacter* Player = GetPlayerChar())
	{
		Player->ShowCenterMessage(Text, Duration);
	}
}

void ATutorialDirector::OpenGate(AGateActor* Gate) const
{
	if (Gate) { Gate->Open(); }
}

void ATutorialDirector::LockOrinDialogue(bool bLocked) const
{
	if (OrinDialogue)
	{
		OrinDialogue->SetInteractionEnabled(!bLocked);
	}
}

void ATutorialDirector::SpawnCombatWave()
{
	if (!EnemyClass)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Tutorial] No EnemyClass set — combat stage auto-clears."));
		return;
	}
	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (const AActor* Point : SpawnPoints)
	{
		if (!Point) continue;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, Point->GetActorTransform(), Params);
		if (!Enemy) continue;
		if (!EnemyTypeRow.IsNone())
		{
			Enemy->ApplyEnemyType(EnemyTypeRow); // right-after-spawn is the supported window
		}
		WaveEnemies.Add(Enemy);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Tutorial] Combat wave spawned: %d enemies."), WaveEnemies.Num());
}

bool ATutorialDirector::AllWaveEnemiesDead() const
{
	for (const TWeakObjectPtr<AEnemyBase>& E : WaveEnemies)
	{
		if (E.IsValid() && E->IsAlive()) return false;
	}
	return true;
}

void ATutorialDirector::FireCardDraft()
{
	// Same proven path as Mira's reroll: the BP manager owns the draft flow end to end.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->GetName() == TEXT("BP_CardRewardManager_C"))
		{
			if (UFunction* Fn = It->FindFunction(TEXT("TriggerCardReward")))
			{
				It->ProcessEvent(Fn, nullptr);
				return;
			}
		}
	}
	// No manager in the level — don't strand the flow; skip straight to the outro.
	UE_LOG(LogLoopedRun, Warning, TEXT("[Tutorial] No BP_CardRewardManager found — skipping the card teach."));
	EnterStage(ETutorialStage::Done);
}

void ATutorialDirector::FinishTutorial()
{
	if (ExitPortal)
	{
		ExitPortal->ActivatePortal();
	}
	else
	{
		// Cage-style fallback: light up whatever portals the level has.
		for (TActorIterator<APortalActor> It(GetWorld()); It; ++It)
		{
			It->ActivatePortal();
		}
	}
	Say(MsgPortal, 5.0f);
	UE_LOG(LogLoopedRun, Display, TEXT("[Tutorial] Complete — exit portal active."));
}

ALoopedCharacter* ATutorialDirector::GetPlayerChar() const
{
	return Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
}
