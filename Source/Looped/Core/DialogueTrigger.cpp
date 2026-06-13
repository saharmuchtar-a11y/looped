#include "DialogueTrigger.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Enemies/EnemyBase.h"
#include "Data/EnemyData.h"
#include "Data/ArtifactData.h"
#include "Looped.h"

ADialogueTrigger::ADialogueTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(SceneRoot);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);

	// "?" marker — the visible thing the player walks up to. Hidden once the dialogue is done.
	MarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerMesh"));
	MarkerMesh->SetupAttachment(SceneRoot);
	MarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> QMark(TEXT("/Game/new_assets/question_mark/StaticMeshes/question_mark.question_mark"));
	if (QMark.Succeeded())
	{
		MarkerMeshAsset = QMark.Object;
		MarkerMesh->SetStaticMesh(MarkerMeshAsset);
	}

	MarkerLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MarkerLight"));
	MarkerLight->SetupAttachment(SceneRoot);
	MarkerLight->SetIntensityUnits(ELightUnits::Lumens);
	MarkerLight->SetIntensity(MarkerLightIntensity);
	MarkerLight->SetAttenuationRadius(600.0f);
	MarkerLight->SetLightColor(MarkerLightColor);
	MarkerLight->SetCastShadows(false);
}

void ADialogueTrigger::SetMarkerVisible(bool bVisible)
{
	if (MarkerMesh)  MarkerMesh->SetVisibility(bVisible);
	if (MarkerLight) MarkerLight->SetVisibility(bVisible);
}

void ADialogueTrigger::BeginPlay()
{
	Super::BeginPlay();
	Trigger->SetSphereRadius(TriggerRadius);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ADialogueTrigger::OnOverlap);

	// Apply marker editor values + show it (the player walks up to this to start the talk).
	if (MarkerMesh)
	{
		if (MarkerMeshAsset) MarkerMesh->SetStaticMesh(MarkerMeshAsset);
		MarkerMesh->SetRelativeLocation(MarkerOffset);
		MarkerMesh->SetRelativeScale3D(MarkerScale);
	}
	if (MarkerLight)
	{
		MarkerLight->SetLightColor(MarkerLightColor);
		MarkerLight->SetIntensity(MarkerLightIntensity);
		MarkerLight->SetRelativeLocation(MarkerOffset);
	}
	SetMarkerVisible(true);

	// One reusable event level: roll the CATEGORY first (GI pity odds — long peaceful streaks make
	// fights/treasure increasingly likely), then pick an event from that category's pool.
	if (RandomRootNodes.Num() > 0)
	{
		TArray<FName>* Pool = &RandomRootNodes;
		if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			const int32 Category = GI->RollEventCategory();
			if (Category == 1 && FightRootNodes.Num() > 0)         Pool = &FightRootNodes;
			else if (Category == 2 && TreasureRootNodes.Num() > 0) Pool = &TreasureRootNodes;
		}
		RootNode = (*Pool)[FMath::RandRange(0, Pool->Num() - 1)];
	}

	// Event rooms auto-open the dialogue after a beat (PC + widget ready) instead of on walk-in.
	if (bAutoStartOnLoad && !RootNode.IsNone())
	{
		GetWorldTimerManager().SetTimer(AutoStartTimer, this, &ADialogueTrigger::AutoStart, 0.4f, false);
	}
}

void ADialogueTrigger::AutoStart()
{
	if (!bOpen && !(bOncePerLoad && bFired))
	{
		StartDialogue(RootNode);
	}
}

void ADialogueTrigger::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!OtherActor) return;
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;
	if (bOpen) return;
	if (bOncePerLoad && bFired) return;

	if (RootNode.IsNone())
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] Trigger has no RootNode set."));
		return;
	}
	StartDialogue(RootNode);
}

