#include "BossRoomExit.h"
#include "Enemies/EnemyBase.h"
#include "Core/LoopedRunGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Looped.h"

ABossRoomExit::ABossRoomExit()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABossRoomExit::BeginPlay()
{
	Super::BeginPlay();

	EnemiesRemaining = 0;
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* E = *It;
		if (!E) continue;
		EnemiesRemaining++;
		E->OnEnemyDied.AddDynamic(this, &ABossRoomExit::HandleEnemyDied);
	}
	UE_LOG(LogLoopedRun, Display, TEXT("BossRoomExit: tracking %d enemies"), EnemiesRemaining);
}

void ABossRoomExit::HandleEnemyDied(AEnemyBase* Enemy)
{
	if (bPortalSpawned) return;
	EnemiesRemaining = FMath::Max(0, EnemiesRemaining - 1);
	UE_LOG(LogLoopedRun, Display, TEXT("BossRoomExit: enemy down. Remaining=%d"), EnemiesRemaining);
	if (EnemiesRemaining > 0) return;

	bPortalSpawned = true;
	if (ALoopedRunGameMode* GM = Cast<ALoopedRunGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->SpawnHubPortal(PortalDestination);
		UE_LOG(LogLoopedRun, Display, TEXT("BossRoomExit: portal -> %s spawned"), *PortalDestination.ToString());
	}
}
