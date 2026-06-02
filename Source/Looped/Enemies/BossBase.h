#pragma once

#include "CoreMinimal.h"
#include "Enemies/EnemyBase.h"
#include "BossBase.generated.h"

/**
 * Boss foundation. Forces ranged + boss flags on construction so the placed instance
 * always runs the Phase 1 ranged kit + Phase 2 teleport + Skyfall Volley pipeline.
 *
 * Designed to be subclassed by BP_BossEnemy (data-only) per the project's
 * "new content = new data, not new code" rule. Add new boss variants by creating
 * data table rows + BP subclass — not new C++ classes.
 */
UCLASS(Blueprintable)
class LOOPED_API ABossBase : public AEnemyBase
{
	GENERATED_BODY()

public:
	ABossBase();

protected:
	virtual void BeginPlay() override;
};
