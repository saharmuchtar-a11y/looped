#include "ForgeKey.h"
#include "ForgePuzzle.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

AForgeKey::AForgeKey()
{
	PrimaryActorTick.bCanEverTick = true; // idle spin

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(RootComponent);
	Trigger->SetSphereRadius(140.0f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &AForgeKey::OnOverlap);

	// Placeholder: a small glowing block hovering at grab height. Real key art later.
	KeyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("KeyMesh"));
	KeyMesh->SetupAttachment(RootComponent);
	KeyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 110.0f));
	KeyMesh->SetRelativeScale3D(FVector(0.35f, 0.12f, 0.5f));
	KeyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		KeyMesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (GlowMat.Succeeded())
	{
		KeyMesh->SetMaterial(0, GlowMat.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(600.0f);
	GlowLight->SetAttenuationRadius(450.0f);
	GlowLight->SetCastShadows(false);

	PickupMessage = FText::FromString(TEXT("\"That's one of my forge-keys! Bring them all — the old girl needs every coil.\""));
}

void AForgeKey::BeginPlay()
{
	Super::BeginPlay();
	KeyMID = KeyMesh->CreateDynamicMaterialInstance(0);
	if (KeyMID)
	{
		KeyMID->SetVectorParameterValue(TEXT("OverlayColor"), KeyColor);
	}
	GlowLight->SetLightColor(KeyColor);
}

void AForgeKey::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bCollected) return;
	// Snap-set spin (no interp — the Lysa lesson): keys just rotate at a constant rate.
	FRotator R = KeyMesh->GetRelativeRotation();
	R.Yaw += SpinSpeed * DeltaSeconds;
	KeyMesh->SetRelativeRotation(R);
}

void AForgeKey::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (bCollected) return;
	ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor);
	if (!Player) return;
	bCollected = true;

	// Into the hands: the player CARRIES it now — seating happens at a wall socket with E.
	for (TActorIterator<AForgePuzzle> It(GetWorld()); It; ++It)
	{
		It->AddCarriedKey(Player);
		break;
	}
	Player->ShowCenterMessage(PickupMessage, 3.5f);
	UE_LOG(LogLoopedRun, Display, TEXT("[Forge] Key %d collected."), KeyIndex);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}
