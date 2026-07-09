#include "DialogueTrigger.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Enemies/EnemyBase.h"
#include "Enemies/BossBase.h"
#include "Core/TreasureChest.h"
#include "EngineUtils.h"
#include "Data/EnemyData.h"
#include "Data/ArtifactData.h"
#include "Data/TreasureTypes.h"
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

	// Floating "[E]" prompt — the interact invitation (text filled in BeginPlay). Screen-space,
	// hidden until the marker shows.
	PromptTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("PromptTagComp"));
	PromptTagComp->SetupAttachment(SceneRoot);
	PromptTagComp->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	PromptTagComp->SetWidgetSpace(EWidgetSpace::Screen);
	PromptTagComp->SetDrawSize(FVector2D(200.0f, 60.0f));
	PromptTagComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PromptTagComp->SetVisibility(false);
	static ConstructorHelpers::FClassFinder<UUserWidget> PromptClass(TEXT("/Game/UI/WBP_NameTag"));
	if (PromptClass.Succeeded())
	{
		PromptTagComp->SetWidgetClass(PromptClass.Class);
	}
}

void ADialogueTrigger::SetMarkerVisible(bool bVisible)
{
	if (MarkerMesh)  MarkerMesh->SetVisibility(bVisible);
	if (MarkerLight) MarkerLight->SetVisibility(bVisible);
	// Static "[E]" tag retired — the proximity prompt ("Press [E] to investigate") covers it.
	if (PromptTagComp) PromptTagComp->SetVisibility(false);
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
	// Fill the "[E]" prompt (rides the marker offset so it hugs whatever the marker is).
	if (PromptTagComp)
	{
		PromptTagComp->SetRelativeLocation(MarkerOffset + FVector(0.0f, 0.0f, 120.0f));
		PromptTagComp->InitWidget();
		if (UUserWidget* W = PromptTagComp->GetUserWidgetObject())
		{
			if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
			{
				T->SetText(FText::FromString(TEXT("[E]")));
				T->SetColorAndOpacity(FSlateColor(MarkerLightColor));
			}
		}
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
	// Walk-in no longer starts the talk — that's the deliberate press-E in Interact (the "[E]"
	// prompt under the marker says so). Kept bound for possible future proximity hints.
}

void ADialogueTrigger::Interact(ALoopedCharacter* Player)
{
	// Same guards the old walk-in start used.
	if (!bInteractionEnabled) return;
	if (bOpen) return;
	if (bOncePerLoad && bFired) return;

	if (RootNode.IsNone())
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] Trigger has no RootNode set."));
		return;
	}
	StartDialogue(RootNode);
}

