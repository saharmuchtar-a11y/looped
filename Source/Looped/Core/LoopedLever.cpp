#include "LoopedLever.h"
#include "GateActor.h"
#include "ElementalHazard.h"
#include "MovingPlatform.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

ALoopedLever::ALoopedLever()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LeverMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeverMesh"));
	LeverMesh->SetupAttachment(SceneRoot);
	LeverMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Sahar's lever model (the Forge's) as the default look; falls back to a cube.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LeverModel(TEXT("/Game/new_assets/serin/lever.lever"));
	if (LeverModel.Succeeded())
	{
		LeverMesh->SetStaticMesh(LeverModel.Object);
	}
	else
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
		if (CubeMesh.Succeeded())
		{
			LeverMesh->SetStaticMesh(CubeMesh.Object);
			LeverMesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 1.2f));
		}
	}

	PromptVerb = FText::FromString(TEXT("pull the lever"));
}

void ALoopedLever::BeginPlay()
{
	Super::BeginPlay();
	// Linked platforms wait for the lever by default (the room starts "dead", the pull wakes it).
	if (bLinkedPlatformsStartFrozen)
	{
		for (AMovingPlatform* P : LinkedPlatforms)
		{
			if (P) P->SetActorTickEnabled(false);
		}
	}
}

void ALoopedLever::Interact(ALoopedCharacter* Player)
{
	if (bOneShot && bPulled) return;
	bPulled = !bPulled;

	// Throw the handle (the Forge's "thrown" look) — roll over on pull, back upright on reset.
	if (LeverMesh)
	{
		LeverMesh->SetRelativeRotation(FRotator(0.0f, 0.0f, bPulled ? -35.0f : 0.0f));
	}
	ApplyLinks();

	if (Player && !PulledMessage.IsEmpty() && bPulled)
	{
		Player->ShowCenterMessage(PulledMessage, 3.5f);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("[Lever] %s -> %s (%d gates, %d hazards, %d platforms)"),
		*GetActorLabel(), bPulled ? TEXT("PULLED") : TEXT("RESET"),
		LinkedGates.Num(), LinkedHazards.Num(), LinkedPlatforms.Num());
}

void ALoopedLever::ApplyLinks()
{
	for (AGateActor* G : LinkedGates)
	{
		if (G) G->Toggle();
	}
	for (AElementalHazard* H : LinkedHazards)
	{
		if (H) H->SetHazardActive(!bPulled); // pulled = the flames die
	}
	for (AMovingPlatform* P : LinkedPlatforms)
	{
		if (P) P->SetActorTickEnabled(bPulled); // pulled = the gears wake
	}
}

FText ALoopedLever::GetInteractPrompt() const
{
	if (bOneShot && bPulled) return FText::GetEmpty(); // dead lever keeps quiet
	return PromptVerb;
}
