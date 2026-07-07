#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "ForgeSocket.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UWidgetComponent;

// A wall-mounted keyhole in Brann's forge puzzle. Tagged "KEY SLOT [E]" so the player knows
// where carried forge-keys go; press E with a key in hand to seat it (the slot lights ember and
// the tag flips to SEATED). All slots seated -> the lever will ignite the forge. Placeholder
// mesh until Sahar's lock asset lands (swap per-instance like the companion models).
UCLASS(Blueprintable)
class LOOPED_API AForgeSocket : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	AForgeSocket();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge|FX")
	FLinearColor EmberColor = FLinearColor(2.2f, 0.6f, 0.1f, 1.0f);

	// ILoopedInteractable — seat a carried key here.
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual FText GetInteractPrompt() const override
	{
		return bFilled ? FText::GetEmpty() : FText::FromString(TEXT("seat a forge-key"));
	}

	bool IsFilled() const { return bFilled; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	// Placeholder slot block — swap for the real lock model later.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SocketMesh;

	// The seated key, visible IN the lock once placed (hidden until then). Nudge the relative
	// offset per instance if a lock model wants the key elsewhere.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SeatedKeyMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

	// "KEY SLOT  [E]" -> "SEATED" floating tag.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> NameTagComp;

private:
	void SetNameTag(const FString& Text, const FLinearColor& Color);

	bool bFilled = false;
};