void ADialogueTrigger::SetInteractionEnabled(bool bEnabled)
{
	bInteractionEnabled = bEnabled;
	// Hide the marker while locked so Orin doesn't look talkable mid-combat.
	if (!bOpen)
	{
		SetMarkerVisible(bEnabled);
	}
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
	// Standalone triggers (casino) use the fullscreen table layout; monitor dialogues keep WBP_Dialogue.
	if (!DialogueWidget && PC)
	{
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, bStandaloneWidget
			? TEXT("/Game/UI/WBP_CasinoTable.WBP_CasinoTable_C")
			: TEXT("/Game/UI/WBP_Dialogue.WBP_Dialogue_C"));
		if (Cls)
		{
			DialogueWidget = CreateWidget<UUserWidget>(PC, Cls);
			if (DialogueWidget)
			{
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice0Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice0);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice1Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice1);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice2Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice2);
				if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(TEXT("Choice3Button")))) B->OnClicked.AddDynamic(this, &ADialogueTrigger::OnChoice3);
				ApplyDialogueTheme(); // each trigger owns its widget instance — paint once at creation
			}
		}
	}
	if (!DialogueWidget)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Dialogue] WBP_Dialogue missing — cannot open."));
		return;
	}

	// Dialogue lives INSIDE the arm monitor (slow-mo, cursor, dashboard hidden) when the player
	// pawn is ours — EXCEPT standalone triggers (casino tables), which own the screen directly.
	ALoopedCharacter* PlayerChar = PC ? Cast<ALoopedCharacter>(PC->GetPawn()) : nullptr;
	if (PlayerChar && !bStandaloneWidget)
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
	case EDialogueOutcome::SlotSpin:
	case EDialogueOutcome::RouletteBet:
	case EDialogueOutcome::BlackjackDeal:
	case EDialogueOutcome::CoinFlip:          return FString::Printf(TEXT("(wager %d)"), C.OutcomeAmount);
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
			case EDialogueOutcome::SlotSpin:
			case EDialogueOutcome::RouletteBet:
			case EDialogueOutcome::BlackjackDeal:
			case EDialogueOutcome::CoinFlip:
				bUsable = GI && GI->GetShards() >= C.OutcomeAmount;
				break;
			case EDialogueOutcome::BlackjackHit:
			case EDialogueOutcome::BlackjackStand:
				bUsable = BJWager > 0; // no hand, no buttons (Leave stays open)
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

			// Once-per-room choices (the bar's drinks): spent = locked until the room reloads.
			if (C.bOncePerRoom && UsedOnceChoices.Contains(C.ChoiceText.ToString()))
			{
				bUsable = false;
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

	// The card table renders only on blackjack nodes that actually have cards on the felt.
	bool bBJNode = false;
	for (const FDialogueChoice& C : CurrentChoices)
	{
		if (C.Outcome == EDialogueOutcome::BlackjackDeal
			|| C.Outcome == EDialogueOutcome::BlackjackHit
			|| C.Outcome == EDialogueOutcome::BlackjackStand)
		{
			bBJNode = true;
			break;
		}
	}
	UpdateBJCardArea(bBJNode && BJPlayerLabels.Num() > 0);
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

void ADialogueTrigger::PushBJCard(bool bDealer, int32 Value)
{
	// Display flavor: an 11 is the Ace; a 10 wears a random court face. Truth stays in the totals.
	FString Label;
	if (Value >= 11)      Label = TEXT("A");
	else if (Value == 10) { static const TCHAR* Faces[] = { TEXT("10"), TEXT("J"), TEXT("Q"), TEXT("K") }; Label = Faces[FMath::RandRange(0, 3)]; }
	else                  Label = FString::FromInt(Value);
	const bool bRed = FMath::RandBool();
	(bDealer ? BJDealerLabels : BJPlayerLabels).Add(Label);
	(bDealer ? BJDealerRed : BJPlayerRed).Add(bRed);
}

void ADialogueTrigger::UpdateBJCardArea(bool bShow)
{
	if (!DialogueWidget) return;

	if (UWidget* Area = DialogueWidget->GetWidgetFromName(TEXT("CardArea")))
	{
		Area->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bShow) return;

	if (UTextBlock* PL = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("PlayerLbl"))))
	{
		PL->SetText(FText::FromString(BJPlayerLabels.Num() > 0
			? FString::Printf(TEXT("YOU — %d"), BJPlayerTotal) : FString(TEXT("YOU"))));
	}

	const FLinearColor FaceUp(0.96f, 0.96f, 0.92f);
	const FLinearColor FaceDown(0.22f, 0.25f, 0.32f);
	struct { const TCHAR* Tag; const TArray<FString>* Labels; const TArray<bool>* Red; } Rows[] = {
		{ TEXT("D"), &BJDealerLabels, &BJDealerRed },
		{ TEXT("P"), &BJPlayerLabels, &BJPlayerRed },
	};
	for (const auto& Row : Rows)
	{
		for (int32 i = 0; i < 6; ++i)
		{
			UBorder* Card = Cast<UBorder>(DialogueWidget->GetWidgetFromName(*FString::Printf(TEXT("Card%s%d"), Row.Tag, i)));
			UTextBlock* Txt = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(*FString::Printf(TEXT("Card%s%dText"), Row.Tag, i)));
			const bool bHas = Row.Labels->IsValidIndex(i);
			if (Card) Card->SetVisibility(bHas ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (!bHas || !Card || !Txt) continue;
			const FString& L = (*Row.Labels)[i];
			const bool bFaceDown = (L == TEXT("?"));
			Card->SetBrushColor(bFaceDown ? FaceDown : FaceUp);
			Txt->SetText(FText::FromString(L));
			Txt->SetColorAndOpacity(FSlateColor(bFaceDown ? FLinearColor(0.6f, 0.65f, 0.75f)
				: (*Row.Red)[i] ? FLinearColor(0.75f, 0.08f, 0.08f)
				                : FLinearColor(0.05f, 0.05f, 0.05f)));
		}
	}
}

