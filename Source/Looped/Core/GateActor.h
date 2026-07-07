#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GateActor.generated.h"

class UStaticMeshComponent;

// A sliding blocker driven by levers/scripts: interps its mesh between the placed (closed)
// pose and placed+OpenOffset. Default sinks into the floor; set the offset sideways/up for
// doors and drawbridges. Pairs with ALoopedLever, but Open/Close are BlueprintCallable for
// any future trigger.
UCLASS(Blueprintable)
class LOOPED_API AGateActor : public AActor
{
	GENERATED_BODY()

public:
	AGateActor();

	// Where "open" is, relative to the placed spot (default: sunk beneath the floor).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate", meta = (MakeEditWidget = true))
	FVector OpenOffset = FVector(0.0f, 0.0f, -420.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	float MoveSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
	bool bStartOpen = false;

	UFUNCTION(BlueprintCallable, Category = "Gate") void Open();
	UFUNCTION(BlueprintCallable, Category = "Gate") void Close();
	UFUNCTION(BlueprintCallable, Category = "Gate") void Toggle();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GateMesh;

private:
	FVector ClosedLocation = FVector::ZeroVector;
	bool bOpen = false;
};
