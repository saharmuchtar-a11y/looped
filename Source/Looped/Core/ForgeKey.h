#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForgeKey.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

// One forge-key in Brann's rescue puzzle (see looped_rescue_system.md). A glowing, slowly
// spinning pickup; walking into it "slots" it — the key vanishes, its lamp lights on the
// AForgePuzzle, and Brann comments. Placeholder visual until real key art exists.
UCLASS(Blueprintable)
class LOOPED_API AForgeKey : public AActor
{
	GENERATED_BODY()

public:
	AForgeKey();

	// Which lamp this key lights on the AForgePuzzle (0-based, unique per key).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge")
	int32 KeyIndex = 0;

	// Brann's line when this key is grabbed (each key gets its own voice line).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge", meta = (MultiLine = true))
	FText PickupMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge|FX")
	FLinearColor KeyColor = FLinearColor(2.2f, 0.6f, 0.1f, 1.0f); // ember orange

	// Idle spin speed (deg/sec) so the key reads as "take me".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge|FX")
	float SpinSpeed = 70.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> KeyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

private:
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> KeyMID;

	bool bCollected = false;
};