void ADialogueTrigger::ApplyDialogueTheme()
{
	// Untinted triggers keep the widget's designed look (each trigger owns its own instance,
	// so painting one machine never bleeds into normal event dialogues).
	if (!DialogueWidget || !bThemeDialogue) return;

	if (UImage* BG = Cast<UImage>(DialogueWidget->GetWidgetFromName(TEXT("PanelBG"))))
	{
		BG->SetColorAndOpacity(ThemePanelColor);
	}
	if (UTextBlock* T = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("SpeakerText"))))
	{
		T->SetColorAndOpacity(FSlateColor(ThemeSpeakerColor));
	}
	if (UTextBlock* T = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("BodyText"))))
	{
		T->SetColorAndOpacity(FSlateColor(ThemeBodyColor));
	}
	if (UTextBlock* T = Cast<UTextBlock>(DialogueWidget->GetWidgetFromName(TEXT("ResultText"))))
	{
		T->SetColorAndOpacity(FSlateColor(ThemeResultColor));
	}
	for (int32 i = 0; i < 4; ++i)
	{
		if (UButton* B = Cast<UButton>(DialogueWidget->GetWidgetFromName(*FString::Printf(TEXT("Choice%dButton"), i))))
		{
			B->SetBackgroundColor(ThemeButtonColor);
		}
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
	// Spend once-per-room choices the moment they fire (ShowNode locks them from here on).
	if (Choice.bOncePerRoom)
	{
		UsedOnceChoices.Add(Choice.ChoiceText.ToString());
	}

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
			if (!GI->TryCasinoWager(Choice.OutcomeAmount))
			{
				Result = TEXT("Not enough Shards...");
				break;
			}
			const float Scale = GI->GetCasinoPayoutScale();
			const float Roll = FMath::FRand();
			// House edge: ~40% win / 8% jackpot / 52% lose (was 45/10/45).
			// Diminishing returns can push a computed payout below the stake — floor every WIN at a
			// real profit. The house edge lives in the LOSE odds; a "win" must never cost Shards.
			if (Roll < 0.08f)
			{
				const int32 Pay = FMath::Max(FMath::CeilToInt(Choice.OutcomeAmount * 1.5f),
					FMath::FloorToInt(Choice.OutcomeAmount * 2.5f * Scale));
				GI->AddShards(Pay);
				Result = FString::Printf(TEXT("JACKPOT! +%d Shards"), Pay - Choice.OutcomeAmount);
			}
			else if (Roll < 0.48f)
			{
				const int32 Pay = FMath::Max(Choice.OutcomeAmount + 1,
					FMath::FloorToInt(Choice.OutcomeAmount * 1.8f * Scale));
				GI->AddShards(Pay);
				Result = FString::Printf(TEXT("You won! +%d Shards"), Pay - Choice.OutcomeAmount);
			}
			else
			{
				Result = FString::Printf(TEXT("Lost. -%d Shards"), Choice.OutcomeAmount);
			}
		}
		break;

	case EDialogueOutcome::SlotSpin:
		if (GI && Choice.OutcomeAmount > 0)
		{
			if (!GI->TryCasinoWager(Choice.OutcomeAmount)) { Result = TEXT("Not enough Shards..."); break; }
			const float Scale = GI->GetCasinoPayoutScale();
			// Three weighted reels — VOID is rarer (5%). Pair pays less; triples still juicy.
			static const TCHAR* Faces[] = { TEXT("SKULL"), TEXT("BELL"), TEXT("EYE"), TEXT("VOID") };
			auto RollReel = []() -> int32
			{
				const float R = FMath::FRand();
				return (R < 0.36f) ? 0 : (R < 0.72f) ? 1 : (R < 0.95f) ? 2 : 3;
			};
			const int32 R1 = RollReel(), R2 = RollReel(), R3 = RollReel();
			const FString Reels = FString::Printf(TEXT("[ %s | %s | %s ]"), Faces[R1], Faces[R2], Faces[R3]);
			int32 Pay = 0;
			FString Verdict = TEXT("Nothing.");
			if (R1 == R2 && R2 == R3)
			{
				const float Mult = (R1 == 3) ? 8.0f : 4.0f;
				// Triples always profit (VOID >= 3x, plain >= 2x) even at full diminishing scale.
				Pay = FMath::Max(Choice.OutcomeAmount * ((R1 == 3) ? 3 : 2),
					FMath::FloorToInt(Choice.OutcomeAmount * Mult * Scale));
				Verdict = (R1 == 3) ? TEXT("VOID JACKPOT!") : TEXT("TRIPLE!");
			}
			else if (R1 == R2 || R2 == R3 || R1 == R3)
			{
				// A pair never LOSES Shards — worst case it refunds the stake (+0).
				Pay = FMath::Max(Choice.OutcomeAmount,
					FMath::FloorToInt(Choice.OutcomeAmount * 1.15f * Scale));
				Verdict = TEXT("A pair.");
			}
			if (Pay > 0) GI->AddShards(Pay);
			Result = FString::Printf(TEXT("%s  %s  %+d Shards"), *Reels, *Verdict, Pay - Choice.OutcomeAmount);
		}
		break;

	case EDialogueOutcome::RouletteBet:
		if (GI && Choice.OutcomeAmount > 0)
		{
			if (!GI->TryCasinoWager(Choice.OutcomeAmount)) { Result = TEXT("Not enough Shards..."); break; }
			const float Scale = GI->GetCasinoPayoutScale();
			const int32 Pocket = FMath::RandRange(0, 36); // 0 = the green void
			const bool bRed = (Pocket != 0) && (Pocket % 2 == 1);
			const FString PocketStr = (Pocket == 0) ? TEXT("GREEN 0")
				: FString::Printf(TEXT("%s %d"), bRed ? TEXT("RED") : TEXT("BLACK"), Pocket);

			bool bWin = false; int32 Pay = 0;
			// House edge: Red/Black pay 1.85x (not even money); Green still 9x. Wins floor at a
			// real profit — diminishing returns must never turn a hit pocket into a loss.
			if (Choice.OutcomeId == TEXT("Green"))    { bWin = (Pocket == 0);           Pay = FMath::Max(Choice.OutcomeAmount * 4, FMath::FloorToInt(Choice.OutcomeAmount * 9.0f * Scale)); }
			else if (Choice.OutcomeId == TEXT("Red")) { bWin = bRed;                    Pay = FMath::Max(Choice.OutcomeAmount + 1, FMath::FloorToInt(Choice.OutcomeAmount * 1.85f * Scale)); }
			else                                      { bWin = !bRed && Pocket != 0;    Pay = FMath::Max(Choice.OutcomeAmount + 1, FMath::FloorToInt(Choice.OutcomeAmount * 1.85f * Scale)); }
			if (bWin)
			{
				GI->AddShards(Pay);
				Result = FString::Printf(TEXT("The ball settles on %s — you called it! +%d Shards"), *PocketStr, Pay - Choice.OutcomeAmount);
			}
			else
			{
				Result = FString::Printf(TEXT("The ball settles on %s. -%d Shards"), *PocketStr, Choice.OutcomeAmount);
			}
		}
		break;

	case EDialogueOutcome::BlackjackDeal:
		if (GI && Choice.OutcomeAmount > 0)
		{
			if (!GI->TryCasinoWager(Choice.OutcomeAmount)) { Result = TEXT("Not enough Shards..."); break; }
			BJWager = Choice.OutcomeAmount;
			const int32 C1 = FMath::RandRange(2, 11);
			const int32 C2 = FMath::RandRange(2, 11);
			BJPlayerTotal = (C1 + C2 == 22) ? 12 : C1 + C2; // double aces soften
			BJDealerUp = FMath::RandRange(2, 11);
			// Fresh table: your two cards face-up, dealer's upcard + one facedown.
			BJPlayerLabels.Reset(); BJPlayerRed.Reset();
			BJDealerLabels.Reset(); BJDealerRed.Reset();
			PushBJCard(false, C1);
			PushBJCard(false, C2);
			PushBJCard(true, BJDealerUp);
			BJDealerLabels.Add(TEXT("?")); BJDealerRed.Add(false);
			Result = FString::Printf(TEXT("You draw %d + %d = %d. Dealer shows %d."), C1, C2, BJPlayerTotal, BJDealerUp);
			if (BJPlayerTotal == 21)
			{
				const float Scale = GI->GetCasinoPayoutScale();
				const int32 Pay = FMath::Max(FMath::CeilToInt(BJWager * 1.5f), FMath::FloorToInt(BJWager * 2.2f * Scale));
				GI->AddShards(Pay);
				Result += FString::Printf(TEXT("  NATURAL 21! +%d Shards"), Pay - BJWager);
				BJWager = 0; // hand over
			}
		}
		break;

	case EDialogueOutcome::BlackjackHit:
		if (GI)
		{
			if (BJWager <= 0) { Result = TEXT("No hand in play."); break; }
			const int32 C = FMath::RandRange(2, 11);
			BJPlayerTotal += C;
			PushBJCard(false, C);
			if (BJPlayerTotal > 21)
			{
				Result = FString::Printf(TEXT("You draw %d — %d. BUST. -%d Shards"), C, BJPlayerTotal, BJWager);
				BJWager = 0;
			}
			else
			{
				Result = FString::Printf(TEXT("You draw %d — %d total. Dealer shows %d."), C, BJPlayerTotal, BJDealerUp);
			}
		}
		break;

	case EDialogueOutcome::BlackjackStand:
		if (GI)
		{
			if (BJWager <= 0) { Result = TEXT("No hand in play."); break; }
			int32 Dealer = BJDealerUp;
			FString Draws = FString::Printf(TEXT("Dealer: %d"), Dealer);
			// The facedown card flips into real draws.
			if (BJDealerLabels.Num() > 0 && BJDealerLabels.Last() == TEXT("?"))
			{
				BJDealerLabels.Pop(); BJDealerRed.Pop();
			}
			while (Dealer < 17)
			{
				const int32 C = FMath::RandRange(2, 11);
				Dealer += C;
				PushBJCard(true, C);
				Draws += FString::Printf(TEXT(" +%d"), C);
			}
			if (Dealer > 21 || BJPlayerTotal > Dealer)
			{
				const float Scale = GI->GetCasinoPayoutScale();
				const int32 Pay = FMath::Max(BJWager + 1, FMath::FloorToInt(BJWager * 1.9f * Scale));
				GI->AddShards(Pay);
				Result = FString::Printf(TEXT("%s = %d. Your %d takes it — WIN! +%d Shards"), *Draws, Dealer, BJPlayerTotal, Pay - BJWager);
			}
			else if (Dealer == BJPlayerTotal)
			{
				GI->AddShards(BJWager);
				Result = FString::Printf(TEXT("%s = %d. Push — wager returned."), *Draws, Dealer);
			}
			else
			{
				Result = FString::Printf(TEXT("%s = %d against your %d. House wins. -%d Shards"), *Draws, Dealer, BJPlayerTotal, BJWager);
			}
			BJWager = 0;
		}
		break;

	case EDialogueOutcome::CoinFlip:
		if (GI && Choice.OutcomeAmount > 0)
		{
			if (!GI->TryCasinoWager(Choice.OutcomeAmount)) { Result = TEXT("Not enough Shards..."); break; }
			const float Scale = GI->GetCasinoPayoutScale();
			// Weighted coin — house edge (~42% win). Pays 2.1x so risk/reward isn't flat 50/50.
			const bool bSkulls = FMath::FRand() < 0.42f;
			const bool bCalledSkulls = (Choice.OutcomeId != TEXT("Spirals")); // default call = Skulls
			const FString Landed = bSkulls ? TEXT("SKULLS") : TEXT("SPIRALS");
			if (bSkulls == bCalledSkulls)
			{
				const int32 Pay = FMath::Max(Choice.OutcomeAmount + 1, FMath::FloorToInt(Choice.OutcomeAmount * 2.1f * Scale));
				GI->AddShards(Pay);
				Result = FString::Printf(TEXT("The coin lands %s — you called it! +%d Shards"), *Landed, Pay - Choice.OutcomeAmount);
			}
			else
			{
				Result = FString::Printf(TEXT("The coin lands %s. The barkeep pockets it. -%d Shards"), *Landed, Choice.OutcomeAmount);
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
		// Monitor-mounted path: restore the dashboard + close the monitor (time/cursor restored
		// there). Standalone (casino) widgets never mounted — nothing to restore.
		if (ALoopedCharacter* PlayerChar = Cast<ALoopedCharacter>(PC->GetPawn()))
		{
			if (!bStandaloneWidget) PlayerChar->UnmountDialogueFromMonitor(DialogueWidget);
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
	OpenExitsOrGateOnRoomEnemies();
}

void ADialogueTrigger::OpenExitsOrGateOnRoomEnemies()
{
	// Count enemies already living in the level (placed by the designer, not by a dialogue fight).
	// Bosses run their own exit flow (BossRoomExit) — never gate on them here.
	int32 Alive = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* E = *It;
		if (!E || E->IsA(ABossBase::StaticClass())) continue;
		if (!E->IsAlive()) continue;
		E->OnEnemyDied.AddUniqueDynamic(this, &ADialogueTrigger::OnRoomEnemyDied);
		++Alive;
	}

	if (Alive > 0)
	{
		// Combat event: hold the exits. OnRoomEnemyDied opens them when the room is clear.
		RoomEnemiesAlive = Alive;
		UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Talk ended with %d enemies alive — exits locked until cleared."), Alive);
		return;
	}

	// No living enemies (peaceful event / shop / casino) — open exits as before.
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ActivateRoomExitPortals();
	}
}

