#include "PortalActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

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

	ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (OtherActor == PlayerChar && !TargetLevelName.IsNone())
	{
		UGameplayStatics::OpenLevel(this, TargetLevelName);
	}
}