void ADialogueTrigger::StartDialogue(FName NodeId)
{
	if (!DialogueTable)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] No DialogueTable assigned."));
		return;
	}
	const FDialogueNode* Node = DialogueTable->FindRow<FDialogueNode>(NodeId, TEXT("DialogueTrigger"), /*bWarn*/false);
	if (!Node)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] Node '%s' not found — closing."), *NodeId.ToString());
		CloseDialogue();
		return;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	// Create the widget once + bind the fixed choice buttons. Loaded at runtime (robust to asset order).
	if (!DialogueWidget && PC)
	{
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_Dialogue.WBP_Dialogue_C"));
		if (Cls)
		{
			DialogueWidget = CreateWidget<UUserWidget>(PC, Cls);
			if (DialogueWidget)
			{
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice0Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice0);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice1Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice1);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice2Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice2);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice3Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice3);
			}
		}
	}
	if (!DialogueWidget)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] WBP_Dialogue missing — cannot open."));
		return;
	}

	// Dialogue lives INSIDE the arm monitor (slow-mo, cursor, dashboard hidden) when the player
	// pawn is ours; plain viewport overlay as fallback.
	ALoopedCharacter* PlayerChar = PC ? Cast<ALoopedCharacter>(PC->GetPawn()) : nullptr;
	if (PlayerChar)
	{
		PlayerChar->MountDialogueInMonitor(DialogueWidget);
	}
	else if (!DialogueWidget->IsInViewport())
	{
		DialogueWidget->AddToViewport(300);
	}

	// First open: free the cursor + stop the player wandering off mid-dialogue. UMG buttons still work
	// with player input disabled (Slate input is separate).
	if (!bOpen && PC)
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		PC->SetInputMode(Mode);
		if (APawn* P = PC->GetPawn()) P->DisableInput(PC);
		SetResultLine(TEXT("")); // fresh event — no stale result line
	}

	bOpen = true;
	bFired = true;
	ShowNode(*Node);
}

// Short "(what happens)" hint appended to a choice's button label, generated straight from the
// outcome data so every event gets it for free. Chained consequences stay hidden — they live on
// the NEXT node's choice (e.g. the Mirror shows "(artifact)" now, the curse surprises you after).
static FString OutcomeHint(const FDialogueChoice& C)
{
	switch (C.Outcome)
	{
	case EDialogueOutcome::AddShards:         return FString::Printf(TEXT("(+%d Shards)"), C.OutcomeAmount);
	case EDialogueOutcome::SpendShards:       return FString::Printf(TEXT("(-%d Shards)"), C.OutcomeAmount);
	case EDialogueOutcome::Heal:              return C.OutcomeAmount >= 500 ? FString(TEXT("(full heal)")) : FString::Printf(TEXT("(+%d HP)"), C.OutcomeAmount);
	case EDialogueOutcome::Damage:            return FString::Printf(TEXT("(-%d HP)"), C.OutcomeAmount);
	case EDialogueOutcome::RandomArtifact:    return TEXT("(blessing)");
	case EDialogueOutcome::GrantArtifact:     return TEXT("(relic)");
	case EDialogueOutcome::AddCurse:          return TEXT("(curse!)");
	case EDialogueOutcome::StartFight:        return TEXT("(FIGHT!)");
	case EDialogueOutcome::UpgradeRandomCard: return TEXT("(upgrade a card)");
	case EDialogueOutcome::CleanseCurse:      return TEXT("(cleanse a curse)");
	case EDialogueOutcome::GambleShards:      return FString::Printf(TEXT("(wager %d Shards)"), C.OutcomeAmount);
	default:                                  return FString();
	}
}

