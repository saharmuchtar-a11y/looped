#include "EnemyBase.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "Looped.h"
#include "Core/LoopedRunGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Player/LoopedCharacter.h"
#include "Components/WidgetComponent.h"
#include "Components/ProgressBar.h"
#include "Blueprint/UserWidget.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;

	AbilitySystemComponent = CreateDefaultSubobject<ULoopedAbilitySystemComponent>(TEXT("AbilitySystem"));

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(GetRootComponent());
	VisualMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.8f));
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetHiddenInGame(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	if (CubeMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeMesh.Object);
	}

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->SetCapsuleHalfHeight(96.0f);
	GetCapsuleComponent()->SetCapsuleRadius(42.0f);

	HPBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HPBar"));
	HPBarWidget->SetupAttachment(GetRootComponent());
	HPBarWidget->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	HPBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
	HPBarWidget->SetDrawSize(FVector2D(80.0f, 8.0f));

	static ConstructorHelpers::FClassFinder<UUserWidget> HPWidgetClass(TEXT("/Game/UI/WBP_EnemyHP"));
	if (HPWidgetClass.Succeeded())
	{
		HPBarWidget->SetWidgetClass(HPWidgetClass.Class);
	}
}

void AEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	AbilitySystemComponent->InitializeAttributes();

	CurrentHealth = POCHealth;
	MaxHealthCached = POCHealth;

	// Create dynamic material for color feedback (burn visual, health feedback)
	if (VisualMesh)
	{
		DynMaterial = VisualMesh->CreateDynamicMaterialInstance(0);
		if (DynMaterial)
		{
			DynMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
		}
		UE_LOG(LogLoopedAI, Display, TEXT("Enemy mesh: %s"), VisualMesh->GetStaticMesh() ? TEXT("loaded") : TEXT("MISSING"));
	}

	BaseSpeed = MoveSpeed;
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
	GetCharacterMovement()->JumpZVelocity = 500.0f;
	GetCharacterMovement()->AirControl = 0.3f;

	// Ranged enemies: taller, blue-ish, slower
	if (bIsRanged)
	{
		SetActorScale3D(FVector(0.7f, 0.7f, 1.4f));
		EnemyColor = FLinearColor(0.2f, 0.3f, 0.8f, 1.0f);
		if (DynMaterial)
		{
			DynMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
		}
		MoveSpeed = 40.0f;
		BaseSpeed = MoveSpeed;
		GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
	}

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy spawned: HP=%.0f %s"), CurrentHealth, bIsRanged ? TEXT("(RANGED)") : TEXT("(MELEE)"));
}

void AEnemyBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsAlive()) return;

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Player = PC->GetPawn())
		{
			FVector PlayerLoc = Player->GetActorLocation();
			FVector MyLoc = GetActorLocation();
			FVector Dir = (PlayerLoc - MyLoc).GetSafeNormal2D();
			float Dist = FVector::Dist2D(PlayerLoc, MyLoc);

			if (Dist > 100.0f)
			{
				AddMovementInput(Dir, 1.0f);
			}

			// Jump if player is significantly above us
			float HeightDiff = PlayerLoc.Z - MyLoc.Z;
			if (HeightDiff > 80.0f && GetCharacterMovement()->IsMovingOnGround())
			{
				LaunchCharacter(FVector(0.0f, 0.0f, 500.0f), false, true);
			}

			FRotator LookRot = Dir.Rotation();
			SetActorRotation(FRotator(0.0f, LookRot.Yaw, 0.0f));

			// Melee: damage player on close contact
			if (!bIsRanged && Dist < 120.0f && bCanMeleeHit)
			{
				if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
				{
					LC->TakeDamageFromEnemy(MeleeDamage);
					bCanMeleeHit = false;
					GetWorldTimerManager().SetTimer(MeleeTimerHandle, this, &AEnemyBase::MeleeCooldownReset, MeleeHitCooldown, false);
				}
			}

			// Ranged: shoot at player from distance
			if (bIsRanged && Dist < RangedRange && Dist > 200.0f && !GetWorldTimerManager().IsTimerActive(RangedTimerHandle))
			{
				RangedAttack();
				GetWorldTimerManager().SetTimer(RangedTimerHandle, this, &AEnemyBase::RangedAttack, RangedFireRate, true);
			}
			if (bIsRanged && (Dist >= RangedRange || !IsAlive()))
			{
				GetWorldTimerManager().ClearTimer(RangedTimerHandle);
			}
		}
	}
}

UAbilitySystemComponent* AEnemyBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AEnemyBase::TakeDamageFromPlayer(float Damage, AActor* DamageSource)
{
	if (!IsAlive()) return;

	CurrentHealth = FMath::Max(0.0f, CurrentHealth - Damage);

	// Update HP bar
	if (HPBarWidget)
	{
		if (UUserWidget* Widget = HPBarWidget->GetWidget())
		{
			if (UProgressBar* Bar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("HPBar"))))
			{
				Bar->SetPercent(GetHealthPercent());
			}
		}
	}

	UE_LOG(LogLoopedCombat, Display, TEXT("Enemy took %.1f damage (HP: %.0f/%.0f)"),
		Damage, CurrentHealth, MaxHealthCached);

	if (CurrentHealth <= 0.0f)
	{
		Die();
	}
}

float AEnemyBase::GetHealthPercent() const
{
	return (MaxHealthCached > 0.0f) ? CurrentHealth / MaxHealthCached : 0.0f;
}

bool AEnemyBase::IsAlive() const
{
	return CurrentHealth > 0.0f;
}

