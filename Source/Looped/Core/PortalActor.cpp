#include "PortalActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"
#include "Components/WidgetComponent.h"
#include "Components/PointLightComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Sound/SoundBase.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Core/LoopedGameInstance.h"
#include "Data/RoomRouting.h"
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
	PortalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Default = the real portal model with the same transform the placed fork portals use, so
	// RUNTIME-SPAWNED portals (the boss-death exit) look identical to every placed one. Falls back
	// to the old grey cylinder only if the asset is missing. Placed instances keep their overrides.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PortalModel(TEXT("/Game/new_assets/portalnew/StaticMeshes/portalnew.portalnew"));
	if (PortalModel.Succeeded())
	{
		PortalMesh->SetStaticMesh(PortalModel.Object);
		PortalMesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 2.0f));
		PortalMesh->SetRelativeRotation(FRotator(0.0f, 270.0f, 0.0f));
		PortalMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
	}
	else
	{
		PortalMesh->SetRelativeScale3D(FVector(2.0f, 2.0f, 3.0f));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
		if (CylinderMesh.Succeeded())
		{
			PortalMesh->SetStaticMesh(CylinderMesh.Object);
		}
	}

	// Floating type label above the portal — screen-space, hidden until SetForkType assigns one.
	LabelComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("LabelComp"));
	LabelComp->SetupAttachment(RootComponent);
	LabelComp->SetRelativeLocation(FVector(0.0f, 0.0f, 320.0f));
	LabelComp->SetWidgetSpace(EWidgetSpace::Screen);
	LabelComp->SetDrawSize(FVector2D(300.0f, 80.0f));
	LabelComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelComp->SetVisibility(false);
	static ConstructorHelpers::FClassFinder<UUserWidget> LabelWidgetClass(TEXT("/Game/UI/WBP_NameTag"));
	if (LabelWidgetClass.Succeeded())
	{
		LabelComp->SetWidgetClass(LabelWidgetClass.Class);
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> PortalSnd(TEXT("/Game/Audio/portal.portal"));
	if (PortalSnd.Succeeded()) PortalTravelSound = PortalSnd.Object;

	// --- FX baked into the portal so it hides/reveals WITH the portal (no more orphan effects) ---
	// Attach to the root (NOT PortalMesh) so the mesh's per-instance rotation/scale doesn't spin the FX.
	PortalFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("PortalFX"));
	PortalFX->SetupAttachment(RootComponent);
	PortalFX->SetRelativeLocation(FXLocalOffset);
	PortalFX->SetAutoActivate(false); // activated when the portal is enabled
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SwirlFX(TEXT("/Game/FX/NS_PortalSwirl.NS_PortalSwirl"));
	if (SwirlFX.Succeeded())
	{
		PortalFXSystem = SwirlFX.Object;
		PortalFX->SetAsset(PortalFXSystem);
	}

	PortalLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PortalLight"));
	PortalLight->SetupAttachment(RootComponent);
	PortalLight->SetRelativeLocation(FXLocalOffset);
	PortalLight->SetIntensityUnits(ELightUnits::Lumens);
	PortalLight->SetIntensity(PortalLightIntensity);
	PortalLight->SetAttenuationRadius(500.0f);
	PortalLight->SetLightColor(PortalLightColor);
	PortalLight->SetCastShadows(false);

	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &APortalActor::OnOverlapBegin);
}

void APortalActor::BeginPlay()
{
	Super::BeginPlay();

	// Apply editor-tunable FX values (lets us retint/reposition per-instance without recompiling).
	if (PortalFX)
	{
		if (PortalFXSystem) PortalFX->SetAsset(PortalFXSystem);
		PortalFX->SetRelativeLocation(FXLocalOffset);
	}
	if (PortalLight)
	{
		PortalLight->SetLightColor(PortalLightColor);
		PortalLight->SetIntensity(PortalLightIntensity);
		PortalLight->SetRelativeLocation(FXLocalOffset);
	}

	// A disabled portal hides itself at start and waits for ActivatePortal() / SetForkType().
	// IMPORTANT: GameMode may call ActivateRoomExitPortals() BEFORE this BeginPlay (actor order).
	// If a fork type was already assigned, do NOT re-disable — that softlocked treasure/? rooms
	// (Sahar: pick pedestal → no exit). F2 Puzzle / F3 Riddle / L_Treasure / DarkTreasure all share
	// bStartDisabled fork portals, so this race hit every MappedType=Treasure room.
	if (!RoomTypeId.IsNone())
	{
		SetPortalEnabled(true);
	}
	else
	{
		SetPortalEnabled(!bStartDisabled);
	}
}

