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

	// FLOORS: mid-run boss kill — spawns the labeled "DESCEND" portal into the next floor's
	// first room. Unlike SpawnHubPortal this does NOT end the run path (the descent continues it).
	void SpawnDescentPortal(FName Destination);

	// Post-boss "?" pedestal (blessing OR cards). Call before / with the exit portal.
	void SpawnBossRewardPedestal();

	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	int32 GetCurrentRoom() const { return CurrentRoomIndex; }

	UFUNCTION(BlueprintPure, Category = "LOOPED|Run")
	int32 GetTotalRooms() const { return TotalRoomsInFloor; }

	// Curse "Haunted": one rise per room. The dying enemy claims the token; first claim wins.
	bool TryConsumeHauntedToken();

protected:
	virtual void BeginPlay() override;

	// --- Tutorial routing (first launch) ---
	// A save that never received the monitor-gate artifact is brand new: entering the Hub
	// bounces it to the tutorial, where Orin's rescue grants the artifact. Legacy saves get
	// the artifact migrated on load, so only genuinely fresh saves route. None = routing off.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Tutorial")
	FName TutorialGateArtifact = TEXT("Orin");

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Tutorial")
	FName TutorialLevelName = TEXT("L_Tutorial");

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

	// Creates the Souls-style boss HUD for TrackedBoss. Retries on a short timer if the
	// PlayerController isn't ready yet (e.g. loading the boss map directly), so the bar reliably shows.
	UFUNCTION()
	void SetupBossHUD();

	UFUNCTION()
	void UpdateBossHUD();

	UFUNCTION()
	void HandleBossDied(class AEnemyBase* Boss);

	UPROPERTY(EditDefaultsOnly, Category = "Run")
	int32 TotalRoomsInFloor = 5;

	// --- Echoes currency rewards (permanent cross-run currency) ---
	// Per-room award is EchoesPerRoom * room depth, so deeper rooms pay more. Tune freely.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 EchoesPerRoom = 6;   // was 10 — a full run paid ~550 Echoes = one vault meta per run, too fast

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 EchoesBossBonus = 100;

	// Per-run Shards: dropped per enemy killed + a bonus when the room is cleared.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 ShardsPerEnemy = 5;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Currency")
	int32 ShardsRoomClearBonus = 20;  // combat should out-earn event luck

private:
	int32 CurrentRoomIndex = 0;
	int32 EnemiesAlive = 0;
	int32 RoomClearCount = 0;
	bool bRunActive = true;
	// Guards the persistent RoomClears stat to exactly one increment per room clear.
	// Reset in RespawnAllEnemies() so a re-fought room can count again.
	bool bRoomClearCounted = false;

	// Curse "Haunted": the room's single rise token (GameMode = per-level lifetime = per-room).
	bool bHauntedTokenUsed = false;

	// Curse "Swarm": clones one placed enemy per CurseSwarmExtraEnemies at a nav point away from
	// the player. Deferred a beat past BeginPlay so placed enemies + player pawn exist.
	void SpawnSwarmEnemies();
	FTimerHandle SwarmSpawnTimerHandle;
};
