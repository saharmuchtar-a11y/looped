#include "ForgePuzzle.h"
#include "CompanionCage.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

AForgePuzzle::AForgePuzzle()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	LeverTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("LeverTrigger"));
	LeverTrigger->SetupAttachment(RootComponent);
	LeverTrigger->SetSphereRadius(300.0f);
	LeverTrigger->SetCollisionProfileName(TEXT("Trigger"));
	LeverTrigger->OnComponentBeginOverlap.AddDynamic(this, &AForgePuzzle::OnLeverOverlap);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Placeholder forge body (big dark block) — swap for real forge art later.
	ForgeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ForgeMesh"));
	ForgeMesh->SetupAttachment(RootComponent);
	ForgeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));
	ForgeMesh->SetRelativeScale3D(FVector(3.0f, 2.0f, 2.2f));
	if (CubeMesh.Succeeded())
	{
		ForgeMesh->SetStaticMesh(CubeMesh.Object);
	}

	// The master lever — a tilted rod beside the forge body.
	LeverMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeverMesh"));
	LeverMesh->SetupAttachment(RootComponent);
	LeverMesh->SetRelativeLocation(FVector(190.0f, 0.0f, 80.0f));
	LeverMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 25.0f));
	LeverMesh->SetRelativeScale3D(FVector(0.12f, 0.12f, 1.2f));
	LeverMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (CylMesh.Succeeded())
	{
		LeverMesh->SetStaticMesh(CylMesh.Object);
	}

	// One indicator lamp per seated socket, spread along the forge front.
	for (int32 i = 0; i < 3; ++i)
	{
		UPointLightComponent* Lamp = CreateDefaultSubobject<UPointLightComponent>(
			*FString::Printf(TEXT("KeyLamp_%d"), i));
		Lamp->SetupAttachment(RootComponent);
		Lamp->SetRelativeLocation(FVector(-120.0f + 120.0f * i, -130.0f, 230.0f));
		Lamp->SetIntensityUnits(ELightUnits::Lumens);
		Lamp->SetIntensity(0.0f); // dark = not yet seated
		Lamp->SetAttenuationRadius(300.0f);
		Lamp->SetCastShadows(false);
		KeyLamps.Add(Lamp);
	}

	// The heart glow: a barely-alive ember until ignition.
	ForgeGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("ForgeGlow"));
	ForgeGlow->SetupAttachment(RootComponent);
	ForgeGlow->SetRelativeLocation(FVector(0.0f, 0.0f, 200.0f));
	ForgeGlow->SetIntensityUnits(ELightUnits::Lumens);
	ForgeGlow->SetIntensity(120.0f);
	ForgeGlow->SetAttenuationRadius(900.0f);
	ForgeGlow->SetCastShadows(false);

	// "PULL LEVER [E]" tag hovering over the lever arm.
	NameTagComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameTagComp"));
	NameTagComp->SetupAttachment(RootComponent);
	NameTagComp->SetRelativeLocation(FVector(190.0f, 0.0f, 260.0f));
	NameTagComp->SetWidgetSpace(EWidgetSpace::Screen);
	NameTagComp->SetDrawSize(FVector2D(340.0f, 80.0f));
	NameTagComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameTagComp->SetVisibility(false);
	static ConstructorHelpers::FClassFinder<UUserWidget> TagClass(TEXT("/Game/UI/WBP_NameTag"));
	if (TagClass.Succeeded())
	{
		NameTagComp->SetWidgetClass(TagClass.Class);
	}

	IgniteMessage = FText::FromString(TEXT("The forge ROARS back to life — heat floods the pipes and the cell door glows, sags, and melts open."));
	ScaldMessage = FText::FromString(TEXT("Steam bursts from the dry line and scalds you. \"Seat the coils first! She bites when she's cold.\""));
}

void AForgePuzzle::BeginPlay()
{
	Super::BeginPlay();
	ForgeGlow->SetLightColor(EmberColor);
	SetNameTag(TEXT("MASTER LEVER"), EmberColor);
}

void AForgePuzzle::AddCarriedKey(ALoopedCharacter* Player)
{
	// Silent count — the key's own pickup line is the ONE message shown (two stacked center
	// messages overlapped and read huge; Sahar's playtest call).
	CarriedKeys++;
	UE_LOG(LogLoopedRun, Display, TEXT("[Forge] Key picked up (carrying %d)."), CarriedKeys);
}

bool AForgePuzzle::TakeCarriedKey()
{
	if (CarriedKeys <= 0) return false;
	CarriedKeys--;
	return true;
}

void AForgePuzzle::NotifySocketFilled(ALoopedCharacter* Player)
{
	// Light the next lamp on the forge front, left to right.
	if (KeyLamps.IsValidIndex(FilledSockets) && KeyLamps[FilledSockets])
	{
		KeyLamps[FilledSockets]->SetLightColor(EmberColor);
		KeyLamps[FilledSockets]->SetIntensity(800.0f);
	}
	FilledSockets++;

	if (Player)
	{
		// ONE short line per action (playtest: long/stacked messages overlapped).
		const bool bAll = FilledSockets >= TotalKeys;
		Player->ShowCenterMessage(FText::FromString(bAll
			? FString(TEXT("All coils seated — pull the lever!"))
			: FString::Printf(TEXT("Coil seated (%d / %d)"), FilledSockets, TotalKeys)), 2.5f);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Forge] Sockets seated: %d/%d"), FilledSockets, TotalKeys);
}

void AForgePuzzle::OnLeverOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	// Walk-up HINT only — the lever itself is the E interaction.
	if (bIgnited) return;
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor);
	if (!Player) return;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastHintTime < 8.0) return;
	LastHintTime = Now;
	Player->ShowCenterMessage(FText::FromString(TEXT("The master lever. Press E to pull it.")), 2.5f);
}

void AForgePuzzle::Interact(ALoopedCharacter* Player)
{
	if (bIgnited) return;

	// E-mash guard so a hasty player doesn't chain-scald themselves.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastLeverTime < 1.5) return;
	LastLeverTime = Now;

	if (FilledSockets >= TotalKeys)
	{
		Ignite();
		Player->ShowCenterMessage(IgniteMessage, 4.5f);
	}
	else
	{
		// ONE merged line: the sting + the hint together (no stacked messages).
		Player->TakeDamageFromEnemy(FailScaldDamage);
		Player->ShowCenterMessage(FText::FromString(FString::Printf(
			TEXT("Steam scalds you! (%d / %d coils seated)"), FilledSockets, TotalKeys)), 3.0f);
	}
}

void AForgePuzzle::Ignite()
{
	bIgnited = true;

	// The dead machine wakes: heart glow roars, lever drops flat, tag retires.
	ForgeGlow->SetIntensity(4000.0f);
	if (LeverMesh)
	{
		LeverMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, -35.0f));
	}
	SetNameTag(TEXT("THE FORGE LIVES"), FLinearColor(0.3f, 1.0f, 0.4f, 1.0f));

	// Melt the cell open — the cage grants the relic + reveals the exit portal (its beat).
	for (TActorIterator<ACompanionCage> It(GetWorld()); It; ++It)
	{
		It->OpenCage();
		break;
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Forge] IGNITED — cell opened."));
}

void AForgePuzzle::SetNameTag(const FString& Text, const FLinearColor& Color)
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
