#include "ElementalHazard.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Default.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Data/ProjectileTypes.h"
#include "Player/LoopedCharacter.h"
#include "Core/LoopedGameInstance.h"
#include "Looped.h"

AElementalHazard::AElementalHazard()
{
	PrimaryActorTick.bCanEverTick = false;

	HazardVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("HazardVolume"));
	HazardVolume->SetBoxExtent(FVector(250.0f, 250.0f, 60.0f));
	HazardVolume->SetCollisionProfileName(TEXT("Trigger"));
	RootComponent = HazardVolume;

	// Thin floor strip (flat cube) — reads as a painted hazard on the ground, not a glow plane.
	HazardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HazardMesh"));
	HazardMesh->SetupAttachment(RootComponent);
	HazardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Sit ON the floor (actor is placed at floor height). Old -58 buried the strip under the slab.
	HazardMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 4.0f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		HazardMesh->SetStaticMesh(CubeMesh.Object);
		// ~500x500 footprint matching the default box, Z thin but readable (~20uu)
		HazardMesh->SetRelativeScale3D(FVector(5.0f, 5.0f, 0.2f));
	}

	HazardLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HazardLight"));
	HazardLight->SetupAttachment(RootComponent);
	HazardLight->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	HazardLight->SetIntensityUnits(ELightUnits::Lumens);
	HazardLight->SetAttenuationRadius(400.0f);
	HazardLight->SetCastShadows(false);
	HazardLight->SetVisibility(false); // off unless LightIntensity > 0 at BeginPlay

	HazardVolume->OnComponentBeginOverlap.AddDynamic(this, &AElementalHazard::OnBeginOverlap);
	HazardVolume->OnComponentEndOverlap.AddDynamic(this, &AElementalHazard::OnEndOverlap);

	// Block AI nav through the burn footprint (player can still walk — trigger only).
	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->SetAreaClass(UNavArea_Null::StaticClass());
}

void AElementalHazard::ApplyHazardMaterial()
{
	if (!HazardMesh) return;

	// Stretch the floor strip to match the damage box footprint (all rooms).
	// Cube is 100uu; box extent is half-size in local space — both inherit actor scale together.
	const FVector Extent = HazardVolume
		? HazardVolume->GetUnscaledBoxExtent()
		: FVector(250.0f, 250.0f, 60.0f);
	const float ScaleX = FMath::Max(0.1f, (Extent.X * 2.0f) / 100.0f); // full width / cube size
	const float ScaleY = FMath::Max(0.1f, (Extent.Y * 2.0f) / 100.0f);
	const float ScaleZ = 0.2f; // thin paint on the floor (~20uu * actor Z)

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		HazardMesh->SetStaticMesh(Cube);
	}
	// ON the floor, slightly proud so it reads over the slab (was -58 = invisible underground).
	HazardMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 4.0f));
	HazardMesh->SetRelativeScale3D(FVector(ScaleX, ScaleY, ScaleZ));
	HazardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HazardMesh->SetVisibility(true);
	HazardMesh->SetHiddenInGame(false);

	// Per-element floor strip MIs (Lava004 for Fire; tinted stone for the rest).
	const TCHAR* Path = TEXT("/Game/Materials/MI_Hazard_Fire.MI_Hazard_Fire");
	const FString Row = ElementRow.ToString();
	if (Row.Equals(TEXT("Venom"), ESearchCase::IgnoreCase) || Row.Equals(TEXT("Poison"), ESearchCase::IgnoreCase))
	{
		Path = TEXT("/Game/Materials/MI_Hazard_Venom.MI_Hazard_Venom");
	}
	else if (Row.Equals(TEXT("Ice"), ESearchCase::IgnoreCase) || Row.Equals(TEXT("Frost"), ESearchCase::IgnoreCase))
	{
		Path = TEXT("/Game/Materials/MI_Hazard_Ice.MI_Hazard_Ice");
	}
	else if (Row.Equals(TEXT("Void"), ESearchCase::IgnoreCase))
	{
		Path = TEXT("/Game/Materials/MI_Hazard_Void.MI_Hazard_Void");
	}
	else
	{
		Path = TEXT("/Game/Materials/MI_Hazard_Fire.MI_Hazard_Fire"); // Fire + unknown → lava
	}

	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path))
	{
		HazardMesh->SetMaterial(0, Mat);
	}
	else
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("ElementalHazard: missing hazard MI at %s"), Path);
	}
}