void AEnemyBase::Die()
{
	UE_LOG(LogLoopedAI, Display, TEXT("Enemy KILLED!"));
	OnEnemyDied.Broadcast(this);
	BP_OnDied();

	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	if (HPBarWidget) HPBarWidget->SetVisibility(false);
	GetWorldTimerManager().ClearTimer(RangedTimerHandle);
	GetWorldTimerManager().ClearTimer(MeleeTimerHandle);
	GetCharacterMovement()->DisableMovement();

	if (ALoopedRunGameMode* GM = Cast<ALoopedRunGameMode>(UGameplayStatics::GetGameMode(this)))
	{
		GM->NotifyAllEnemiesDefeated();
	}
}

void AEnemyBase::Respawn()
{
	GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	GetWorldTimerManager().ClearTimer(VenomTimerHandle);
	BurnTicksRemaining = 0;
	VenomTicksRemaining = 0;
	MoveSpeed = BaseSpeed;
	GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
	CurrentHealth = POCHealth;
	MaxHealthCached = POCHealth;
	SetActorScale3D(FVector(1.0f, 1.0f, 1.0f));
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (HPBarWidget) HPBarWidget->SetVisibility(true);
	GetCharacterMovement()->SetMovementMode(EMovementMode::MOVE_Walking);

	// Reset HP bar to full
	if (HPBarWidget)
	{
		if (UUserWidget* Widget = HPBarWidget->GetWidget())
		{
			if (UProgressBar* Bar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("HPBar"))))
			{
				Bar->SetPercent(1.0f);
			}
		}
	}

	// Reset color to default enemy color
	if (DynMaterial)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), EnemyColor);
	}

	UE_LOG(LogLoopedAI, Display, TEXT("Enemy Respawned: HP=%.0f"), CurrentHealth);
}

void AEnemyBase::ApplyBurnEffect(float DamagePerTick, int32 NumTicks)
{
	if (!IsAlive()) return;

	// Refresh burn — clear any existing and restart
	GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	BurnDamagePerTick = DamagePerTick;
	BurnTicksRemaining = NumTicks;

	// Visual: turn orange to show burning
	if (DynMaterial)
	{
		DynMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.35f, 0.0f, 1.0f));
	}

	GetWorldTimerManager().SetTimer(BurnTimerHandle, this, &AEnemyBase::BurnTick, 1.0f, true);
	UE_LOG(LogLoopedAI, Display, TEXT("Burn applied: %.0f x %d ticks"), DamagePerTick, NumTicks);
}

void AEnemyBase::BurnTick()
{
	if (!IsAlive() || BurnTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(BurnTimerHandle);
		return;
	}

	BurnTicksRemaining--;
	TakeDamageFromPlayer(BurnDamagePerTick, this);
	UE_LOG(LogLoopedAI, Display, TEXT("Burn tick: %.0f dmg, %d remaining"), BurnDamagePerTick, BurnTicksRemaining);

	if (BurnTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(BurnTimerHandle);
	}
}

void AEnemyBase::ApplyVenomEffect(float DamagePerTick, int32 NumTicks, float SlowMultiplier)
{
	if (!IsAlive()) return;

	GetWorldTimerManager().ClearTimer(VenomTimerHandle);
	VenomDamagePerTick = DamagePerTick;
	VenomTicksRemaining = NumTicks;
	VenomSlowMultiplier = SlowMultiplier;

	MoveSpeed = BaseSpeed * VenomSlowMultiplier;
	GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;

	GetWorldTimerManager().SetTimer(VenomTimerHandle, this, &AEnemyBase::VenomTick, 1.0f, true);
	UE_LOG(LogLoopedAI, Display, TEXT("Venom applied: %.0f x %d ticks, %.0f%% slow"), DamagePerTick, NumTicks, (1.0f - SlowMultiplier) * 100.0f);
}

void AEnemyBase::VenomTick()
{
	if (!IsAlive() || VenomTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(VenomTimerHandle);
		MoveSpeed = BaseSpeed;
		GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
		return;
	}

	VenomTicksRemaining--;
	TakeDamageFromPlayer(VenomDamagePerTick, this);

	if (VenomTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(VenomTimerHandle);
		MoveSpeed = BaseSpeed;
		GetCharacterMovement()->MaxWalkSpeed = BaseSpeed;
	}
}

void AEnemyBase::MeleeCooldownReset()
{
	bCanMeleeHit = true;
}

void AEnemyBase::RangedAttack()
{
	if (!IsAlive()) return;

	APawn* Player = GetWorld()->GetFirstPlayerController()->GetPawn();
	if (!Player) return;

	if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(Player))
	{
		LC->TakeDamageFromEnemy(RangedDamage);
		UE_LOG(LogLoopedAI, Display, TEXT("Ranged enemy hit player for %.0f"), RangedDamage);
	}
}

void AEnemyBase::ApplyChainSparkEffect(float ChainDamage, float ChainRadius)
{
	if (!IsAlive()) return;

	FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Other = *It;
		if (Other == this || !Other->IsAlive()) continue;

		float Dist = FVector::Dist(MyLoc, Other->GetActorLocation());
		if (Dist <= ChainRadius)
		{
			Other->TakeDamageFromPlayer(ChainDamage, this);
			UE_LOG(LogLoopedAI, Display, TEXT("Chain spark hit nearby enemy for %.0f dmg (dist: %.0f)"), ChainDamage, Dist);
		}
	}
}

void AEnemyBase::ApplyLifestealEffect(float HealAmount)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ALoopedCharacter* LC = Cast<ALoopedCharacter>(PC->GetPawn()))
		{
			LC->HealPlayer(HealAmount);
			UE_LOG(LogLoopedAI, Display, TEXT("Lifesteal healed player for %.0f"), HealAmount);
		}
	}
}
