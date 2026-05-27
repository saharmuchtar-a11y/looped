#include "LoopedGameInstance.h"
#include "Looped.h"

void ULoopedGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogLoopedCore, Display, TEXT("LoopedGameInstance initialized. Hunter Rank: %d"), HunterRank);
}

void ULoopedGameInstance::AddRunXP(int32 XP)
{
	CurrentXP += XP;
	UE_LOG(LogLoopedCore, Display, TEXT("Run XP added: %d (Total: %d)"), XP, CurrentXP);
	// POC: rank progression deferred to post-prototype
}
