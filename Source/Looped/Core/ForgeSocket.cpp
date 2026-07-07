#include "ForgeSocket.h"
#include "ForgePuzzle.h"
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

AForgeSocket::AForgeSocket()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Placeholder: a slot block at hand height. Swap for the real lock model when it lands.
	SocketMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SocketMesh"));
	SocketMesh->SetupAttachment(RootComponent);
	SocketMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	SocketMesh->SetRelativeScale3D(FVector(0.55f, 0.25f, 0.55f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		SocketMesh->SetStaticMesh(CubeMesh.Object);
	}

	// The key that appears seated in the lock after a successful place (hidden until then).
	SeatedKeyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeatedKeyMesh"));
	SeatedKeyMesh->SetupAttachment(RootComponent);
	SeatedKeyMesh->SetRelativeLocation(FVector(0.0f, 28.0f, 120.0f)); // just proud of the lock face
	SeatedKeyMesh->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	SeatedKeyMesh->SetRelativeScale3D(FVector(0.4f, 0.4f, 0.4f));
	SeatedKeyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SeatedKeyMesh->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> KeyMesh(TEXT("/Game/new_assets/slotmachine/key/StaticMeshes/key.key"));
	if (KeyMesh.Succeeded())
	{
		SeatedKeyMesh->SetStaticMesh(KeyMesh.Object);
	}

	GlowLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GlowLight"));
	GlowLight->SetupAttachment(RootComponent);
	GlowLight->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(180.0f); // dim ember — "something goes here"
	GlowLight->SetAttenuationRadius(350.0f);
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
}

void AForgeSocket::BeginPlay()
{
	Super::BeginPlay();
	GlowLight->SetLightColor(EmberColor);
	SetNameTag(TEXT("KEY SLOT"), EmberColor);
}

void AForgeSocket::Interact(ALoopedCharacter* Player)
{
	if (bFilled)
	{
		Player->ShowCenterMessage(FText::FromString(TEXT("This coil is already seated.")), 2.0f);
		return;
	}

	AForgePuzzle* Puzzle = nullptr;
	for (TActorIterator<AForgePuzzle> It(GetWorld()); It; ++It) { Puzzle = *It; break; }
	if (!Puzzle) return;

	if (!Puzzle->TakeCarriedKey())
	{
		Player->ShowCenterMessage(FText::FromString(TEXT("The slot is empty and so are your hands — find a forge-key.")), 3.0f);
		return;
	}

	// Seat it: the key appears IN the lock, slot roars ember, tag flips, the puzzle counts it.
	bFilled = true;
	if (SeatedKeyMesh) SeatedKeyMesh->SetVisibility(true);
	GlowLight->SetIntensity(1200.0f);
	SetNameTag(TEXT("SEATED"), FLinearColor(0.3f, 1.0f, 0.4f, 1.0f));
	Puzzle->NotifySocketFilled(Player);
	UE_LOG(LogLoopedRun, Display, TEXT("[Forge] Socket '%s' seated."), *GetName());
}

void AForgeSocket::SetNameTag(const FString& Text, const FLinearColor& Color)
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
