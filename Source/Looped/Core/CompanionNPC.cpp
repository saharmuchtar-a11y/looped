#include "CompanionNPC.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

ACompanionNPC::ACompanionNPC()
{
	PrimaryActorTick.bCanEverTick = true; // face-player tracking (disabled when hidden)

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(260.0f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACompanionNPC::OnOverlap);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ACompanionNPC::OnTriggerEnd); // walk-out closes the trainer menu

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.8f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CylMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (GlowMat.Succeeded())
	{
		BodyMesh->SetMaterial(0, GlowMat.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(500.0f);
	GlowLight->SetAttenuationRadius(500.0f);
	GlowLight->SetCastShadows(false);

	NameTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTagComp"));
	NameTagComp->SetupAttachment(RootComponent);
	NameTagComp->SetRelativeLocation(FVector(0.0f, 0.0f, 300.0f));
	NameTagComp->SetWidgetSpace(EWidgetSpace::Screen);
	NameTagComp->SetDrawSize(FVector2D(340.0f, 80.0f));
	NameTagComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameTagComp->SetVisibility(false);
	static ConstructorHelpers::FClassFinder<UUserWidget> TagClass(TEXT("/Game/UI/WBP_NameTag"));
	if (TagClass.Succeeded())
	{
		NameTagComp->SetWidgetClass(TagClass.Class);
	}

	CompanionDisplayName = FText::FromString(TEXT("Lysa"));
	GreetingLines = {
		FText::FromString(TEXT("\"Still breathing, Hunter? Good. I patch faster than the loop breaks.\"")),
		FText::FromString(TEXT("\"Eighty years down there... the camp fire feels impossible.\"")),
		FText::FromString(TEXT("\"Go. I'll be here when you come back — that's the point of me.\""))
	};
}

void ACompanionNPC::BeginPlay()
{
	Super::BeginPlay();

	// Bob baseline: whatever rest height the instance authored (Meshy models ride at rel z ~95).
	if (BodyMesh)
	{
		BobRestZ = BodyMesh->GetRelativeLocation().Z;
	}

	// Visibility gate: not rescued yet → the actor simply isn't here.
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI || !GI->HasArtifact(CompanionArtifact))
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}

	BodyMID = BodyMesh->CreateDynamicMaterialInstance(0);
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("OverlayColor"), CompanionColor);
	}
	GlowLight->SetLightColor(CompanionColor);

	NameTagComp->InitWidget();
	if (UUserWidget* W = NameTagComp->GetUserWidgetObject())
	{
		if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
		{
			// The proximity prompt ("Press [E] to talk...") advertises the interaction now.
			T->SetText(CompanionDisplayName);
			T->SetColorAndOpacity(FSlateColor(CompanionColor));
		}
	}
	NameTagComp->SetVisibility(true);
}

void ACompanionNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Hover-bob (Mira): sine the MESH's relative Z around its authored rest height. Mesh-only,
	// like the face-player snap — the trigger, name tag and interact range stay put.
	if (bBobUpDown && BodyMesh)
	{
		FVector Rel = BodyMesh->GetRelativeLocation();
		const double T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Rel.Z = BobRestZ + FMath::Sin(T * BobSpeed) * BobAmplitude;
		BodyMesh->SetRelativeLocation(Rel);
	}

	// Face-player: snap the MESH (not the actor) to the target yaw each tick — Vorr's exact
	// approach. No interpolation: FInterpTo on raw yaw never converges across the ±180 wrap and
	// sent the first Lysa spinning. Mesh-only rotation also keeps the trigger/name tag still.
	if (!bFacePlayer || !BodyMesh) return;
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn) return;

	FVector ToPlayer = Pawn->GetActorLocation() - BodyMesh->GetComponentLocation();
	ToPlayer.Z = 0.0f; // yaw only — keep her upright
	if (ToPlayer.IsNearlyZero()) return;
	const float Yaw = ToPlayer.Rotation().Yaw + FaceYawOffset;
	BodyMesh->SetWorldRotation(FRotator(0.0f, Yaw, 0.0f));
}

