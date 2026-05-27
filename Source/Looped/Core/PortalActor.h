#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

UCLASS(Blueprintable)
class LOOPED_API APortalActor : public AActor
{
	GENERATED_BODY()

public:
	APortalActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FName TargetLevelName;

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
