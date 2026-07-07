#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "MiraFragment.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

// One fragment of Mira (the 4th rescue — she is SCATTERED through the loop, not caged).
// A violet, slowly spinning glint tucked into a deep room; E to take it. Exists only while
// the hunt is live (ULoopedGameInstance::IsMiraFragmentHuntActive) — otherwise it hides
// itself on BeginPlay, so the same placed actors serve every phase of the meta.
UCLASS(Blueprintable)
class LOOPED_API AMiraFragment : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	AMiraFragment();

	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return 260.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return bActive ? FText::FromString(TEXT("take Mira's fragment")) : FText::GetEmpty();
	}

	// Her voice, appended after the "FRAGMENT n of N" count on pickup.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mira", meta = (MultiLine = true))
	FText PickupFlavor;

	// Shown instead when this fragment completes the set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mira", meta = (MultiLine = true))
	FText CompleteFlavor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mira|FX")
	FLinearColor FragmentColor = FLinearColor(1.6f, 0.5f, 2.6f, 1.0f); // Mira violet

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mira|FX")
	float SpinSpeed = 55.0f;

	// Vertical bob (uu) so the glint catches the eye across a room.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mira|FX")
	float BobAmplitude = 10.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FragmentMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

private:
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FragmentMID;

	bool bActive = false;
	float BobTime = 0.0f;
	float MeshBaseZ = 110.0f;
};