void ACompanionNPC::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor);
	if (!Player || GreetingLines.Num() == 0) return;

	const double Now = FPlatformTime::Seconds();
	if (Now - LastGreetingTime < 8.0) return;
	LastGreetingTime = Now;

	Player->ShowCenterMessage(GreetingLines[NextGreetingIndex % GreetingLines.Num()], 3.5f);
	NextGreetingIndex++;
}

int32 ACompanionNPC::NextTrainingCost() const
{
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	return TrainingCostBase + TrainingCostStep * (GI ? GI->GetSkillGaugeRanks() : 0);
}

FText ACompanionNPC::GetInteractPrompt() const
{
	if (!TravelOnInteractLevel.IsNone())
	{
		return TravelPromptVerb.IsEmpty()
			? FText::FromString(FString::Printf(TEXT("travel with %s"), *CompanionDisplayName.ToString()))
			: TravelPromptVerb;
	}
	if (bOffersChronoTraining)
	{
		return bTrainerOpen ? FText::GetEmpty()
			: FText::FromString(FString::Printf(TEXT("train with %s"), *CompanionDisplayName.ToString()));
	}
	return FText::FromString(FString::Printf(TEXT("talk to %s"), *CompanionDisplayName.ToString()));
}

void ACompanionNPC::OpenTrainingMenu(ALoopedCharacter* Player)
{
	if (bTrainerOpen || !Player) return;
	APlayerController* PC = Cast<APlayerController>(Player->GetController());
	if (!PC) return;

	if (!TrainerWidget)
	{
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_Trainer.WBP_Trainer_C"));
		if (!Cls) return;
		TrainerWidget = CreateWidget<UUserWidget>(PC, Cls);
		if (!TrainerWidget) return;
		if (UButton* B = Cast<UButton>(TrainerWidget->GetWidgetFromName(TEXT("Train0Buy"))))
			B->OnClicked.AddDynamic(this, &ACompanionNPC::OnTrain0);
		if (UButton* B = Cast<UButton>(TrainerWidget->GetWidgetFromName(TEXT("CloseButton"))))
			B->OnClicked.AddDynamic(this, &ACompanionNPC::OnTrainerClose);
	}

	TrainerPC = PC;
	TrainerWidget->AddToViewport(150);
	bTrainerOpen = true;

	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);

	RefreshTrainingMenu();
}

void ACompanionNPC::CloseTrainingMenu()
{
	if (!bTrainerOpen) return;
	bTrainerOpen = false;
	if (TrainerWidget) TrainerWidget->RemoveFromParent();
	if (TrainerPC)
	{
		TrainerPC->bShowMouseCursor = false;
		TrainerPC->SetInputMode(FInputModeGameOnly());
	}
}

