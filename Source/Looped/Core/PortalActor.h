#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

// How a portal decides its destination.
//   Fixed    — use TargetLevelName (default; preserves every placed actor + the boss→Hub portal).
//   StartRun — query the GameInstance for the FIRST room of the freshly generated run path.
//   NextRoom — query the GameInstance to ADVANCE one step along the run path.
UENUM(BlueprintType)
enum class ERoutePortalMode : uint8
{
	Fixed    UMETA(DisplayName = "Fixed (TargetLevelName)"),
	StartRun UMETA(DisplayName = "Start Run (first generated room)"),
	NextRoom UMETA(DisplayName = "Next Room (advance run path)")
};

UCLASS(Blueprintable)
class LOOPED_API APortalActor : public AActor
{
	GENERATED_BODY()

public:
	APortalActor();

	// Used only when Mode == Fixed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FName TargetLevelName;

	// Default Fixed = 100% backward compatible for already-placed portals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	ERoutePortalMode Mode = ERoutePortalMode::Fixed;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PortalMesh;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);
};
