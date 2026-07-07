#include "MovingPlatform.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Looped.h"

AMovingPlatform::AMovingPlatform()
{
	PrimaryActorTick.bCanEverTick = true;

	// Root IS the mesh: based-movement (riding) keys off the component the character stands on,
	// and moving the root every tick is exactly what CharacterMovement expects from a platform.
	PlatformMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlatformMesh"));
	SetRootComponent(PlatformMesh);
	PlatformMesh->SetMobility(EComponentMobility::Movable);
	PlatformMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		PlatformMesh->SetStaticMesh(CubeMesh.Object);
		// Flat slab default (~300x300x30) — resize per instance; swap the mesh for real art later.
		PlatformMesh->SetRelativeScale3D(FVector(3.0f, 3.0f, 0.3f));
	}
}

void AMovingPlatform::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the world route once: placed spot first, then each authored offset from it.
	const FVector Home = GetActorLocation();
	Route.Reset();
	Route.Add(Home);
	for (const FVector& S : Stops)
	{
		Route.Add(Home + S);
	}
	TargetIndex = (Route.Num() > 1) ? 1 : 0;
	Direction = 1;
	PauseRemaining = FMath::Max(0.0f, StartDelaySeconds);

	if (Route.Num() < 2)
	{
		SetActorTickEnabled(false); // no route authored — a static slab
	}
}

void AMovingPlatform::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (PauseRemaining > 0.0f)
	{
		PauseRemaining -= DeltaSeconds;
		return;
	}

	const FVector Target = Route[TargetIndex];
	const FVector Pos = GetActorLocation();
	const FVector Step = (Target - Pos).GetClampedToMaxSize(MoveSpeed * DeltaSeconds);
	SetActorLocation(Pos + Step, /*bSweep*/ false);

	if (FVector::DistSquared(GetActorLocation(), Target) < 4.0f)
	{
		// Reached a stop: ping-pong at the ends, brief pause at every stop.
		if (TargetIndex + Direction < 0 || TargetIndex + Direction >= Route.Num())
		{
			Direction *= -1;
		}
		TargetIndex += Direction;
		PauseRemaining = PauseSeconds;
	}
}
