#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "InputActionValue.h"
#include "LoopedCharacter.generated.h"

class ULoopedAbilitySystemComponent;
class ULoopedAttributeSet;
class UPassiveStackComponent;
class UWeaponHolderComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerDied);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerHealthChanged, float, NewHealthPercent);

UCLASS()
class LOOPED_API ALoopedCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ALoopedCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnPlayerDied OnPlayerDied;

	UPROPERTY(BlueprintAssignable, Category = "LOOPED|Events")
	FOnPlayerHealthChanged OnPlayerHealthChanged;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "LOOPED|Health")
	bool IsAlive() const;

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Health")
	void TakeDamageFromEnemy(float Damage);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Health")
	void HealPlayer(float Amount);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Health")
	float GetPOCHealthPercent() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float POCMaxHealth = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "POC")
	float POCCurrentHealth = 100.0f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULoopedAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPassiveStackComponent> PassiveStack;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWeaponHolderComponent> WeaponHolder;

	// Input
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction;

	// Movement params
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float BaseWalkSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeedMultiplier = 1.43f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float JumpHeight = 150.0f;

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartFire(const FInputActionValue& Value);
	void StopFire(const FInputActionValue& Value);
	void StartSprint(const FInputActionValue& Value);
	void StopSprint(const FInputActionValue& Value);
	void StartCrouch(const FInputActionValue& Value);
	void StopCrouch(const FInputActionValue& Value);

	void HandleHealthChanged(float NewHealth, float MaxHealth);

	UPROPERTY()
	TObjectPtr<ULoopedAttributeSet> AttributeSet;

	bool bIsSprinting = false;
};