void APortalActor::SetPortalEnabled(bool bEnabled)
{
	if (PortalMesh)
	{
		PortalMesh->SetVisibility(bEnabled);
	}
	if (TriggerBox)
	{
		// Off = no overlap events + no collision; On = restore the Trigger query collision.
		TriggerBox->SetGenerateOverlapEvents(bEnabled);
		TriggerBox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	if (!bEnabled && LabelComp)
	{
		LabelComp->SetVisibility(false); // a hidden portal hides its label too
	}
	// Editor-authored label ("ENTER THE LOOP") rides the portal's visibility. Fork portals
	// (RoomTypeId set at runtime) own the widget through SetForkType instead.
	else if (bEnabled && LabelComp && !PortalLabel.IsEmpty() && RoomTypeId.IsNone())
	{
		LabelComp->InitWidget();
		if (UUserWidget* W = LabelComp->GetUserWidgetObject())
		{
			if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
			{
				T->SetText(PortalLabel);
				T->SetColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.85f, 1.0f)));
			}
		}
		LabelComp->SetVisibility(true);
	}
	// FX follows the portal's visibility — fixes "effects showing where the portal isn't".
	if (PortalFX)
	{
		PortalFX->SetVisibility(bEnabled);
		if (bEnabled) PortalFX->Activate(true); else PortalFX->Deactivate();
	}
	if (PortalLight)
	{
		PortalLight->SetVisibility(bEnabled);
	}
}

void APortalActor::ActivatePortal()
{
	SetPortalEnabled(true);
	UE_LOG(LogLoopedRun, Display, TEXT("[Portal] Activated (Mode=%d)."), (int32)Mode);
}

void APortalActor::SetForkType(FName InRoomTypeId)
{
	RoomTypeId = InRoomTypeId;

	// Resolve the hovering label text + color from DT_RoomTypes (fallback to the raw id / white).
	FText Label = FText::FromName(InRoomTypeId);
	FLinearColor LabelColor = FLinearColor::White;
	if (UWorld* W = GetWorld())
	{
		if (ULoopedGameInstance* GI = W->GetGameInstance<ULoopedGameInstance>())
		{
			if (const FRoomTypeData* Row = GI->FindRoomType(InRoomTypeId))
			{
				if (!Row->DisplayLabel.IsEmpty())
				{
					Label = Row->DisplayLabel;
				}
				LabelColor = Row->LabelColor;
			}

			// Curse "Fogbound": the portals hide where they lead — every fork label reads the
			// same fog-grey "???", so the player chooses rooms blind. The portal still WORKS
			// (RoomTypeId is set above); only the label is blanked.
			if (GI->HasCurse(TEXT("Fogbound")))
			{
				Label = FText::FromString(TEXT("???"));
				LabelColor = FLinearColor(0.45f, 0.45f, 0.5f, 1.0f);
			}
		}
	}

	// Push the text + color into the WBP_NameTag's "NameText" block and reveal the label.
	if (LabelComp)
	{
		LabelComp->InitWidget();
		if (UUserWidget* W = LabelComp->GetUserWidgetObject())
		{
			if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(TEXT("NameText"))))
			{
				T->SetText(Label);
				T->SetColorAndOpacity(FSlateColor(LabelColor));
			}
		}
		LabelComp->SetVisibility(true);
	}

	SetPortalEnabled(true); // reveal the mesh + trigger
	UE_LOG(LogLoopedRun, Display, TEXT("[Portal] Fork type set to '%s'."), *InRoomTypeId.ToString());
}

void APortalActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor) return;

	// Accept only a player-controlled pawn.
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;

	// Already mid-fade from a prior overlap — ignore. Critical: NextRoom resolution advances the
	// run path index, so a double-fire here would skip a room.
	if (bTraveling) return;

	// A fork portal (type assigned at runtime via SetForkType) wins over the editor Mode: it loads a
	// random level of its type and records the room. Otherwise fall back to the Mode behaviour.
	FName Destination = NAME_None;
	if (!RoomTypeId.IsNone())
	{
		if (ULoopedGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<ULoopedGameInstance>() : nullptr)
		{
			Destination = GI->EnterRoomType(RoomTypeId);
		}
		if (Destination.IsNone())
		{
			UE_LOG(LogLoopedRun, Warning, TEXT("[Portal] Fork type '%s' resolved no level — overlap ignored."), *RoomTypeId.ToString());
			return;
		}
		// Skip the Mode switch entirely for typed fork portals.
		BeginTravel(Destination);
		return;
	}

	// Resolve the destination by mode. StartRun/NextRoom query the GameInstance run path;
	// Fixed uses the editor-set TargetLevelName (unchanged legacy behavior).
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

	BeginTravel(Destination);
}

void APortalActor::BeginTravel(FName Destination)
{
	// Single commit-to-travel point for ALL portals (fork + fixed/hub). Plays the portal SFX exactly
	// once on exit, fades to black, then DoTravel() swaps the level after the fade.
	bTraveling = true;
	PendingDestination = Destination;

	if (PortalTravelSound)
	{
		UGameplayStatics::PlaySound2D(this, PortalTravelSound);
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->PlayerCameraManager)
		{
			// Hold at black when finished so there's no flicker between fade-end and the level swap.
			PC->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, FadeOutDuration, FLinearColor::Black, false, true);
		}
	}

	// Travel after TravelDelay (>= fade) so the portal whoosh can finish under the held black before
	// OpenLevel cuts the audio. Snappy fade visual (FadeOutDuration), longer audio-safe travel hold.
	const float Delay = FMath::Max3(0.05f, FadeOutDuration, TravelDelay);
	GetWorldTimerManager().SetTimer(TravelTimerHandle, this, &APortalActor::DoTravel, Delay, false);
}

void APortalActor::DoTravel()
{
	UE_LOG(LogLoopedRun, Display, TEXT("[Portal] Mode=%d -> OpenLevel %s"), (int32)Mode, *PendingDestination.ToString());
	UGameplayStatics::OpenLevel(this, PendingDestination);
}
