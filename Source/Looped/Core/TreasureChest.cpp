#include "TreasureChest.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Data/ArtifactData.h"
#include "Looped.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ATreasureChest::ATreasureChest()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	PedestalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PedestalMesh"));
	PedestalMesh->SetupAttachment(RootComponent);
	PedestalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PedestalMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.0f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		PedestalMesh->SetStaticMesh(CubeMesh.Object);
	}

	FloatingWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("FloatingWidgetComp"));
	FloatingWidgetComp->SetupAttachment(RootComponent);
	FloatingWidgetComp->SetRelativeLocation(FVector(0.0f, 0.0f, 160.0f));
	FloatingWidgetComp->SetWidgetSpace(EWidgetSpace::Screen); // always faces the camera — no mirroring
	FloatingWidgetComp->SetDrawSize(FVector2D(420.0f, 240.0f));

	// Auto-link the sign widget so no manual Blueprint assignment is needed.
	static ConstructorHelpers::FClassFinder<UUserWidget> SignWidgetClass(TEXT("/Game/UI/WBP_TreasureSign"));
	if (SignWidgetClass.Succeeded())
	{
		FloatingWidgetComp->SetWidgetClass(SignWidgetClass.Class);
	}

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(RootComponent);
	TriggerBox->SetBoxExtent(FVector(110.0f, 110.0f, 150.0f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ATreasureChest::OnOverlapBegin);

	// Shared UI button click when the reward is taken (original placeholder path).
	static ConstructorHelpers::FObjectFinder<USoundBase> ButtonSnd(TEXT("/Game/Audio/button.button"));
	if (ButtonSnd.Succeeded()) PickupSound = ButtonSnd.Object;
}

void ATreasureChest::BeginPlay()
{
	Super::BeginPlay();
	RollOffer();
}

void ATreasureChest::ApplyQuestionMarkRewardVisual()
{
	bDespawnWhenTaken = true;
	// CHEST, not "?" (Sahar 2026-07-10): the reward must never blend into the event's own
	// question-mark marker it spawns next to — a chest reads as "claim me" instantly.
	if (UStaticMesh* Chest = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/new_assets/chest/StaticMeshes/chest.chest")))
	{
		if (PedestalMesh)
		{
			PedestalMesh->SetStaticMesh(Chest);
			PedestalMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 0.8f));
			// Meshy exports: centered pivot -> lift by half height; front lines up at yaw -90.
			PedestalMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
			PedestalMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
			PedestalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	if (FloatingWidgetComp)
	{
		FloatingWidgetComp->SetRelativeLocation(FVector(0.0f, 0.0f, 190.0f));
	}
}

void ATreasureChest::RollOffer()
{
	ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr;
	CurseWarningText = FText::GetEmpty();

	switch (RewardType)
	{
	case ETreasureRewardType::CleanRelic:
		RolledArtifactId = GI ? GI->RollRunArtifact(false) : NAME_None;
		if (const FArtifactData* R = GI ? GI->FindArtifactRow(RolledArtifactId) : nullptr)
		{
			OfferName        = R->DisplayName;
			OfferDescription = R->Description;
		}
		else { OfferName = FText::FromString(TEXT("Blessing")); OfferDescription = FText::FromString(TEXT("No blessing available.")); }
		break;

	case ETreasureRewardType::CursedRelic:
		RolledArtifactId = GI ? GI->RollRunArtifact(true) : NAME_None;
		if (const FArtifactData* R = GI ? GI->FindArtifactRow(RolledArtifactId) : nullptr)
		{
			OfferName        = R->DisplayName;
			OfferDescription = R->Description;
			// Spell out what the curse DOES, not just its name (Sahar: everything explains itself).
			const FString CurseDesc = GI ? GI->GetCurseDescription(R->AssociatedCurseId).ToString() : FString();
			CurseWarningText = FText::FromString(CurseDesc.IsEmpty()
				? FString::Printf(TEXT("CURSED BARGAIN - injects the %s curse"), *R->AssociatedCurseId.ToString())
				: FString::Printf(TEXT("CURSED BARGAIN - %s curse: %s"), *R->AssociatedCurseId.ToString(), *CurseDesc));
		}
		else { OfferName = FText::FromString(TEXT("Cursed Blessing")); OfferDescription = FText::FromString(TEXT("No blessing available.")); }
		break;

	case ETreasureRewardType::CardBundle:
		OfferName        = FText::FromString(TEXT("Card Bundle"));
		OfferDescription = FText::FromString(FString::Printf(TEXT("Adds %d random cards to your run deck."), CardBundleCount));
		break;

	default:
		OfferName        = FText::FromString(TEXT("Coming soon"));
		OfferDescription = FText::GetEmpty();
		break;
	}

	PushOfferTextToWidget();
}

void ATreasureChest::PushOfferTextToWidget()
{
	if (!FloatingWidgetComp) return;
	// Ensure the widget instance exists (Screen-space widgets create lazily).
	if (!FloatingWidgetComp->GetUserWidgetObject())
	{
		FloatingWidgetComp->InitWidget();
	}
	UUserWidget* W = FloatingWidgetComp->GetUserWidgetObject();
	if (!W) return;

	auto SetBlock = [W](const TCHAR* BlockName, const FText& Value)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(BlockName)))
		{
			T->SetText(Value);
			T->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	};

	// Inspection-card layout: three separate blocks.
	SetBlock(TEXT("NameText"),  OfferName);
	SetBlock(TEXT("DescText"),  OfferDescription);
	SetBlock(TEXT("CurseText"), CurseWarningText); // auto-hides when empty (clean offers)

	// The "step inside to choose" prompt only makes sense while the offer is still pickable —
	// hide it once the pedestal is taken or locked.
	if (UTextBlock* Footer = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("FooterText"))))
	{
		Footer->SetVisibility((bTaken || bLocked) ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	// Fallback for the single-block layout (until the widget is split): combine into OfferText.
	if (UTextBlock* Legacy = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("OfferText"))))
	{
		FString Combined = OfferName.ToString();
		if (!OfferDescription.IsEmpty()) Combined += TEXT("\n") + OfferDescription.ToString();
		if (!CurseWarningText.IsEmpty()) Combined += TEXT("\n") + CurseWarningText.ToString();
		Legacy->SetText(FText::FromString(Combined));
	}
}