void ADialogueTrigger::OnRoomEnemyDied(AEnemyBase* /*Enemy*/)
{
	if (--RoomEnemiesAlive > 0) return;
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ActivateRoomExitPortals();
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Room cleared after talk — exits open."));
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
	// Strong fights only (EventChampion / Warden / etc. with bGrantsBossReward): "?" pedestal.
	// Normal melee packs clear the room and open exits — no blessing/cards mark.
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	ALoopedCharacter* Player = nullptr;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Player = Cast<ALoopedCharacter>(PC->GetPawn());
	}

	bool bGrantPedestal = false;
	if (UDataTable* Enemies = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Enemies.DT_Enemies")))
	{
		if (const FEnemyTypeData* Row = Enemies->FindRow<FEnemyTypeData>(PendingFightRow, TEXT("FightReward"), false))
		{
			// Explicit flag, OR placed/boss-only archetype (SpawnWeight 0 = EventChampion/Warden/FloorBoss/TheLooped).
			// "Random" / Grunt / Hound packs have weight > 0 → no pedestal.
			bGrantPedestal = Row->bGrantsBossReward || Row->SpawnWeight <= 0.0f;
		}
	}

	if (!bGrantPedestal)
	{
		if (GI)
		{
			GI->ActivateRoomExitPortals();
		}
		if (Player)
		{
			Player->ShowCenterMessage(FText::FromString(TEXT("Path clear.")), 2.5f);
		}
		UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Fight won (%s) — no boss reward; exits open."),
			*PendingFightRow.ToString());
		return;
	}

	if (GI)
	{
		GI->ResetTreasurePicks();
		const FVector SpawnLoc = GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ATreasureChest* Pedestal = GetWorld()->SpawnActor<ATreasureChest>(
			ATreasureChest::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params))
		{
			// 50/50 blessing vs card bundle — treasure-style choice via press-E.
			Pedestal->RewardType = (FMath::FRand() < 0.5f)
				? ETreasureRewardType::CleanRelic
				: ETreasureRewardType::CardBundle;
			Pedestal->CardBundleCount = 2;
			Pedestal->ApplyQuestionMarkRewardVisual();
			Pedestal->RerollOffer(); // BeginPlay already rolled with the default type
		}
		if (Player)
		{
			Player->ShowCenterMessage(FText::FromString(TEXT("A mark appears — press [E] to claim your reward.")), 4.0f);
		}
		// Exits stay closed until the pedestal is claimed (TreasureChest::AcceptPedestal).
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Dialogue] Fight won (%s) — reward pedestal spawned."),
		*PendingFightRow.ToString());
}
