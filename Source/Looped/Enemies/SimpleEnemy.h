#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimpleEnemy.generated.h"

UCLASS(Blueprintable)
class LOOPED_API ASimpleEnemy : public AActor
{
	GENERATED_BODY()

public:
	ASimpleEnemy();

	UFUNCTION(BlueprintCallable)
	void TakeDamageFromPlayer(float Damage, AActor* DamageSource);

	UFUNCTION(BlueprintPure)
	bool IsAlive() const { return CurrentHealth > 0.0f; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy")
	float MaxHealth = 40.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Enemy")
	float CurrentHealth = 40.0f;
};
