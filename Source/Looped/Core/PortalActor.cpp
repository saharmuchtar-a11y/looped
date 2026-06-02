#include "PortalActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/LoopedGameInstance.h"
#include "Looped.h"

APortalActor::APortalActor()
{
	PrimaryActorTick.bCanEverTick = false;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetBoxExtent(FVector(100.0f, 100.0f, 150.0f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	RootComponent = TriggerBox;

	PortalMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PortalMesh"));
	PortalMesh->SetupAttachment(RootComponent);
	PortalMesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 3.0f));
	PortalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
	if (CylinderMesh.Succeeded())
	{
		PortalMesh->SetStaticMesh(CylinderMesh.Object);
	}

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &APortalActor::OnOverlapBegin);
}

void APortalActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor) return;

	// Accept only a player-controlled pawn.
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;

	// Resolve the destination by mode. StartRun/NextRoom query the GameInstance run path;
	// Fixed uses the editor-set TargetLevelName (unchanged legacy behavior).
	FName Destination = NAME_None;
	switch (Mode)
	{
	case ERoutePortalMode::StartRun:
		if (ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr)
		{
			Destination = GI->BeginRunPath();
		}
		break;
	case ERoutePortalMode::NextRoom:
		if (ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr)
		{
			Destination = GI->AdvanceToNextRoom();
		}
		break;
	case ERoutePortalMode::Fixed:
	default:
		Destination = TargetLevelName;
		break;
	}

	if (Destination.IsNone())
	{
		UE_LOG(LogLoopedRun, Warning, TEXT("[Portal] No destination resolved (Mode=%d) — overlap ignored."), (int32)Mode);
		return;
	}

	UE_LOG(LogLoopedRun, Display, TEXT("[Portal] Mode=%d -> OpenLevel %s"), (int32)Mode, *Destination.ToString());
	UGameplayStatics::OpenLevel(this, Destination);
}