void AElementalHazard::BeginPlay()
{
	Super::BeginPlay();

	if (UDataTable* ElemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_ProjectileElements.DT_ProjectileElements")))
	{
		if (const FProjectileElementData* Row = ElemTable->FindRow<FProjectileElementData>(ElementRow, TEXT("ElementalHazard"), false))
		{
			ElementColor = Row->OrbColor;
			StatusEffect = Row->StatusEffect;
			StatusMagnitude = Row->StatusMagnitude;
			StatusDuration = Row->StatusDuration;
		}
		else
		{
			UE_LOG(LogLoopedCore, Warning, TEXT("ElementalHazard: element row '%s' not found in DT_ProjectileElements"), *ElementRow.ToString());
		}
	}

	ApplyHazardMaterial();

	// Floor strips read as paint on the ground — kill fill lights even if old instances
	// still have LightIntensity serialized from the glow-plane era (Sahar: no glowing hazards).
	if (HazardLight)
	{
		HazardLight->SetIntensity(0.0f);
		HazardLight->SetVisibility(false);
	}

	if (PulseOnSeconds > 0.0f && PulseOffSeconds > 0.0f)
	{
		bHazardActive = false;
		ApplyHazardVisual();
		SyncNavModifier();
		GetWorldTimerManager().SetTimer(PulseTimerHandle,
			FTimerDelegate::CreateUObject(this, &AElementalHazard::PulseFlip, true),
			FMath::Max(0.05f, PulsePhaseSeconds + PulseOffSeconds), false);
	}
	else
	{
		SyncNavModifier();
	}
}

void AElementalHazard::SyncNavModifier()
{
	if (!NavModifier) return;
	// Active burn = unwalkable for AI; pulse OFF / lever-forced off = walkable again.
	if (IsHazardActive())
	{
		NavModifier->SetAreaClass(UNavArea_Null::StaticClass());
	}
	else
	{
		NavModifier->SetAreaClass(UNavArea_Default::StaticClass());
	}
}

void AElementalHazard::PulseFlip(bool bTurnOn)
{
	if (bForcedOff) return;
	bHazardActive = bTurnOn;
	ApplyHazardVisual();
	SyncNavModifier();

	if (bHazardActive && PlayerInside)
	{
		DamageTick();
		if (PlayerInside)
		{
			GetWorldTimerManager().SetTimer(TickTimerHandle, this, &AElementalHazard::DamageTick, TickInterval, true);
		}
	}

	GetWorldTimerManager().SetTimer(PulseTimerHandle,
		FTimerDelegate::CreateUObject(this, &AElementalHazard::PulseFlip, !bTurnOn),
		bTurnOn ? PulseOnSeconds : PulseOffSeconds, false);
}

void AElementalHazard::ApplyHazardVisual()
{
	if (HazardMesh) HazardMesh->SetVisibility(bHazardActive);
	// Lights stay off — hazards are floor strips, not glow volumes.
	if (HazardLight) HazardLight->SetVisibility(false);
}

void AElementalHazard::SetHazardActive(bool bActive)
{
	bForcedOff = !bActive;
	bHazardActive = bActive;
	ApplyHazardVisual();
	SyncNavModifier();
	if (!bActive)
	{
		GetWorldTimerManager().ClearTimer(PulseTimerHandle);
		GetWorldTimerManager().ClearTimer(TickTimerHandle);
	}
	else if (PulseOnSeconds > 0.0f && PulseOffSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(PulseTimerHandle,
			FTimerDelegate::CreateUObject(this, &AElementalHazard::PulseFlip, false),
			PulseOnSeconds, false);
	}
	else if (PlayerInside)
	{
		GetWorldTimerManager().SetTimer(TickTimerHandle, this, &AElementalHazard::DamageTick, TickInterval, true);
	}
}

FBox AElementalHazard::GetHazardBounds() const
{
	if (!HazardVolume)
	{
		return FBox(GetActorLocation() - FVector(100.0f), GetActorLocation() + FVector(100.0f));
	}
	return HazardVolume->Bounds.GetBox();
}

void AElementalHazard::OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor))
	{
		// Death-cam ragdoll / corpse overlaps must not arm the tick or apply status.
		if (!Player->IsAlive()) return;
		PlayerInside = Player;
		if (!bHazardActive) return;
		DamageTick();
		if (PlayerInside)
		{
			GetWorldTimerManager().SetTimer(TickTimerHandle, this, &AElementalHazard::DamageTick, TickInterval, true);
		}
	}
}

void AElementalHazard::OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (PlayerInside && Cast<ALoopedCharacter>(OtherActor) == PlayerInside)
	{
		PlayerInside = nullptr;
		GetWorldTimerManager().ClearTimer(TickTimerHandle);
	}
}

void AElementalHazard::DamageTick()
{
	// IsValid: PlayerInside can dangle if the pawn was destroyed mid-overlap (level travel / death).
	if (!IsValid(PlayerInside) || !PlayerInside->IsAlive() || !bHazardActive)
	{
		GetWorldTimerManager().ClearTimer(TickTimerHandle);
		if (!IsValid(PlayerInside) || !PlayerInside->IsAlive()) PlayerInside = nullptr;
		return;
	}

	// Blessing "Ashwalker": floor hazards (lava/fire, venom, ice, void) deal no damage and apply no status.
	if (const ULoopedGameInstance* GI = PlayerInside->GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasRunArtifact(TEXT("Ashwalker")))
		{
			return;
		}
	}

	PlayerInside->TakeDamageFromEnemy(DamagePerTick);
	// Lethal tick may have killed them (or Second Wind revived them). Never apply status on a corpse —
	// death-cam ragdoll re-overlaps this volume and used to call ApplyElementalStatus → IsAlive AV.
	if (!IsValid(PlayerInside) || !PlayerInside->IsAlive())
	{
		GetWorldTimerManager().ClearTimer(TickTimerHandle);
		PlayerInside = nullptr;
		return;
	}
	if (bApplyStatus && !StatusEffect.IsNone())
	{
		PlayerInside->ApplyElementalStatus(StatusEffect, StatusMagnitude, StatusDuration);
	}
}
