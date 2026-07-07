#include "ElementalHazard.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Data/ProjectileTypes.h"
#include "Player/LoopedCharacter.h"
#include "Looped.h"

AElementalHazard::AElementalHazard()
{
	PrimaryActorTick.bCanEverTick = false;

	HazardVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("HazardVolume"));
	HazardVolume->SetBoxExtent(FVector(250.0f, 250.0f, 60.0f));
	HazardVolume->SetCollisionProfileName(TEXT("Trigger"));
	RootComponent = HazardVolume;

	// Placeholder floor patch — an additive-glow plane tinted to the element. Swap the mesh/material
	// per room for real art (the collision is the box, so the visual is free to change).
	HazardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HazardMesh"));
	HazardMesh->SetupAttachment(RootComponent);
	HazardMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HazardMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -55.0f)); // sit near the floor (box bottom)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		HazardMesh->SetStaticMesh(PlaneMesh.Object);
		HazardMesh->SetRelativeScale3D(FVector(5.0f, 5.0f, 1.0f)); // ~500x500 to match the box footprint
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Materials/M_EnemyOverlay.M_EnemyOverlay"));
	if (GlowMat.Succeeded())
	{
		HazardMesh->SetMaterial(0, GlowMat.Object);
	}

	HazardLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("HazardLight"));
	HazardLight->SetupAttachment(RootComponent);
	HazardLight->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	HazardLight->SetIntensityUnits(ELightUnits::Lumens);
	HazardLight->SetAttenuationRadius(600.0f);
	HazardLight->SetCastShadows(false);

	HazardVolume->OnComponentBeginOverlap.AddDynamic(this, &AElementalHazard::OnBeginOverlap);
	HazardVolume->OnComponentEndOverlap.AddDynamic(this, &AElementalHazard::OnEndOverlap);
}

void AElementalHazard::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the element (color + on-hit status) from DT_ProjectileElements — same table the orbs use.
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

	const FLinearColor UseColor = (ColorOverride.A > 0.0f) ? ColorOverride : ElementColor;

	if (HazardMesh)
	{
		HazardMID = HazardMesh->CreateDynamicMaterialInstance(0);
		if (HazardMID)
		{
			HazardMID->SetVectorParameterValue(TEXT("OverlayColor"), UseColor);
		}
	}
	if (HazardLight)
	{
		HazardLight->SetLightColor(UseColor);
		HazardLight->SetIntensity(LightIntensity);
	}

	// Pulsing blowers (Floor 3): start OFF (dark telegraph), first ignition after the phase
	// offset — so a row of blowers with staggered phases breathes in sequence.
	if (PulseOnSeconds > 0.0f && PulseOffSeconds > 0.0f)
	{
		bHazardActive = false;
		ApplyHazardVisual();
		GetWorldTimerManager().SetTimer(PulseTimerHandle,
			FTimerDelegate::CreateUObject(this, &AElementalHazard::PulseFlip, true),
			FMath::Max(0.05f, PulsePhaseSeconds + PulseOffSeconds), false);
	}
}

void AElementalHazard::PulseFlip(bool bTurnOn)
{
	if (bForcedOff) return; // a lever killed this hazard — stay dark
	bHazardActive = bTurnOn;
	ApplyHazardVisual();

	// While igniting with the player already standing inside: bite immediately (overlap won't re-fire).
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
	if (HazardMesh)  HazardMesh->SetVisibility(bHazardActive);
	if (HazardLight) HazardLight->SetVisibility(bHazardActive);
}

void AElementalHazard::SetHazardActive(bool bActive)
{
	// External kill switch (levers): OFF cancels the pulse cycle and goes dark; ON restores —
	// pulsing hazards resume their cycle, constant ones simply relight.
	bForcedOff = !bActive;
	bHazardActive = bActive;
	ApplyHazardVisual();
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

void AElementalHazard::OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (ALoopedCharacter* Player = Cast<ALoopedCharacter>(OtherActor))
	{
		PlayerInside = Player;
		if (!bHazardActive) return; // dark blower / lever-killed: standing here is safe (for now)
		// Immediate first bite so stepping in hurts right away, then a repeating tick while inside.
		DamageTick();
		if (PlayerInside) // DamageTick clears itself if the hit was fatal
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
		// Any burn/slow/weaken already applied keeps ticking on its own player-side timer (a hazard
		// leaves a mark you carry out — intended), then expires normally.
	}
}

void AElementalHazard::DamageTick()
{
	if (!PlayerInside || !PlayerInside->IsAlive() || !bHazardActive)
	{
		GetWorldTimerManager().ClearTimer(TickTimerHandle);
		if (!PlayerInside || !PlayerInside->IsAlive()) PlayerInside = nullptr;
		return;
	}

	PlayerInside->TakeDamageFromEnemy(DamagePerTick);
	if (bApplyStatus && !StatusEffect.IsNone())
	{
		// Re-applied each tick so the status persists while standing in it, and lingers briefly after.
		PlayerInside->ApplyElementalStatus(StatusEffect, StatusMagnitude, StatusDuration);
	}
}
