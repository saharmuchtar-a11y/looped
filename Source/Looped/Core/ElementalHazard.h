#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElementalHazard.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class ALoopedCharacter;

// A floor / area hazard for the themed elemental rooms. While the player stands inside the volume it
// ticks damage and applies the element's on-hit status — Fire→Burn, Ice→Slow, Venom→Poison,
// Void→Weaken — all pulled from the element's DT_ProjectileElements row, so a lava pool burns, an ice
// patch chills, a toxic puddle poisons, a void rift weakens. Element is data: drop the actor in a
// room and set ElementRow; color + status follow. The placeholder plane mesh / additive material are
// meant to be swapped for real art per room.
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

	// If A > 0 this overrides the element row's OrbColor for the glow + light (per-instance retint).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|FX")
	FLinearColor ColorOverride = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hazard|FX")
	float LightIntensity = 1500.0f;

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

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> HazardVolume;

	// Placeholder tinted plane at the floor — swap for real art (lava, ice, ooze, rift) per room.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HazardMesh;

	// Element-colored glow so the hazard reads as dangerous even in a dim room.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> HazardLight;

	UFUNCTION()
	void OnBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// Repeating while the player is inside: bites damage + refreshes the element status.
	void DamageTick();

private:
	// Resolved from the ElementRow at BeginPlay.
	FLinearColor ElementColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	FName StatusEffect = NAME_None;
	float StatusMagnitude = 0.0f;
	float StatusDuration = 0.0f;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HazardMID;

	// The player currently standing in the hazard (null when empty). Drives the tick timer.
	UPROPERTY()
	TObjectPtr<ALoopedCharacter> PlayerInside = nullptr;

	FTimerHandle TickTimerHandle;

	// Pulse machinery: bHazardActive gates damage/status; the visual (mesh+light) mirrors it.
	// bForcedOff (a lever pulled) beats the pulse cycle.
	bool bHazardActive = true;
	bool bForcedOff = false;
	FTimerHandle PulseTimerHandle;
	void PulseFlip(bool bTurnOn);
	void ApplyHazardVisual();
};
