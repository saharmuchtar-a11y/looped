#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LoopedRunGameMode.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunEnded, bool, bVictory);

UCLASS()
class LOOPED_API ALoopedRunGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALoopedRunGameMode();

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnRoomCleared OnRoomCleared;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnRunEnded OnRunEnded;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void NotifyAllEnemiesDefeated();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void NotifyPlayerDied();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void RespawnAllEnemies();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void SetCursorVisibility(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void SpawnHubPortal(FName Destination = FName(TEXT("L_Hub")));

	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	int32 GetCurrentRoom() const { return CurrentRoomIndex; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	int32 GetTotalRooms() const { return TotalRoomsInFloor; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Run")
	int32 TotalRoomsInFloor = 5;

private:
	int32 CurrentRoomIndex = 0;
	int32 EnemiesAlive = 0;
	int32 RoomClearCount = 0;
	bool bRunActive = true;
};
