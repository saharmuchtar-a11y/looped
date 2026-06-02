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

	// Instant run victory (used by the secret sphere event). Records the run, broadcasts
	// victory, and spawns the hub portal home.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Run")
	void NotifyRunWon();

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

	void EnsureNavMeshExists();

	// Spawns an ABossBase if the current level is the boss arena. Looks for an ATargetPoint
	// tagged "BossSpawn"; falls back to a hardcoded location if none present.
	void SpawnBossIfBossLevel();

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	TSubclassOf<class UUserWidget> BossHUDClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> BossHUDWidget;

	// Small "Room X / N" HUD shown on combat rooms (created + filled in C++, like the Boss HUD).
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	TSubclassOf<class UUserWidget> RoomHUDClass;

	UPROPERTY()
	TObjectPtr<UUserWidget> RoomHUDWidget;

	void CreateRoomHUD();

	// Refreshes the RoomHUD's "HealthNumbers" text block from the player's live HP each tick of
	// a light timer (same approach as UpdateBossHUD). Runs only while a RoomHUD exists.
	FTimerHandle PlayerHUDTimerHandle;

	UFUNCTION()
	void UpdatePlayerHUD();

	UPROPERTY()
	TObjectPtr<class AEnemyBase> TrackedBoss;

	FTimerHandle BossHUDTimerHandle;

	// World time when boss was spawned — used to compute kill duration for FastestBossKill stat.
	float BossSpawnedAtSeconds = 0.0f;

	UFUNCTION()
	void UpdateBossHUD();

	UFUNCTION()
	void HandleBossDied(class AEnemyBase* Boss);

	UPROPERTY(EditDefaultsOnly, Category = "Run")
	int32 TotalRoomsInFloor = 5;

	// --- Echoes currency rewards (permanent cross-run currency) ---
	// Per-room award is EchoesPerRoom * room depth, so deeper rooms pay more. Tune freely.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 EchoesPerRoom = 10;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 EchoesBossBonus = 100;

	// Per-run Shards: dropped per enemy killed + a bonus when the room is cleared.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 ShardsPerEnemy = 5;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 ShardsRoomClearBonus = 15;

private:
	int32 CurrentRoomIndex = 0;
	int32 EnemiesAlive = 0;
	int32 RoomClearCount = 0;
	bool bRunActive = true;
	// Guards the persistent RoomClears stat to exactly one increment per room clear.
	// Reset in RespawnAllEnemies() so a re-fought room can count again.
	bool bRoomClearCounted = false;
};
