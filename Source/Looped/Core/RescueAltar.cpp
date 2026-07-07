#include "RescueAltar.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Core/LoopedGameInstance.h"
#include "Data/LoopedSaveData.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

ARescueAltar::ARescueAltar()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(200.0f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ARescueAltar::OnOverlap);

	// Placeholder pedestal (low cylinder). Real altar art comes later, like Vorr's model did.
	AltarMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AltarMesh"));
	AltarMesh->SetupAttachment(RootComponent);
	AltarMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 0.6f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylMesh.Succeeded())
	{
		AltarMesh->SetStaticMesh(CylMesh.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 140.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetAttenuationRadius(650.0f);
	GlowLight->SetCastShadows(false);

	NameTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTagComp"));
	NameTagComp->SetupAttachment(RootComponent);
	NameTagComp->SetRelativeLocation(FVector(0.0f, 0.0f, 260.0f));
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
	LaunchMessage = FText::FromString(TEXT("The loop bends. Somewhere below, a cell door waits..."));
	ColdMessage = FText::FromString(TEXT("The altar is cold. Prove you persist — the loop is not convinced yet."));
}

void ARescueAltar::BeginPlay()
{
	Super::BeginPlay();

	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());

	// RESCUED — the companion stands at the Hub now; the altar has served its purpose.
	if (GI && GI->HasArtifact(CompanionArtifact))
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}

	// All gates must pass: vault (first boss kill) + the chain's previous companion + boss-kill
	// count. Any miss = the altar stays dark and only whispers.
	bLit = !bRequireVaultUnlock || (GI && GI->IsPermanentVaultUnlocked());
	if (bLit && !RequiredArtifact.IsNone())
	{
		bLit = GI && GI->HasArtifact(RequiredArtifact);
	}
	if (bLit && RequiredBossKills > 0)
	{
		bLit = GI && GI->Stats && GI->Stats->BossKills >= RequiredBossKills;
	}

	if (bLit)
	{
		GlowLight->SetLightColor(LitColor);
		GlowLight->SetIntensity(1400.0f);

		// "RESCUE: LYSA" floating tag, tinted to the companion color (portal-label pattern).
		NameTagComp->InitWidget();
		if (UUserWidget* W = NameTagComp->GetUserWidgetObject())
		{
			if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
			{
				T->SetText(FText::FromString(FString::Printf(TEXT("RESCUE: %s"), *CompanionDisplayName.ToString().ToUpper())));
				T->SetColorAndOpacity(FSlateColor(LitColor));
			}
		}
		NameTagComp->SetVisibility(true);
	}
	else
	{
		// DARK — barely-there ember; the player learns it exists but not yet what it wants.
		GlowLight->SetLightColor(FLinearColor(0.35f, 0.35f, 0.4f, 1.0f));
		GlowLight->SetIntensity(150.0f);
	}
}

void ARescueAltar::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	// Walk-in keeps ONLY the dark-altar whisper — launching is a deliberate press-E (Interact),
	// so brushing past a lit altar can never yank you out of the Hub.
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor);
	if (!Player || bLit) return;

	const double Now = FPlatformTime::Seconds();
	if (Now - LastColdMessageTime > 6.0)
	{
		LastColdMessageTime = Now;
		Player->ShowCenterMessage(ColdMessage, 3.5f);
	}
}

void ARescueAltar::Interact(ALoopedCharacter* Player)
{
	if (!bLit)
	{
		// E on a dark altar: same whisper, same rate limit.
		const double Now = FPlatformTime::Seconds();
		if (Now - LastColdMessageTime > 6.0)
		{
			LastColdMessageTime = Now;
			Player->ShowCenterMessage(ColdMessage, 3.5f);
		}
		return;
	}

	if (bTraveling) return;
	bTraveling = true;

	Player->ShowCenterMessage(LaunchMessage, TravelDelay + 1.5f);
	UE_LOG(LogLoopedCore, Display, TEXT("[Rescue] Altar answered [E] — launching mission '%s'."), *MissionLevel.ToString());
	GetWorldTimerManager().SetTimer(TravelTimerHandle, this, &ARescueAltar::LaunchMission, TravelDelay, false);
}

void ARescueAltar::LaunchMission()
{
	UGameplayStatics::OpenLevel(this, MissionLevel);
}
