#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "LoopedGameInstance.generated.h"

UCLASS()
class LOOPED_API ULoopedGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Meta")
	int32 GetHunterRank() const { return HunterRank; }

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Meta")
	void AddRunXP(int32 XP);

private:
	int32 HunterRank = 0; // 0=F, 1=E, 2=D, 3=C, 4=B, 5=A, 6=S
	int32 CurrentXP = 0;
};
