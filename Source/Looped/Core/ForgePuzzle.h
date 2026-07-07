#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "ForgePuzzle.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWidgetComponent;

// The dead forge at the heart of Brann's rescue (see looped_rescue_system.md). Fetch loop:
// pick up forge-keys (AForgeKey, walk-over) — you CARRY them — then press E at a wall keyhole
// (AForgeSocket, tagged "KEY SLOT [E]") to seat each one. All sockets seated -> press E on the
// LEVER here: the forge IGNITES and melts Brann's cell open (ACompanionCage::OpenCage grants
// the relic + reveals the exit). Pulling early costs a steam scald. Key/socket/lever/cage =
// the reusable puzzle-kit.
UCLASS(Blueprintable)
class LOOPED_API AForgePuzzle : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	AForgePuzzle();

	// How many sockets must be seated (match the placed AForgeSocket count).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge")
	int32 TotalKeys = 3;

	// Damage for pulling the lever with sockets unseated (steam-vent scald).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge")
	float FailScaldDamage = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge", meta = (MultiLine = true))
	FText IgniteMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge", meta = (MultiLine = true))
	FText ScaldMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Forge|FX")
	FLinearColor EmberColor = FLinearColor(2.2f, 0.6f, 0.1f, 1.0f);

	// --- Puzzle state API (keys call in, sockets draw out) ---
	// AForgeKey pickup: the player now carries one more key.
	void AddCarriedKey(class ALoopedCharacter* Player);

	// AForgeSocket seating: consume one carried key. False = hands empty.
	bool TakeCarriedKey();

	// AForgeSocket seated: count it, light a lamp, progress message.
	void NotifySocketFilled(class ALoopedCharacter* Player);

	// ILoopedInteractable — the master lever (press E beside the forge).
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return 320.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return bIgnited ? FText::GetEmpty() : FText::FromString(TEXT("pull the lever"));
	}

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	// Proximity hint only ("press E") — the lever itself fires via Interact.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> LeverTrigger;

	// Placeholder forge body + lever — swap for real art later.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ForgeMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LeverMesh;

	// One indicator lamp per seated socket, lit left-to-right.
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UPointLightComponent>> KeyLamps;

	// The forge's main glow — cold at start, roars alive on ignite.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> ForgeGlow;

	// "PULL LEVER  [E]" floating tag above the lever.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> NameTagComp;

	UFUNCTION()
	void OnLeverOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

private:
	void Ignite();
	void SetNameTag(const FString& Text, const FLinearColor& Color);

	int32 CarriedKeys = 0;   // picked up, not yet seated
	int32 FilledSockets = 0; // seated via AForgeSocket
	bool bIgnited = false;
	double LastLeverTime = -100.0; // rate-limits scalds (E-mash guard)
	double LastHintTime = -100.0;  // rate-limits the walk-up hint
};