void ADialogueTrigger::ShowNode(const FDialogueNode& Node)
{
	if (!DialogueWidget) return;

	if (UTextBlock* T = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("SpeakerText"))))
	{
		T->SetText(Node.Speaker);
		T->SetVisibility(Node.Speaker.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (UTextBlock* T = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("BodyText"))))
	{
		T->SetText(Node.Body);
	}

	CurrentChoices = Node.Choices;

	// For locking pointless choices (cleanse with no curses, heal at full HP, prices you can't pay).
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	ALoopedCharacter* PlayerChar = nullptr;
	if (APlayerController* LockPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PlayerChar = Cast<ALoopedCharacter>(LockPC->GetPawn());
	}

	// Up to 4 fixed choice buttons; show only as many as the node defines. A node with no choices gets
	// a single auto "Continue" that closes.
	const int32 Count = CurrentChoices.Num();
	for (int32 i = 0; i < 4; ++i)
	{
		const FString BtnName = FString::Printf(TEXT("Choice%dButton"), i);
		const FString TxtName = FString::Printf(TEXT("Choice%dText"), i);
		UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(*BtnName));
		UTextBlock* Txt = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(*TxtName));

		bool bUse = false;
		bool bUsable = true;
		FText Label;
		if (Count == 0 && i == 0) { bUse = true; Label = FText::FromString(TEXT("Continue")); }
		else if (i < Count)
		{
			bUse = true;
			Label = CurrentChoices[i].ChoiceText;
			if (Label.IsEmpty()) Label = FText::FromString(TEXT("Continue"));
			const FString Hint = OutcomeHint(CurrentChoices[i]);
			if (!Hint.IsEmpty())
			{
				Label = FText::FromString(Label.ToString() + TEXT("  ") + Hint);
			}

			// Lock choices that would do nothing (or that the player can't pay for) right now.
			const FDialogueChoice& C = CurrentChoices[i];
			switch (C.Outcome)
			{
			case EDialogueOutcome::CleanseCurse:
				bUsable = GI && GI->GetActiveCurses().Num() > 0;
				break;
			case EDialogueOutcome::Heal:
				bUsable = PlayerChar && PlayerChar->POCCurrentHealth < PlayerChar->POCMaxHealth - 0.5f;
				break;
			case EDialogueOutcome::SpendShards:
			case EDialogueOutcome::GambleShards:
				bUsable = GI && GI->GetShards() >= C.OutcomeAmount;
				break;
			case EDialogueOutcome::Damage:
				// No suicide buttons: paying HP you don't have stays locked.
				bUsable = PlayerChar && PlayerChar->POCCurrentHealth > (float)C.OutcomeAmount;
				break;
			case EDialogueOutcome::UpgradeRandomCard:
				if (GI && PlayerChar)
				{
					bUsable = false;
					for (const FPassiveSlot& Slot : GI->RunDeck)
					{
						if (!PlayerChar->IsPerkAtMax(Slot.CardRowName)) { bUsable = true; break; }
					}
					// Empty deck still allowed — the outcome pays consolation shards instead.
					if (GI->RunDeck.Num() == 0) bUsable = true;
				}
				break;
			default:
				break;
			}

			// A node's ONLY exit can never be locked — otherwise the dialogue becomes a trap
			// (e.g. paid the basin's price, no curse to cleanse, Continue greyed out = stuck).
			// The outcome whiffs gracefully instead ("Nothing to cleanse.").
			if (Count == 1)
			{
				bUsable = true;
			}
		}

		if (B)
		{
			B->SetVisibility(bUse ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			B->SetIsEnabled(bUsable); // greyed out when the choice is pointless/unaffordable
		}
		if (Txt) Txt->SetText(Label);
	}
}

void ADialogueTrigger::OnChoice0() { HandleChoice(0); }
void ADialogueTrigger::OnChoice1() { HandleChoice(1); }
void ADialogueTrigger::OnChoice2() { HandleChoice(2); }
void ADialogueTrigger::OnChoice3() { HandleChoice(3); }

void ADialogueTrigger::HandleChoice(int32 Index)
{
	if (!bOpen) return;

	// No-choice node: the lone "Continue" just closes.
	if (CurrentChoices.Num() == 0)
	{
		CloseDialogue();
		return;
	}
	if (!CurrentChoices.IsValidIndex(Index)) return;

	const FDialogueChoice Choice = CurrentChoices[Index]; // copy — ShowNode overwrites CurrentChoices
	const FString Result = ApplyOutcome(Choice);

	if (Choice.NextNode.IsNone())
	{
		// Closing — the panel is gone, so a screen message can't overlap anything.
		if (!Result.IsEmpty())
		{
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
			{
				if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(PC->GetPawn()))
				{
					Player->ShowCenterMessage(FText::FromString(Result), 3.0f);
				}
			}
		}
		CloseDialogue();
	}
	else
	{
		StartDialogue(Choice.NextNode);
		// After ShowNode so the gold result line survives onto the next screen of text.
		SetResultLine(Result);
	}
}

