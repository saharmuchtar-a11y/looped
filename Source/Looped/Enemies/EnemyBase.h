#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Data/EnemyData.h"
#include "EnemyBase.generated.h"

class ULoopedAbilitySystemComponent;
class UStaticMeshComponent;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDied, AEnemyBase*, Enemy);

UCLASS(Blueprintable)
class LOOPED_API AEnemyBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnEnemyDied OnEnemyDied;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void TakeDamageFromPlayer(float Damage, AActor* DamageSource);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Enemy")
	bool IsAlive() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "LOOPED|Enemy")
	void BP_OnDied();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void Respawn();

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyBurnEffect(float DamagePerTick = 10.0f, int32 NumTicks = 3);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyVenomEffect(float DamagePerTick = 5.0f, int32 NumTicks = 5, float SlowMultiplier = 0.4f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyChainSparkEffect(float ChainDamage = 10.0f, float ChainRadius = 500.0f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Enemy")
	void ApplyLifestealEffect(float HealAmount = 5.0f);

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULoopedAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float POCHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	FLinearColor EnemyColor = FLinearColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MoveSpeed = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MeleeDamage = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float MeleeHitCooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	bool bIsRanged = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedDamage = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedFireRate = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float RangedRange = 1500.0f;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float CurrentHealth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	float MaxHealthCached = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> HPBarWidget;

private:
	void Die();
	void BurnTick();
	void VenomTick();

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMaterial;

	FTimerHandle BurnTimerHandle;
	float BurnDamagePerTick = 0.0f;
	int32 BurnTicksRemaining = 0;

	FTimerHandle MeleeTimerHandle;
	FTimerHandle RangedTimerHandle;
	bool bCanMeleeHit = true;
	void MeleeCooldownReset();
	void RangedAttack();

	FTimerHandle VenomTimerHandle;
	float VenomDamagePerTick = 0.0f;
	int32 VenomTicksRemaining = 0;
	float VenomSlowMultiplier = 1.0f;
	float BaseSpeed = 0.0f;
};
