#include "LoopedRunGameMode.h"
#include "Looped.h"
#include "Enemies/EnemyBase.h"
#include "Core/PortalActor.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

ALoopedRunGameMode::ALoopedRunGameMode()
{
	// DefaultPawnClass set via Blueprint or World Settings — not hardcoded
	// This allows BP_LoopedCharacter (with input actions) to be used
}

void ALoopedRunGameMode::BeginPlay()
{
	Super::BeginPlay();
	CurrentRoomIndex = 1;
	bRunActive = true;
	UE_LOG(LogLoopedRun, Display, TEXT("Run started. Floor 1, Room %d/%d"), CurrentRoomIndex, TotalRoomsInFloor);
}

void ALoopedRunGameMode::NotifyAllEnemiesDefeated()
{
	if (!bRunActive) return;

	RoomClearCount++;
	UE_LOG(LogLoopedRun, Display, TEXT("Room cleared! (clear count: %d)"), RoomClearCount);
	OnRoomCleared.Broadcast();
}

void ALoopedRunGameMode::RespawnAllEnemies()
{
	int32 Count = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		It->Respawn();
		Count++;
	}
	// Reset the room-cleared flag so the next wave can trigger it again
	bRunActive = true;
	UE_LOG(LogLoopedRun, Display, TEXT("Respawned %d enemies"), Count);
}

void ALoopedRunGameMode::SetCursorVisibility(bool bVisible)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->bShowMouseCursor = bVisible;
	}
}

void ALoopedRunGameMode::SpawnHubPortal(FName Destination)
{
	if (ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		FVector SpawnLoc = Player->GetActorLocation() + Player->GetActorForwardVector() * 400.0f;
		SpawnLoc.Z = Player->GetActorLocation().Z;

		FActorSpawnParameters Params;
		APortalActor* Portal = GetWorld()->SpawnActor<APortalActor>(SpawnLoc, FRotator::ZeroRotator, Params);
		if (Portal)
		{
			Portal->TargetLevelName = Destination;
			Portal->SetActorLabel(TEXT("RunPortal"));
			UE_LOG(LogLoopedRun, Display, TEXT("Portal spawned → %s"), *Destination.ToString());
		}
	}
}

void ALoopedRunGameMode::NotifyPlayerDied()
{
	if (!bRunActive) return;

	UE_LOG(LogLoopedRun, Display, TEXT("Player died in Room %d. Run ended."), CurrentRoomIndex);
	bRunActive = false;
	OnRunEnded.Broadcast(false);
}
