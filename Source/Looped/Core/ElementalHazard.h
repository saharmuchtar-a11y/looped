#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElementalHazard.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;
class UNavModifierComponent;
class ALoopedCharacter;

// A floor / area hazard for the themed elemental rooms. While the player stands inside the volume it
// ticks damage and applies the element's on-hit status — Fire→Burn, Ice→Slow, Venom→Poison,
// Void→Weaken — all pulled from the element's DT_ProjectileElements row. Visual = a THIN floor
// strip (lava texture for Fire, tinted stone for others) — not a glowing overlay plane.
UCLASS(Blueprintable)
class LOOPED_API AElementalHazard : public AActor
{
	GENERATED_BODY()

public:
	AElementalHazard();

	// DT_ProjectileElements RowName driving the color + on-hit status (Fire / Ice / Venom / Void).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard")
	FName ElementRow = FName(TEXT("Fire"));

	// Damage dealt each tick while the player is inside.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard")
	float DamagePerTick = 6.0f;

	// Seconds between damage ticks while standing in the hazard.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard")
	float TickInterval = 0.75f;

	// When true, each tick also applies the element's status (burn / slow / poison / weaken).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard")
	bool bApplyStatus = true;

	// If A > 0 this overrides the element row's OrbColor for the (optional) light only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|FX")
	FLinearColor ColorOverride = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// Soft fill light — default OFF (0). Hazards read as floor strips, not glowing volumes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|FX")
	float LightIntensity = 0.0f;

	// --- Pulsing (fire blowers / timed jets, Floor 3): the hazard cycles ON/OFF. While OFF it
	// goes dark and harmless — that's the telegraph. Both 0 (default) = always on (lava pools). ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|Pulse")
	float PulseOnSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|Pulse")
	float PulseOffSeconds = 0.0f;

	// Delays the first ignition so neighboring blowers can alternate (stagger per instance).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|Pulse")
	float PulsePhaseSeconds = 0.0f;

	// Kill switch for levers/scripts: false = dark + harmless until re-enabled. Pulsing pauses too.
	UFUNCTION(BlueprintCallable, Category = "Hazard")
	void SetHazardActive(bool bActive);

	// True while the hazard is dealing damage (pulse ON and not lever-forced off).
	UFUNCTION(BlueprintPure, Category = "Hazard")
	bool IsHazardActive() const { return bHazardActive && !bForcedOff; }

	// World-space AABB of the damage volume (for AI avoidance).
	UFUNCTION(BlueprintPure, Category = "Hazard")
	FBox GetHazardBounds() const;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> HazardVolume;

	// Thin floor strip (cube, flat Z) — material picked from ElementRow at BeginPlay.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HazardMesh;

	// Optional soft light (off by default).
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> HazardLight;

	// Marks the hazard footprint as non-walkable on the navmesh so AI cannot path onto lava/venom.
	// Pulsing blowers flip Null↔Default with the pulse so OFF windows stay traversable.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNavModifierComponent> NavModifier;

	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void DamageTick();

private:
	FLinearColor ElementColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	FName StatusEffect = NAME_None;
	float StatusMagnitude = 0.0f;
	float StatusDuration = 0.0f;

	UPROPERTY()
	TObjectPtr<ALoopedCharacter> PlayerInside = nullptr;

	FTimerHandle TickTimerHandle;

	bool bHazardActive = true;
	bool bForcedOff = false;
	FTimerHandle PulseTimerHandle;
	void PulseFlip(bool bTurnOn);
	void ApplyHazardVisual();
	void ApplyHazardMaterial();
	void SyncNavModifier();
};
