#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LoopedInteractable.generated.h"

UINTERFACE(MinimalAPI)
class ULoopedInteractable : public UInterface
{
	GENERATED_BODY()
};

// Press-E interaction target. ALoopedCharacter::TryInteract picks the nearest implementer in
// range on each Interact press (E) and calls Interact. First users: the forge keyholes + lever
// (Brann's rescue); anything future that wants "walk up and press E" implements this.
class LOOPED_API ILoopedInteractable
{
	GENERATED_BODY()

public:
	// The E press. Player is never null.
	virtual void Interact(class ALoopedCharacter* Player) = 0;

	// Max distance (uu) from the player at which this target is eligible.
	virtual float GetInteractRange() const { return 300.0f; }

	// The verb for the proximity prompt: shown as "Press [E] to <verb>" while the player is in
	// range. Return empty to show NOTHING (e.g. a taken pedestal, a dark altar keeping its
	// mystery, an already-open shop).
	virtual FText GetInteractPrompt() const { return FText::GetEmpty(); }
};
