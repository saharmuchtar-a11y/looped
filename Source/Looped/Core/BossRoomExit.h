#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossRoomExit.generated.h"

class AEnemyBase;

// Place ONE of these in a level when the room should award a portal to Hub
// after all enemies are dead, without showing the card UI.
UCLASS(Blueprintable)
class LOOPED_API ABossRoomExit : public AActor
{
	GENERATED_BODY()

public:
	ABossRoomExit();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Boss")
	FName PortalDestination = TEXT("L_Hub");

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleEnemyDied(AEnemyBase* Enemy);

private:
	int32 EnemiesRemaining = 0;
	bool bPortalSpawned = false;
};