void ADialogueTrigger::SetResultLine(const FString& Result)
{
	if (!DialogueWidget || Result.IsEmpty()) return;

	// Append the "what happened" line to the BODY text itself (the separate ResultText widget was
	// set correctly but never rendered in-game — body text demonstrably renders, so ride it).
	// Called AFTER StartDialogue, so BodyText already holds the next node's story text.
	if (UTextBlock* Body = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("BodyText"))))
	{
		const FString Combined = Body->GetText().ToString() + TEXT("\n\n— ") + Result + TEXT(" —");
		Body->SetText(FText::FromString(Combined));
		UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] ResultLine appended to body: '%s'"), *Result);
	}
}

FString ADialogueTrigger::ApplyOutcome(const FDialogueChoice& Choice)
{
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	ALoopedCharacter* Player = nullptr;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Player = Cast<ALoopedCharacter>(PC->GetPawn());
	}

	// Build a short "what happened" line — the caller shows it in the dialogue's gold result line
	// (or as a screen message when the dialogue closes).
	FString Result;
	switch (Choice.Outcome)
	{
	case EDialogueOutcome::AddShards:
		if (GI && Choice.OutcomeAmount > 0)
		{
			GI->AddShards(Choice.OutcomeAmount);
			Result = FString::Printf(TEXT("+%d Shards"), Choice.OutcomeAmount);
		}
		// (negative AddShards is a no-op until a SpendShards outcome exists — no message)
		break;
	case EDialogueOutcome::Heal:
		if (Player) Player->HealPlayer((float)Choice.OutcomeAmount);
		Result = FString::Printf(TEXT("Healed %d HP"), Choice.OutcomeAmount);
		break;
	case EDialogueOutcome::Damage:
		if (Player) Player->TakeDamageFromEnemy((float)Choice.OutcomeAmount);
		Result = FString::Printf(TEXT("Took %d damage"), Choice.OutcomeAmount);
		break;
	case EDialogueOutcome::RandomArtifact:
		if (GI)
		{
			const FName A = GI->GrantRandomRunArtifact();
			const FArtifactData* Row = A.IsNone() ? nullptr : GI->FindArtifactRow(A);
			Result = A.IsNone() ? FString(TEXT("Gained a blessing"))
				: (Row && !Row->Description.IsEmpty())
					? FString::Printf(TEXT("Blessing: %s — %s"), *Row->DisplayName.ToString(), *Row->Description.ToString())
					: FString::Printf(TEXT("Blessing: %s"), *A.ToString());
		}
		break;
	case EDialogueOutcome::GrantArtifact:
		if (GI && !Choice.OutcomeId.IsNone())
		{
			GI->GrantArtifact(Choice.OutcomeId);
			const FArtifactData* Row = GI->FindArtifactRow(Choice.OutcomeId);
			Result = (Row && !Row->Description.IsEmpty())
				? FString::Printf(TEXT("Relic: %s — %s"), *Row->DisplayName.ToString(), *Row->Description.ToString())
				: FString::Printf(TEXT("Relic: %s"), *Choice.OutcomeId.ToString());
		}
		break;
	case EDialogueOutcome::AddCurse:
		if (GI && !Choice.OutcomeId.IsNone())
		{
			GI->AddCurse(Choice.OutcomeId);
			const FString Desc = GI->GetCurseDescription(Choice.OutcomeId).ToString();
			Result = Desc.IsEmpty()
				? FString::Printf(TEXT("Cursed: %s"), *Choice.OutcomeId.ToString())
				: FString::Printf(TEXT("Cursed: %s — %s"), *Choice.OutcomeId.ToString(), *Desc);
		}
		break;
	case EDialogueOutcome::SpendShards:
		if (GI && Choice.OutcomeAmount > 0)
		{
			// If they can't afford it the choice whiffs but the dialogue still advances (POC).
			Result = GI->SpendShards(Choice.OutcomeAmount)
				? FString::Printf(TEXT("-%d Shards"), Choice.OutcomeAmount)
				: FString(TEXT("Not enough Shards..."));
		}
		break;
	case EDialogueOutcome::StartFight:
		// Don't spawn mid-dialogue — stage it; CloseDialogue fires the fight and HOLDS the exit
		// portals until every spawned enemy is dead.
		bFightPending = true;
		PendingFightRow = Choice.OutcomeId.IsNone() ? FName(TEXT("Random")) : Choice.OutcomeId;
		PendingFightCount = FMath::Max(1, Choice.OutcomeAmount);
		break;
	case EDialogueOutcome::UpgradeRandomCard:
		if (GI && Player)
		{
			// Pool = run-deck cards that aren't maxed. Empty deck / all maxed -> consolation shards.
			TArray<FName> Pool;
			for (const FPassiveSlot& Slot : GI->RunDeck)
			{
				if (!Player->IsPerkAtMax(Slot.CardRowName))
				{
					Pool.Add(Slot.CardRowName);
				}
			}
			if (Pool.Num() > 0)
			{
				const FName Card = Pool[FMath::RandRange(0, Pool.Num() - 1)];
				const int32 NewLevel = Player->IncrementPerkLevel(Card);
				Result = FString::Printf(TEXT("Upgraded: %s (Lv %d)"), *Card.ToString(), NewLevel);
			}
			else
			{
				GI->AddShards(30);
				Result = TEXT("Nothing to sharpen. +30 Shards");
			}
		}
		break;
	case EDialogueOutcome::CleanseCurse:
		if (GI)
		{
			const TArray<FName> Curses = GI->GetActiveCurses();
			if (Curses.Num() > 0)
			{
				const FName Curse = Curses[FMath::RandRange(0, Curses.Num() - 1)];
				GI->RemoveCurse(Curse);
				Result = FString::Printf(TEXT("Cleansed: %s"), *Curse.ToString());
			}
			else
			{
				Result = TEXT("Nothing to cleanse.");
			}
		}
		break;
	case EDialogueOutcome::GambleShards:
		if (GI && Choice.OutcomeAmount > 0)
		{
			if (!GI->SpendShards(Choice.OutcomeAmount))
			{
				Result = TEXT("Not enough Shards...");
				break;
			}
			const float Roll = FMath::FRand();
			if (Roll < 0.10f)
			{
				GI->AddShards(Choice.OutcomeAmount * 3);
				Result = FString::Printf(TEXT("JACKPOT! +%d Shards"), Choice.OutcomeAmount * 2);
			}
			else if (Roll < 0.55f)
			{
				GI->AddShards(Choice.OutcomeAmount * 2);
				Result = FString::Printf(TEXT("You won! +%d Shards"), Choice.OutcomeAmount);
			}
			else
			{
				Result = FString::Printf(TEXT("Lost. -%d Shards"), Choice.OutcomeAmount);
			}
		}
		break;
	case EDialogueOutcome::None:
	default:
		break;
	}

	return Result;
}

