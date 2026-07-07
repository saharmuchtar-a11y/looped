#include "CompanionCage.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Core/LoopedGameInstance.h"
#include "Core/LoopedRunGameMode.h"
#include "Core/PortalActor.h"
#include "Enemies/EnemyBase.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

ACompanionCage::ACompanionCage()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(320.0f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACompanionCage::OnOverlap);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Glowing placeholder figure (a person-sized cylinder tinted in BeginPlay).
	CompanionMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CompanionMesh"));
	CompanionMesh->SetupAttachment(RootComponent);
	CompanionMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	CompanionMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.8f));
	CompanionMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylMesh.Succeeded())
	{
		CompanionMesh->SetStaticMesh(CylMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (GlowMat.Succeeded())
	{
		CompanionMesh->SetMaterial(0, GlowMat.Object);
	}

	// The bar ring: 8 thin cylinders around the figure. Solid (blocks walking through) until opened.
	const float BarRadius = 150.0f;
	for (int32 i = 0; i < 8; ++i)
	{
		UStaticMeshComponent* Bar = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("CageBar_%d"), i));
		Bar->SetupAttachment(RootComponent);
		const float Angle = (2.0f * PI / 8.0f) * i;
		Bar->SetRelativeLocation(FVector(BarRadius * FMath::Cos(Angle), BarRadius * FMath::Sin(Angle), 150.0f));
		Bar->SetRelativeScale3D(FVector(0.09f, 0.09f, 3.0f));
		if (CylMesh.Succeeded())
		{
			Bar->SetStaticMesh(CylMesh.Object);
		}
		CageBars.Add(Bar);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(900.0f);
	GlowLight->SetAttenuationRadius(700.0f);
	GlowLight->SetCastShadows(false);

	NameTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTagComp"));
	NameTagComp->SetupAttachment(RootComponent);
	NameTagComp->SetRelativeLocation(FVector(0.0f, 0.0f, 360.0f));
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
	FreedMessage = FText::FromString(TEXT("The cage shatters. Lysa is FREE — the loop will remember this."));
	CaptiveMessage = FText::FromString(TEXT("\"Behind you! Deal with the Warden — then get me out of here!\""));
}

void ACompanionCage::BeginPlay()
{
	Super::BeginPlay();

	// Already rescued (a past session): they live at the Hub now — their NPC stands where the
	// cage was, so the cage vanishes entirely. A "— freed" tag over nothing just floats
	// (Sahar hit exactly that next to Serin's hub NPC).
	if (const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance()))
	{
		if (GI->HasArtifact(CompanionArtifact))
		{
			bOpened = true;
			SetActorHiddenInGame(true);
			SetActorEnableCollision(false);
			// Screen-space widget components don't obey HiddenInGame — hide the tag directly.
			NameTagComp->SetVisibility(false);
			return;
		}
	}

	// Tint the placeholder figure + glow to the companion color.
	CompanionMID = CompanionMesh->CreateDynamicMaterialInstance(0);
	if (CompanionMID)
	{
		CompanionMID->SetVectorParameterValue(TEXT("OverlayColor"), CompanionColor);
	}
	GlowLight->SetLightColor(CompanionColor);
	SetNameTag(FString::Printf(TEXT("%s — captive"), *CompanionDisplayName.ToString().ToUpper()), CompanionColor);

	// Puzzle cages (Brann's forge) open ONLY via an external OpenCage() call — no death
	// listening, no failsafe (the puzzle itself is the gate; nothing here can strand you).
	if (!bOpenWhenEnemiesDead) return;

	// The GameMode broadcasts OnRoomCleared once per enemy DEATH (historic misnomer) — each
	// death, recount the living; when none remain the Warden has fallen and the cage opens.
	if (ALoopedRunGameMode* GM = GetWorld()->GetAuthGameMode<ALoopedRunGameMode>())
	{
		GM->OnRoomCleared.AddDynamic(this, &ACompanionCage::HandleEnemyDied);
	}

	// Failsafe: a mis-set-up level with zero enemies must still open — never strand the player.
	GetWorldTimerManager().SetTimer(FailsafeTimerHandle, this, &ACompanionCage::FailsafeCheck, 3.0f, false);
}

void ACompanionCage::HandleEnemyDied()
{
	if (bOpened) return;

	int32 Alive = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		if (*It && (*It)->IsAlive()) ++Alive;
	}
	if (Alive == 0)
	{
		OpenCage();
	}
}

void ACompanionCage::FailsafeCheck()
{
	if (bOpened) return;
	int32 Alive = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		if (*It && (*It)->IsAlive()) ++Alive;
	}
	if (Alive == 0)
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Rescue] Cage failsafe: no enemies in level — opening anyway."));
		OpenCage();
	}
}

void ACompanionCage::OpenCage()
{
	if (bOpened) return;
	bOpened = true;

	// Bars drop: hidden + no collision (the figure stays — the companion steps "free").
	for (UStaticMeshComponent* Bar : CageBars)
	{
		if (!Bar) continue;
		Bar->SetVisibility(false);
		Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Permanent companion relic — GrantArtifact saves + fires the "RELIC ACQUIRED" toast.
	if (ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance()))
	{
		GI->GrantArtifact(CompanionArtifact);
	}

	if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0)))
	{
		Player->ShowCenterMessage(FreedMessage, 4.0f);
	}
	SetNameTag(FString::Printf(TEXT("%s — FREED"), *CompanionDisplayName.ToString().ToUpper()),
		FLinearColor(0.3f, 1.0f, 0.4f, 1.0f));

	UE_LOG(LogLoopedRun, Display, TEXT("[Rescue] %s freed — relic '%s' granted."),
		*CompanionDisplayName.ToString(), *CompanionArtifact.ToString());

	// Beat first, exit after — same cause-precedes-portal sequencing as the sphere win.
	// (Tutorial cage opts out: the director reveals the exit only after the monitor teach.)
	if (bRevealPortalsOnOpen)
	{
		GetWorldTimerManager().SetTimer(PortalTimerHandle, this, &ACompanionCage::RevealExitPortals, PortalRevealDelay, false);
	}
}

void ACompanionCage::RevealExitPortals()
{
	for (TActorIterator<APortalActor> It(GetWorld()); It; ++It)
	{
		It->ActivatePortal();
	}
}

void ACompanionCage::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (bOpened) return;
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor);
	if (!Player) return;

	const double Now = FPlatformTime::Seconds();
	if (Now - LastCaptiveMessageTime > 8.0)
	{
		LastCaptiveMessageTime = Now;
		Player->ShowCenterMessage(CaptiveMessage, 3.5f);
	}
}

void ACompanionCage::SetNameTag(const FString& Text, const FLinearColor& Color)
{
	NameTagComp->InitWidget();
	if (UUserWidget* W = NameTagComp->GetUserWidgetObject())
	{
		if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
		{
			T->SetText(FText::FromString(Text));
			T->SetColorAndOpacity(FSlateColor(Color));
		}
	}
	NameTagComp->SetVisibility(true);
}