void ATreasureChest::OnOverlapBegin(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	// Walk-in no longer CLAIMS (that's the deliberate press-E in Interact) — it only refreshes
	// the locked state so a pedestal approached after the budget is spent relabels immediately.
	if (bTaken || bLocked || !OtherActor) return;
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;

	ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr;
	if (!GI) return;

	if (!GI->CanPickTreasure()) { LockPedestal(); }
}

void ATreasureChest::Interact(ALoopedCharacter* Player)
{
	if (bTaken || bLocked) return;
	ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr;
	if (!GI) return;

	// Budget already spent on another pedestal — lock instead of granting, and SAY so
	// (a silent E-press reads as a bug; see the stuck-champion-reward playtest).
	if (!GI->CanPickTreasure())
	{
		LockPedestal();
		if (Player) Player->ShowCenterMessage(FText::FromString(TEXT("Your pick here is already spent.")), 2.5f);
		return;
	}

	const FText ClaimName = OfferName; // captured before AcceptPedestal relabels it
	AcceptPedestal();
	if (Player)
	{
		Player->ShowCenterMessage(bTaken
			? FText::FromString(FString::Printf(TEXT("Claimed: %s"), *ClaimName.ToString()))
			: FText::FromString(TEXT("Nothing to claim here.")), 3.0f);
	}
}

void ATreasureChest::AcceptPedestal()
{
	if (bTaken || bLocked) return;
	ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr;
	if (!GI || !GI->CanPickTreasure()) return;

	FString What;
	switch (RewardType)
	{
	case ETreasureRewardType::CleanRelic:
	case ETreasureRewardType::CursedRelic:
		if (RolledArtifactId.IsNone())
		{
			// Boss/champion chest is the ONLY pedestal in its room — "pick elsewhere" would be a
			// softlock. Blessing pool empty? Pay cards instead; the claim must always succeed.
			if (bDespawnWhenTaken)
			{
				GI->GrantRunCardBundle(2);
				What = TEXT("2 cards (blessing pool empty)");
				break;
			}
			return; // treasure rooms have sibling pedestals; let the player pick elsewhere
		}
		GI->GrantRunArtifact(RolledArtifactId);     // handles cursed-bargain injection
		What = RolledArtifactId.ToString();
		break;
	case ETreasureRewardType::CardBundle:
		GI->GrantRunCardBundle(CardBundleCount);
		What = FString::Printf(TEXT("%d cards"), CardBundleCount);
		break;
	default:
		UE_LOG(LogLoopedRun, Warning, TEXT("[Treasure] RewardType not implemented yet."));
		return;
	}

	bTaken = true;
	GI->RegisterTreasurePick();
	if (PickupSound)
	{
		UGameplayStatics::PlaySound2D(this, PickupSound);
	}
	OnPedestalTaken(RolledArtifactId);

#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			FString::Printf(TEXT("Treasure taken: %s"), *What), true, FVector2D(1.5f, 1.5f));
	}
#endif

	// N of X mutual exclusion: if the room's budget is now spent, lock every other pedestal.
	if (!GI->CanPickTreasure())
	{
		for (TActorIterator<ATreasureChest> It(GetWorld()); It; ++It)
		{
			if (*It && *It != this) (*It)->LockPedestal();
		}
		// Safety: treasure/? rooms should already have exits from GameMode entry, but if the
		// portal BeginPlay race wiped them (or the player only looks after the pick), re-open now.
		GI->ActivateRoomExitPortals();
	}
	if (TriggerBox)
	{
		TriggerBox->SetGenerateOverlapEvents(false);
	}

	// Boss/elite "?" mark: vanish mesh + floating label after the claim (Sahar).
	if (bDespawnWhenTaken)
	{
		if (PedestalMesh) PedestalMesh->SetVisibility(false, true);
		if (FloatingWidgetComp)
		{
			FloatingWidgetComp->SetVisibility(false, true);
			FloatingWidgetComp->SetHiddenInGame(true);
		}
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		return;
	}

	OfferName        = FText::FromString(TEXT("TAKEN"));
	OfferDescription = FText::FromString(TEXT("Acquired"));
	CurseWarningText = FText::GetEmpty();
	PushOfferTextToWidget();
}

void ATreasureChest::LockPedestal()
{
	if (bLocked || bTaken) return;
	bLocked = true;
	if (TriggerBox)
	{
		TriggerBox->SetGenerateOverlapEvents(false);
	}
	OfferName        = FText::FromString(TEXT("LOCKED"));
	OfferDescription = FText::FromString(TEXT("Choice Spent"));
	CurseWarningText = FText::GetEmpty();
	PushOfferTextToWidget();
	OnPedestalLocked();
}