void ACompanionNPC::RefreshTrainingMenu()
{
	if (!TrainerWidget) return;
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());

	if (UTextBlock* Bal = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("EchoesBalance"))))
	{
		Bal->SetText(FText::FromString(FString::Printf(TEXT("Echoes: %d"), GI ? GI->GetEchoes() : 0)));
	}

	// Row 0 — the chrono gauge (today's only discipline).
	const int32 Ranks = GI ? GI->GetSkillGaugeRanks() : 0;
	const int32 MaxRanks = GI ? GI->MaxSkillGaugeRanks : 3;
	const bool bMaxed = Ranks >= MaxRanks;
	const int32 Cost = NextTrainingCost();
	if (UTextBlock* N = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("Train0Name"))))
	{
		N->SetText(FText::FromString(bMaxed
			? FString::Printf(TEXT("Chrono-gauge — MASTERED (rank %d/%d)"), Ranks, MaxRanks)
			: FString::Printf(TEXT("Deepen chrono-gauge +1s  (rank %d/%d)"), Ranks, MaxRanks)));
	}
	if (UTextBlock* C = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("Train0Cost"))))
	{
		C->SetText(bMaxed ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("%d"), Cost)));
	}
	if (UTextBlock* BT = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("Train0BuyText"))))
	{
		BT->SetText(FText::FromString(bMaxed ? TEXT("MAX") : TEXT("TRAIN")));
	}
	if (UButton* B = Cast<UButton>(TrainerWidget->GetWidgetFromName(TEXT("Train0Buy"))))
	{
		B->SetIsEnabled(!bMaxed && GI && GI->GetEchoes() >= Cost);
	}

	// Row 1 — a teaser for the skills to come; rows 2-3 stay dark until they exist.
	if (UTextBlock* N = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("Train1Name"))))
	{
		N->SetText(FText::FromString(TEXT("??? — \"When you've earned a second discipline, I'll teach it.\"")));
		N->SetColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.45f, 0.55f)));
	}
	if (UTextBlock* C = Cast<UTextBlock>(TrainerWidget->GetWidgetFromName(TEXT("Train1Cost"))))
	{
		C->SetText(FText::GetEmpty());
	}
	if (UWidget* B = TrainerWidget->GetWidgetFromName(TEXT("Train1Buy")))
	{
		B->SetVisibility(ESlateVisibility::Collapsed);
	}
	for (int32 i = 2; i < 4; ++i)
	{
		if (UWidget* Row = TrainerWidget->GetWidgetFromName(*FString::Printf(TEXT("TrainRow%d"), i)))
		{
			Row->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void ACompanionNPC::TryTrain(int32 /*SlotIndex*/)
{
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI || GI->GetSkillGaugeRanks() >= GI->MaxSkillGaugeRanks) { RefreshTrainingMenu(); return; }

	const int32 Cost = NextTrainingCost();
	if (GI->SpendEchoes(Cost))
	{
		const int32 Rank = GI->GrantSkillGaugeRank();
		if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
		{
			Player->ShowCenterMessage(FText::FromString(FString::Printf(
				TEXT("\"Hold longer. Breathe slower.\" Chrono-gauge deepened — rank %d/%d."),
				Rank, GI->MaxSkillGaugeRanks)), 4.0f);
		}
	}
	RefreshTrainingMenu();
}

void ACompanionNPC::OnTrain0() { TryTrain(0); }
void ACompanionNPC::OnTrainerClose() { CloseTrainingMenu(); }

void ACompanionNPC::OnTriggerEnd(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;
	CloseTrainingMenu();
}

void ACompanionNPC::Interact(ALoopedCharacter* Player)
{
	// Travel role (Orin): E replays his mission — the tutorial stays a practice space.
	if (Player && !TravelOnInteractLevel.IsNone())
	{
		UE_LOG(LogLoopedRun, Display, TEXT("[Companion] %s travel -> %s"),
			*CompanionDisplayName.ToString(), *TravelOnInteractLevel.ToString());
		UGameplayStatics::OpenLevel(this, TravelOnInteractLevel);
		return;
	}
	// Trainer role (Lysa): E opens the training menu — a page, not a one-shot interaction.
	if (Player && bOffersChronoTraining)
	{
		OpenTrainingMenu(Player);
		LastGreetingTime = FPlatformTime::Seconds(); // no greeting stacked on the menu
		return;
	}
	// Press-E TALK: their deeper stories, told in order (LoreLines), looping. Falls back to the
	// greeting pool when no lore is authored yet.
	const TArray<FText>& Pool = (LoreLines.Num() > 0) ? LoreLines : GreetingLines;
	if (!Player || Pool.Num() == 0) return;

	Player->ShowCenterMessage(Pool[NextLoreIndex % Pool.Num()], 4.5f);
	NextLoreIndex++;
	// Talking resets the walk-up cooldown so the greeting doesn't stack right on top of lore.
	LastGreetingTime = FPlatformTime::Seconds();
}
