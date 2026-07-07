#include "GateActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AGateActor::AGateActor()
{
	PrimaryActorTick.bCanEverTick = true;

	GateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMesh"));
	SetRootComponent(GateMesh);
	GateMesh->SetMobility(EComponentMobility::Movable);
	GateMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		GateMesh->SetStaticMesh(CubeMesh.Object);
		// Wall-slab default (~400 wide, 40 thick, 400 tall) — resize/swap per instance.
		GateMesh->SetRelativeScale3D(FVector(4.0f, 0.4f, 4.0f));
	}
}

void AGateActor::BeginPlay()
{
	Super::BeginPlay();
	ClosedLocation = GetActorLocation();
	bOpen = bStartOpen;
	if (bOpen)
	{
		SetActorLocation(ClosedLocation + OpenOffset);
	}
	SetActorTickEnabled(false); // wakes on Open/Close
}

void AGateActor::Open()   { bOpen = true;  SetActorTickEnabled(true); }
void AGateActor::Close()  { bOpen = false; SetActorTickEnabled(true); }
void AGateActor::Toggle() { bOpen ? Close() : Open(); }

void AGateActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FVector Target = bOpen ? ClosedLocation + OpenOffset : ClosedLocation;
	const FVector Pos = GetActorLocation();
	const FVector Step = (Target - Pos).GetClampedToMaxSize(MoveSpeed * DeltaSeconds);
	SetActorLocation(Pos + Step, /*bSweep*/ false);
	if (FVector::DistSquared(GetActorLocation(), Target) < 1.0f)
	{
		SetActorTickEnabled(false); // arrived — sleep until the next command
	}
}
