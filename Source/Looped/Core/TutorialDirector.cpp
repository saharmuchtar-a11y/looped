#include "TutorialDirector.h"
#include "Core/CompanionCage.h"
#include "Core/PortalActor.h"
#include "Core/LoopedGameInstance.h"
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
	MsgMovement     = FText::FromString(TEXT("MOVE — [W][A][S][D].   JUMP — [SPACE]."));
	MsgChrono       = FText::FromString(TEXT("Press [Q] — bend time. The world slows; you slow less. Made for dodging."));
	MsgCombat       = FText::FromString(TEXT("Void-spawn! [LEFT CLICK] — cut them down."));
	MsgFreed        = FText::FromString(TEXT("He is free. Speak with him — [E]."));
	MsgMonitor      = FText::FromString(TEXT("Press [MIDDLE CLICK] — raise the Arm Monitor. It slows the world and shields you while open."));
	MsgMonitorPanel = FText::FromString(TEXT("The monitor remembers for you — objectives below, Orin's whispers above. Close it when ready."));
	MsgCards        = FText::FromString(TEXT("A frequency surges. Choose a card — your deck is your build."));
	MsgCombos       = FText::FromString(TEXT("Frequencies resonate. Certain pairs sing together — find the combos."));
	MsgRelics       = FText::FromString(TEXT("Relics wait in the deep rooms. Artifacts outlive the loop itself."));
	MsgPortal       = FText::FromString(TEXT("The way out is open."));
}

void ATutorialDirector::BeginPlay()
{
	Super::BeginPlay();

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

	// Transient center messages fade — re-show the current instruction until its stage lands.
	const double Now = FPlatformTime::Seconds();
	if (!CurrentPrompt.IsEmpty() && Now - LastPromptTime > RepromptSeconds)
	{
		LastPromptTime = Now;
		Say(CurrentPrompt, 4.0f);
	}

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

		if (DistanceMoved >= MoveDistanceRequired && bJumpSeen)
		{
			EnterStage(ETutorialStage::Chrono);
		}
		break;
	}

	case ETutorialStage::Chrono:
		if (Player->IsChronoSkillActive())
		{
			EnterStage(ETutorialStage::Combat);
		}
		break;

	case ETutorialStage::Combat:
		if (AllWaveEnemiesDead())
		{
			EnterStage(ETutorialStage::Freed);
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

void ATutorialDirector::EnterStage(ETutorialStage NewStage)
{
	Stage = NewStage;
	UE_LOG(LogLoopedRun, Display, TEXT("[Tutorial] Stage -> %d"), static_cast<int32>(NewStage));

	switch (NewStage)
	{
	case ETutorialStage::Movement:
		SetPrompt(MsgMovement);
		break;

	case ETutorialStage::Chrono:
		SetPrompt(MsgChrono);
		break;

	case ETutorialStage::Combat:
		SetPrompt(MsgCombat);
		SpawnCombatWave();
		break;

	case ETutorialStage::Freed:
		if (Cage) { Cage->OpenCage(); }
		SetPrompt(MsgFreed);
		break;

	case ETutorialStage::Monitor:
		// The monitor is on the arm — from this moment the Sphere is the way out (Sahar:
		// exit available "from the moment i take the monitor"). The exit portal is the
		// invisible trigger hugging the sphere, hub-style; its label lights up now.
		if (ExitPortal) { ExitPortal->ActivatePortal(); }
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
