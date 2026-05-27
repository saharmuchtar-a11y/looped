#include "LoopedCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "GAS/LoopedAttributeSet.h"
#include "Player/Components/PassiveStackComponent.h"
#include "Player/Components/WeaponHolderComponent.h"
#include "Looped.h"

ALoopedCharacter::ALoopedCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetRootComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 60.0f));
	FirstPersonCamera->bUsePawnControlRotation = true;

	AbilitySystemComponent = CreateDefaultSubobject<ULoopedAbilitySystemComponent>(TEXT("AbilitySystem"));
	PassiveStack = CreateDefaultSubobject<UPassiveStackComponent>(TEXT("PassiveStack"));
	WeaponHolder = CreateDefaultSubobject<UWeaponHolderComponent>(TEXT("WeaponHolder"));

	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
	GetCharacterMovement()->JumpZVelocity = FMath::Sqrt(2.0f * 980.0f * JumpHeight);
	GetCharacterMovement()->AirControl = 0.8f;
	GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
	GetCharacterMovement()->FallingLateralFriction = 0.0f;
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCharacterMovement()->CrouchedHalfHeight = 44.0f;
}

void ALoopedCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Set up Enhanced Input
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	// Initialize GAS
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	AbilitySystemComponent->InitializeAttributes();

	UE_LOG(LogLoopedCore, Display, TEXT("LoopedCharacter initialized"));
}

UAbilitySystemComponent* ALoopedCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

float ALoopedCharacter::GetHealthPercent() const
{
	if (const ULoopedAttributeSet* Attrs = AbilitySystemComponent->GetSet<ULoopedAttributeSet>())
	{
		return (Attrs->GetMaxHealth() > 0.0f) ? Attrs->GetHealth() / Attrs->GetMaxHealth() : 0.0f;
	}
	return 0.0f;
}

bool ALoopedCharacter::IsAlive() const
{
	if (const ULoopedAttributeSet* Attrs = AbilitySystemComponent->GetSet<ULoopedAttributeSet>())
	{
		return Attrs->GetHealth() > 0.0f;
	}
	return false;
}

void ALoopedCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

	if (MoveAction)
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ALoopedCharacter::Move);
	if (LookAction)
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ALoopedCharacter::Look);
	if (JumpAction)
	{
		EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
	if (FireAction)
	{
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &ALoopedCharacter::StartFire);
		EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &ALoopedCharacter::StopFire);
	}
	if (SprintAction)
	{
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &ALoopedCharacter::StartSprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &ALoopedCharacter::StopSprint);
	}
	if (CrouchAction)
	{
		EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &ALoopedCharacter::StartCrouch);
		EIC->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ALoopedCharacter::StopCrouch);
	}
}

void ALoopedCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	if (GetController())
	{
		const FRotator YawRotation(0.0f, GetControlRotation().Yaw, 0.0f);
		const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Forward, Input.Y);
		AddMovementInput(Right, Input.X);
	}
}

void ALoopedCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	AddControllerYawInput(Input.X);
	AddControllerPitchInput(-Input.Y);
}

void ALoopedCharacter::StartFire(const FInputActionValue& Value)
{
	if (WeaponHolder)
	{
		WeaponHolder->StartFiring();
	}
}

void ALoopedCharacter::StopFire(const FInputActionValue& Value)
{
	if (WeaponHolder)
	{
		WeaponHolder->StopFiring();
	}
}

void ALoopedCharacter::TakeDamageFromEnemy(float Damage)
{
	if (POCCurrentHealth <= 0.0f) return;
	POCCurrentHealth = FMath::Max(0.0f, POCCurrentHealth - Damage);
	OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	UE_LOG(LogLoopedCore, Display, TEXT("Player took %.0f damage (HP: %.0f/%.0f)"), Damage, POCCurrentHealth, POCMaxHealth);

	if (POCCurrentHealth <= 0.0f)
	{
		OnPlayerDied.Broadcast();
		UE_LOG(LogLoopedCore, Display, TEXT("Player DIED!"));
	}
}

void ALoopedCharacter::HealPlayer(float Amount)
{
	POCCurrentHealth = FMath::Min(POCMaxHealth, POCCurrentHealth + Amount);
	OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
}

float ALoopedCharacter::GetPOCHealthPercent() const
{
	return (POCMaxHealth > 0.0f) ? POCCurrentHealth / POCMaxHealth : 0.0f;
}

void ALoopedCharacter::StartSprint(const FInputActionValue& Value)
{
	bIsSprinting = true;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed * SprintSpeedMultiplier;
}

void ALoopedCharacter::StopSprint(const FInputActionValue& Value)
{
	bIsSprinting = false;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
}

void ALoopedCharacter::StartCrouch(const FInputActionValue& Value)
{
	Crouch();
}

void ALoopedCharacter::StopCrouch(const FInputActionValue& Value)
{
	UnCrouch();
}
