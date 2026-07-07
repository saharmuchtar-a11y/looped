#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "LoopedLever.generated.h"

class UStaticMeshComponent;
class AGateActor;
class AElementalHazard;
class AMovingPlatform;

// Press-E lever for the Floor 2/3 rooms: one pull drives the room — opens/closes linked
// gates, kills (or restores) linked hazards, wakes linked platforms. The generalized child
// of the Forge's one-off lever; place, link actors, done.
UCLASS(Blueprintable)
class LOOPED_API ALoopedLever : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	ALoopedLever();

	// Gates to Toggle() on every pull.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	TArray<TObjectPtr<AGateActor>> LinkedGates;

	// Hazards switched by the pull: pulled = OFF (the fire blowers die), reset = back ON.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	TArray<TObjectPtr<AElementalHazard>> LinkedHazards;

	// Platforms started/stopped by the pull: pulled = moving, reset = frozen. (Leave a platform
	// unlinked for always-moving.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	TArray<TObjectPtr<AMovingPlatform>> LinkedPlatforms;

	// Platforms in LinkedPlatforms start FROZEN until the lever wakes them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	bool bLinkedPlatformsStartFrozen = true;

	// TRUE: one pull, then the lever goes dead (prompt disappears). FALSE: toggles back and forth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	bool bOneShot = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever")
	FText PromptVerb;

	// Center-screen beat on pull ("The flames die." / "Gears grind somewhere below.").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lever", meta = (MultiLine = true))
	FText PulledMessage;

	// ILoopedInteractable
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual FText GetInteractPrompt() const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LeverMesh;

private:
	bool bPulled = false;
	void ApplyLinks();
};