void ADialogueTrigger::CloseDialogue()
{
	bOpen = false;

	// The talk is over — the "?" marker disappears.
	SetMarkerVisible(false);
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		// Monitor-mounted path: restore the dashboard + close the monitor (time/cursor restored there).
		if (ALoopedCharacter* PlayerChar = Cast<ALoopedCharacter>(PC->GetPawn()))
		{
			PlayerChar->UnmountDialogueFromMonitor(DialogueWidget);
		}
		PC->bShowMouseCursor = false;
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		if (APawn* P = PC->GetPawn()) P->EnableInput(PC);
	}
	if (DialogueWidget && DialogueWidget->IsInViewport())
	{
		DialogueWidget->RemoveFromParent(); // viewport-fallback path
	}

	// A staged fight means the event ISN'T over — spawn it after a breath and keep the exits locked
	// until the player wins. Otherwise: finishing the talk = clearing the room.
	if (bFightPending)
	{
		bFightPending = false;
		GetWorldTimerManager().SetTimer(FightSpawnTimer, this, &ADialogueTrigger::SpawnFight, 0.7f, false);
		return;
	}
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ActivateRoomExitPortals();
	}
}

void ADialogueTrigger::SpawnFight()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Pick the body BP by the archetype's ranged flag (melee/ranged BPs carry the right collision
	// cube + materials); the DT_Enemies row then overrides all stats via ApplyEnemyType. "Random"
	// resolves per enemy inside ApplyEnemyType, so a "pack" fight is a different mix every time.
	bool bRowIsRanged = false;
	if (UDataTable* Enemies = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies")))
	{
		if (const FEnemyTypeData* Row = Enemies->FindRow<FEnemyTypeData>(PendingFightRow, TEXT("DialogueFight"), false))
		{
			bRowIsRanged = Row->bIsRanged;
		}
	}
	UClass* MeleeBP  = LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/BP_TestEnemy.BP_TestEnemy_C"));
	UClass* RangedBP = LoadClass<AEnemyBase>(nullptr, TEXT("/Game/Blueprints/BP_RangedEnemy.BP_RangedEnemy_C"));
	UClass* SpawnClass = (bRowIsRanged && RangedBP) ? RangedBP : (MeleeBP ? MeleeBP : RangedBP);
	if (!SpawnClass)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] Fight: no enemy BP class found — opening exits."));
		OnFightWon();
		return;
	}

	FightEnemiesAlive = 0;
	const FVector Center = GetActorLocation();
	for (int32 i = 0; i < PendingFightCount; ++i)
	{
		// Ring around the marker, away from typical player position. Flat arenas — fixed Z lift.
		const float Angle = (2.0f * PI * i) / FMath::Max(1, PendingFightCount) + FMath::FRandRange(-0.3f, 0.3f);
		const FVector Loc = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 650.0f + FVector(0, 0, 120.0f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(SpawnClass, Loc, FRotator::ZeroRotator, Params);
		if (Enemy)
		{
			Enemy->ApplyEnemyType(PendingFightRow);
			Enemy->OnEnemyDied.AddDynamic(this, &ADialogueTrigger::OnFightEnemyDied);
			FightEnemiesAlive++;
		}
	}

	if (FightEnemiesAlive == 0)
	{
		OnFightWon(); // spawn totally failed — never strand the player in a locked room
		return;
	}

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(PC->GetPawn()))
		{
			Player->ShowCenterMessage(FText::FromString(TEXT("FIGHT!")), 2.0f);
		}
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Fight started: %d x %s"), FightEnemiesAlive, *PendingFightRow.ToString());
}

