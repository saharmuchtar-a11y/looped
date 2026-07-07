#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MovingPlatform.generated.h"

class UStaticMeshComponent;

// A rideable patrol platform for the Floor 2/3 rooms (crossings over lava, moving cover,
// clockwork gauntlets). Ping-pongs through Stops (offsets from its placed position) at
// MoveSpeed, pausing PauseSeconds at each end. The character's based-movement rides it
// automatically because the root component moves every tick.
UCLASS(Blueprintable)
class LOOPED_API AMovingPlatform : public AActor
{
	GENERATED_BODY()

public:
	AMovingPlatform();

	// Waypoints as offsets from the PLACED location (index 0 = the placed spot itself is
	// implicit). One entry = simple A→B ping-pong; more = a patrol chain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform", meta = (MakeEditWidget = true))
	TArray<FVector> Stops { FVector(800.0f, 0.0f, 0.0f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
	float MoveSpeed = 250.0f;

	// Breather at each end of the route (board/leave window).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
	float PauseSeconds = 1.0f;

	// Delays the first departure — stagger neighboring platforms per instance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
	float StartDelaySeconds = 0.0f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PlatformMesh;

private:
	// World-space route resolved at BeginPlay: [placed location, placed+Stops[0], ...].
	TArray<FVector> Route;
	int32 TargetIndex = 0;
	int32 Direction = 1; // +1 forward through Route, -1 back
	float PauseRemaining = 0.0f;
};
