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

	// One reusable event level: pick a random event for this visit.
	if (RandomRootNodes.Num() > 0)
	{
		RootNode = RandomRootNodes[FMath::RandRange(0, RandomRootNodes.Num() - 1)];
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

	if (!DialogueWidget->IsInViewport())
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
	}

	bOpen = true;
	bFired = true;
	ShowNode(*Node);
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
		FText Label;
		if (Count == 0 && i == 0) { bUse = true; Label = FText::FromString(TEXT("Continue")); }
		else if (i < Count)       { bUse = true; Label = CurrentChoices[i].ChoiceText; if (Label.IsEmpty()) Label = FText::FromString(TEXT("Continue")); }

		if (B)   B->SetVisibility(bUse ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
	ApplyOutcome(Choice);

	if (Choice.NextNode.IsNone())
	{
		CloseDialogue();
	}
	else
	{
		StartDialogue(Choice.NextNode);
	}
}

void ADialogueTrigger::ApplyOutcome(const FDialogueChoice& Choice)
{
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	ALoopedCharacter* Player = nullptr;
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		Player = Cast<ALoopedCharacter>(PC->GetPawn());
	}

	// Build a short "what happened" line shown center-screen after the choice (some results aren't
	// described in the body text).
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
			Result = A.IsNone() ? FString(TEXT("Gained an artifact")) : FString::Printf(TEXT("Gained: %s"), *A.ToString());
		}
		break;
	case EDialogueOutcome::GrantArtifact:
		if (GI && !Choice.OutcomeId.IsNone())
		{
			GI->GrantArtifact(Choice.OutcomeId);
			Result = FString::Printf(TEXT("Gained: %s"), *Choice.OutcomeId.ToString());
		}
		break;
	case EDialogueOutcome::AddCurse:
		if (GI && !Choice.OutcomeId.IsNone())
		{
			GI->AddCurse(Choice.OutcomeId);
			Result = FString::Printf(TEXT("Cursed: %s"), *Choice.OutcomeId.ToString());
		}
		break;
	case EDialogueOutcome::None:
	default:
		break;
	}

	if (!Result.IsEmpty() && Player)
	{
		Player->ShowCenterMessage(FText::FromString(Result), 3.0f);
	}
}

void ADialogueTrigger::CloseDialogue()
{
	bOpen = false;

	// The talk is over — the "?" marker disappears.
	SetMarkerVisible(false);
	if (DialogueWidget && DialogueWidget->IsInViewport())
	{
		DialogueWidget->RemoveFromParent();
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		if (APawn* P = PC->GetPawn()) P->EnableInput(PC);
	}

	// Finishing the event = clearing the room: open the exit fork so the run can continue.
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ActivateRoomExitPortals();
	}
}