void ADialogueTrigger::OnFightEnemyDied(AEnemyBase* /*Enemy*/)
{
	FightEnemiesAlive--;
	if (FightEnemiesAlive <= 0)
	{
		OnFightWon();
	}
}

void ADialogueTrigger::OnFightWon()
{
	// Pay the wager and open the exits.
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	ALoopedCharacter* Player = nullptr;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Player = Cast<ALoopedCharacter>(PC->GetPawn());
	}

	FString Msg = TEXT("You won.");
	if (GI)
	{
		const FName A = GI->GrantRandomRunArtifact();
		GI->AddShards(40);
		const FArtifactData* Row = A.IsNone() ? nullptr : GI->FindArtifactRow(A);
		Msg = A.IsNone() ? FString(TEXT("You won. +40 Shards"))
			: (Row && !Row->Description.IsEmpty())
				? FString::Printf(TEXT("You won — Blessing: %s (%s)  +40 Shards"), *Row->DisplayName.ToString(), *Row->Description.ToString())
				: FString::Printf(TEXT("You won — Blessing: %s  +40 Shards"), *A.ToString());
		GI->ActivateRoomExitPortals();
	}
	if (Player)
	{
		Player->ShowCenterMessage(FText::FromString(Msg), 4.0f);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Fight won — exits open."));
}
