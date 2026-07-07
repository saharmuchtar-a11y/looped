#include "MiraFragment.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/LoopedGameInstance.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

AMiraFragment::AMiraFragment()
{
	PrimaryActorTick.bCanEverTick = true; // idle spin + bob

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// A thin tilted sliver (cube placeholder like the forge keys — real art later). The roll
	// tilt + yaw spin in Tick read as a slowly turning crystal.
	FragmentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FragmentMesh"));
	FragmentMesh->SetupAttachment(RootComponent);
	FragmentMesh->SetRelativeLocation(FVector(0.0f, 0.0f, MeshBaseZ));
	FragmentMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, 25.0f));
	FragmentMesh->SetRelativeScale3D(FVector(0.18f, 0.18f, 0.75f));
	FragmentMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FragmentMesh->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (GlowMat.Succeeded())
	{
		FragmentMesh->SetMaterial(0, GlowMat.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(500.0f);
	GlowLight->SetAttenuationRadius(420.0f);
	GlowLight->SetCastShadows(false);

	PickupFlavor = FText::FromString(TEXT("\"...warmer. I can almost feel my hands again. Keep going.\""));
	CompleteFlavor = FText::FromString(TEXT("The last fragment sings — at the Hub, MIRA pulls herself back together. The loop remembers her now."));
}

void AMiraFragment::BeginPlay()
{
	Super::BeginPlay();

	// The hunt gates existence: outside it this actor simply isn't here. One check covers
	// pre-Serin runs, the one-per-run latch, and Mira already whole.
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI || !GI->IsMiraFragmentHuntActive())
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}

	bActive = true;
	FragmentMID = FragmentMesh->CreateDynamicMaterialInstance(0);
	if (FragmentMID)
	{
		FragmentMID->SetVectorParameterValue(TEXT("OverlayColor"), FragmentColor);
	}
	GlowLight->SetLightColor(FragmentColor);
}

void AMiraFragment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive) return;

	// Snap-set spin (no interp — the Lysa lesson) + a slow sine bob.
	BobTime += DeltaSeconds;
	FRotator R = FragmentMesh->GetRelativeRotation();
	R.Yaw += SpinSpeed * DeltaSeconds;
	FragmentMesh->SetRelativeRotation(R);
	FVector L = FragmentMesh->GetRelativeLocation();
	L.Z = MeshBaseZ + FMath::Sin(BobTime * 2.0f) * BobAmplitude;
	FragmentMesh->SetRelativeLocation(L);
}

void AMiraFragment::Interact(ALoopedCharacter* Player)
{
	if (!bActive) return;
	ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(GetGameInstance());
	if (!GI || !GI->IsMiraFragmentHuntActive()) return;
	bActive = false;

	const int32 Count = GI->CollectMiraFragment();
	const int32 Needed = GI->MiraFragmentsNeeded;
	const FText Msg = (Count >= Needed)
		? CompleteFlavor
		: FText::FromString(FString::Printf(TEXT("MIRA'S FRAGMENT — %d of %d. %s"),
			Count, Needed, *PickupFlavor.ToString()));
	Player->ShowCenterMessage(Msg, 4.5f);

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}
