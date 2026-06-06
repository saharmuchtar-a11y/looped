#include "LoopedCharacter.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GAS/LoopedAbilitySystemComponent.h"
#include "GAS/LoopedAttributeSet.h"
#include "Player/Components/PassiveStackComponent.h"
#include "Player/Components/WeaponHolderComponent.h"
#include "Enemies/EnemyBase.h"
#include "EngineUtils.h"
#include "Core/LoopedGameInstance.h"
#include "Core/LoopedRunGameMode.h"
#include "Data/PassiveCardData.h"
#include "SloMo/SloMoManager.h"
#include "InputAction.h"
#include "Looped.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

ALoopedCharacter::ALoopedCharacter()
{
	// Tick is enabled so we can pin a persistent perk-level HUD via AddOnScreenDebugMessage
	// (cheap — no UMG widget needed for the prototype display).
	PrimaryActorTick.bCanEverTick = true;

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

	// Player-hurt SFX — loaded by path (plays when damage lands).
	static ConstructorHelpers::FObjectFinder<USoundBase> HurtSnd(TEXT("/Game/Audio/player_hurt.player_hurt"));
	if (HurtSnd.Succeeded()) PlayerHurtSound = HurtSnd.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> JumpSnd(TEXT("/Game/Audio/jump.jump"));
	if (JumpSnd.Succeeded()) JumpSound = JumpSnd.Object;

	// Death screen widget class — loaded from WBP_DeathScreen via path.
	static ConstructorHelpers::FClassFinder<UUserWidget> DeathScreenClassFinder(TEXT("/Game/UI/WBP_DeathScreen"));
	if (DeathScreenClassFinder.Succeeded())
	{
		DeathScreenClass = DeathScreenClassFinder.Class;
	}

	// Held-weapon mesh, attached to the hero hand socket. Starts empty/hidden — the WeaponHolder
	// drives which mesh shows from the equipped FWeaponData (data-driven; no baked-in component).
	WeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMeshComp->SetupAttachment(GetMesh(), WeaponAttachSocket);
	WeaponMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMeshComp->SetVisibility(false);
	WeaponMeshComp->SetCastShadow(false);

	// Pre-load Manny unarmed attack anims so PlayRandomAttackAnim has something to pick from.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk1(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk2(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_02"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Atk3(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_03"));
	if (Atk1.Succeeded()) AttackAnims.Add(Atk1.Object);
	if (Atk2.Succeeded()) AttackAnims.Add(Atk2.Object);
	if (Atk3.Succeeded()) AttackAnims.Add(Atk3.Object);

	// Manual bullet-time input — loaded here so no per-Blueprint assignment is needed
	// (mapped to MiddleMouseButton in IMC_Default).
	static ConstructorHelpers::FObjectFinder<UInputAction> SloMoActionFinder(TEXT("/Game/IA_SloMo"));
	if (SloMoActionFinder.Succeeded())
	{
		SloMoAction = SloMoActionFinder.Object;
	}

	// Arm-monitor dashboard — a viewport widget created on first open. Auto-link the class.
	static ConstructorHelpers::FClassFinder<UUserWidget> WristWidgetClass(TEXT("/Game/UI/WBP_WristScreen"));
	if (WristWidgetClass.Succeeded())
	{
		WristMenuClass = WristWidgetClass.Class;
	}

	// Secret sphere reward flavor — ties the Time Sphere (story anchor) + Gravity mastery (the
	// gate) + the Wing artifact (its lighter-gravity effect) + the 80-year loop together.
	SecretSphereMessage = FText::FromString(TEXT(
		"The Time Sphere answers your mastery of Gravity.\n"
		"A shard of the Void breaks loose and binds to your arm — the WING.\n"
		"After 80 years, the loop's pull finally feels lighter."));
}

void ALoopedCharacter::PlayRandomAttackAnim()
{
	if (AttackAnims.Num() == 0 || !GetMesh()) return;
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst) return;
	const int32 Pick = FMath::RandRange(0, AttackAnims.Num() - 1);
	if (UAnimSequence* Seq = AttackAnims[Pick])
	{
		AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, FName(TEXT("DefaultSlot")), 0.1f, 0.1f, 1.0f);
	}
}

void ALoopedCharacter::SetWeaponVisualMesh(UStaticMesh* NewMesh)
{
	if (!WeaponMeshComp) return;
	// Re-assert the socket attachment in case the socket name changed in defaults after construction.
	if (WeaponMeshComp->GetAttachSocketName() != WeaponAttachSocket)
	{
		WeaponMeshComp->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachSocket);
	}
	WeaponMeshComp->SetStaticMesh(NewMesh);
	WeaponMeshComp->SetVisibility(NewMesh != nullptr);
}

void ALoopedCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cache the camera's rest position so the positional shake can jitter around it + snap back.
	if (FirstPersonCamera)
	{
		BaseCameraRelLoc = FirstPersonCamera->GetRelativeLocation();
	}

	// Fade in from black on every level load. Paired with APortalActor's fade-out before travel,
	// this masks the one-time synchronous OpenLevel hitch so the transition reads as a smooth fade
	// instead of a freeze — no loading screen. StartCameraFade(1->0) snaps black, then clears.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.45f, FLinearColor::Black, false, false);
		}
	}

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

	// Mirror persistent perk levels from GameInstance onto this pawn (survives OpenLevel)
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		// Entering Hub == start of a new run. Wipe perks so the next run begins fresh.
		const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("");
		if (MapName.Contains(TEXT("L_Hub")))
		{
			GI->ResetRunState(); // new run — one wipe: deck + run state (health/shards/curses)
			GI->CurrentRunRoom = 0; // new run — room progress resets too
			// (DumpStatsToScreen removed — achievement/tracking stats stay saved, just not shown
			//  to the player. The function remains for dev/console use.)
			UE_LOG(LogLoopedCore, Display, TEXT("Hub entered — run state reset for new run."));
		}

		// If the player just died in another level, re-show the death screen here
		// for whatever time remains. Widget death is normal across OpenLevel — GI state isn't.
		ShowDeathScreenIfActive();

		// Merchant next-run boons: starting a run (any non-Hub level) consumes the pending
		// cards bought at the merchant — they're applied once as free starting perks, then
		// cleared. Perks were just reset on Hub entry, so this fires effectively at run start.
		if (!MapName.Contains(TEXT("L_Hub")))
		{
			const TArray<FName> Pending = GI->GetPendingNextRunCards();
			if (Pending.Num() > 0)
			{
				for (const FName& Card : Pending)
				{
					const int32 NewLevel = GI->IncrementPerkLevel(Card);
					GI->RecordPerkPicked(Card, NewLevel);
				}
				GI->ClearPendingNextRunCards();
				UE_LOG(LogLoopedCore, Display, TEXT("Applied %d merchant next-run card(s)."), Pending.Num());
			}
		}

	}

	// --- Secret chain link 3: cache this level's Time Spheres for touch detection ---
	// The hub's sphere is excluded by skipping the whole hub level (the secret only fires
	// in combat rooms). Spheres are placed actors, so they already exist at BeginPlay.
	// Identified by class name (BP_TimeSphere) since the BP isn't reparented to C++.
	SecretSpheres.Reset();
	const FString LevelName = GetWorld() ? GetWorld()->GetMapName() : TEXT("");
	bInHubLevel = LevelName.Contains(TEXT("L_Hub"));
	if (!bInHubLevel && GetWorld())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			AActor* A = *It;
			if (A && A->GetClass()->GetName().Contains(TEXT("TimeSphere")))
			{
				SecretSpheres.Add(A);
			}
		}
		UE_LOG(LogLoopedCore, Display, TEXT("[Secret] Cached %d Time Sphere(s) in '%s'"), SecretSpheres.Num(), *LevelName);
	}

	// Re-apply Speed/Gravity passives in case we just respawned in a new level (reads the deck,
	// which persists on the GameInstance — safe to re-run every level).
	ApplyMovementMods();

	// --- Persistent run health: 3-branch init (fixes room-travel amnesia without re-heal bugs) ---
	// bInHubLevel was set above. SyncHealthToRunState (write-through) keeps GI in lockstep after.
	ULoopedGameInstance* HealthGI = GetGameInstance<ULoopedGameInstance>();
	if (!HealthGI || bInHubLevel)
	{
		// Hub (or no GI): always full health. The deck is empty in the Hub, so this is base MaxHP.
		ApplyMaxHPMod();
		POCCurrentHealth = POCMaxHealth;
		OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	}
	else if (!HealthGI->IsRunHealthInitialized())
	{
		// First combat room of the run: derive MaxHP from the build, start at full, seed run state.
		ApplyMaxHPMod();
		POCCurrentHealth = POCMaxHealth;
		HealthGI->SetRunHealth(POCCurrentHealth, POCMaxHealth);
		OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	}
	else
	{
		// Subsequent rooms: restore the carried-over health (the amnesia fix). Straight assignment
		// from the persistent run state — no ApplyMaxHPMod, so no delta re-heal on room entry.
		POCMaxHealth = HealthGI->GetRunMaxHealth();
		POCCurrentHealth = HealthGI->GetRunHealth();
		OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	}

	// Show the persistent owned-artifacts list on the HUD (every level, incl. Hub).
	UpdateArtifactHUD();

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
	if (SloMoAction)
	{
		// Recycled: the Middle-Mouse press now toggles the arm monitor (which slows time itself),
		// replacing the old hold-to-bullet-time.
		EIC->BindAction(SloMoAction, ETriggerEvent::Started, this, &ALoopedCharacter::ToggleHologramMenu);
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
	// No attacking while the arm monitor is open — prevents free hits during menu slow-mo.
	if (bHologramOpen) return;

	if (WeaponHolder)
	{
		WeaponHolder->StartFiring();
	}
	PlayRandomAttackAnim();
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
	if (bHologramOpen) return; // invulnerable while the arm monitor is open (paused-menu feel)

	if (PlayerHurtSound)
	{
		UGameplayStatics::PlaySound2D(this, PlayerHurtSound);
	}
	AddCameraShake(1.2f); // small screen punch when you get hit (no-freeze damage feedback)

	// Curse "Frailty": you take more damage this run.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasCurse(TEXT("Frailty")))
		{
			Damage *= GI->CurseFrailtyDamageMult;
		}
	}

	const float OldHP = POCCurrentHealth;
	POCCurrentHealth = FMath::Max(0.0f, POCCurrentHealth - Damage);
	OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	SyncHealthToRunState();
	UE_LOG(LogLoopedCore, Display, TEXT("Player took %.0f damage (HP: %.0f/%.0f)"), Damage, POCCurrentHealth, POCMaxHealth);

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddDamageTaken(OldHP - POCCurrentHealth);
	}

	if (POCCurrentHealth <= 0.0f)
	{
		ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
		if (GI)
		{
			if (GetWorld() && GetWorld()->GetMapName().Contains(TEXT("L_FinalBoss")))
			{
				GI->AddBossDeath();
			}
			else
			{
				GI->AddPlayerDeath();
			}
			GI->StartDeathScreen();
		}
		UE_LOG(LogLoopedCore, Display, TEXT("Player DIED!"));
		ShowDeathScreenIfActive();
		// Death cam: ragdoll the body + pull the camera to a 3rd-person shot of the hero on the floor.
		EnterDeathCam();
		// Hold the shot, THEN travel. OnPlayerDied (which the BP uses to OpenLevel→Hub) is delayed by
		// DeathCamDuration so the "back to square one" moment actually reads. The death screen persists
		// into the Hub via the GameInstance flag for whatever time remains.
		GetWorldTimerManager().SetTimer(DeathTravelTimerHandle, this, &ALoopedCharacter::FinishDeathAndTravel, FMath::Max(0.1f, DeathCamDuration), false);
	}
}

void ALoopedCharacter::EnterDeathCam()
{
	// Freeze the player: no input, no movement.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		DisableInput(PC);
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}

	// Ragdoll the hero so the body crumples to the floor (same approach as enemies). Make sure the
	// body is visible to its owner (FP setups often hide it) and doesn't block via the capsule.
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->SetOwnerNoSee(false);
		M->SetCollisionProfileName(TEXT("Ragdoll"));
		M->SetSimulatePhysics(true);
		M->SetAllBodiesSimulatePhysics(true);
		M->WakeAllRigidBodies();
		M->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Detach the FP camera, lift it straight UP over the body and aim DOWN at it (top-down death shot).
	// Overhead can't clip the side walls; a small back offset gives a touch of angle, and a vertical
	// sweep drops the camera below any low ceiling so it never ends up inside geometry.
	if (FirstPersonCamera)
	{
		const FVector HeroLoc = GetActorLocation();
		const FVector Pivot   = HeroLoc + FVector(0.0f, 0.0f, 40.0f);
		// High 3/4 angle: well up AND pulled back so the shot has elevation + perspective (not a flat
		// straight-down "just the floor" view). The sweep keeps it out of walls/ceilings.
		FVector CamLoc = HeroLoc + (-GetActorForwardVector()) * 300.0f + FVector(0.0f, 0.0f, 430.0f);

		FHitResult Hit;
		FCollisionQueryParams Params(FName(TEXT("DeathCam")), false, this);
		Params.AddIgnoredActor(this);
		if (GetWorld()->SweepSingleByChannel(Hit, Pivot, CamLoc, FQuat::Identity,
			ECC_WorldStatic, FCollisionShape::MakeSphere(22.0f), Params))
		{
			CamLoc = Hit.Location - (CamLoc - Pivot).GetSafeNormal() * 20.0f;
		}

		FirstPersonCamera->bUsePawnControlRotation = false;
		FirstPersonCamera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

		// Glide from the current first-person view to the death shot (Tick interpolates) — no snap.
		DeathCamStartLoc = FirstPersonCamera->GetComponentLocation();
		DeathCamStartRot = FirstPersonCamera->GetComponentRotation();
		DeathCamTargetLoc = CamLoc;
		DeathCamTargetRot = (HeroLoc - CamLoc).Rotation();
		DeathCamMoveElapsed = 0.0f;
		bDeathCamMoving = true;
		bInDeathCam = true;
	}
}

void ALoopedCharacter::FinishDeathAndTravel()
{
	// Notify listeners (stats, etc.), then travel. The BP's HandleDeath no longer OpenLevels on death
	// (that immediate travel was removed so the death cam can hold) — C++ owns the delayed travel now.
	OnPlayerDied.Broadcast();
	UGameplayStatics::OpenLevel(this, FName(TEXT("L_Hub")));
}

void ALoopedCharacter::ShowDeathScreenIfActive()
{
	if (DeathScreenWidget) return;
	if (!DeathScreenClass) return;

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI || !GI->IsDeathScreenActive()) return;

	const float Remaining = GI->GetDeathScreenRemainingSeconds();
	if (Remaining <= 0.0f)
	{
		GI->EndDeathScreen();
		return;
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	DeathScreenWidget = CreateWidget<UUserWidget>(PC, DeathScreenClass);
	if (!DeathScreenWidget) return;

	DeathScreenWidget->AddToViewport(1000);
	if (UWidget* DeathText = DeathScreenWidget->GetWidgetFromName(TEXT("DeathText")))
	{
		if (UCanvasPanelSlot* CPS = Cast<UCanvasPanelSlot>(DeathText->Slot))
		{
			CPS->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CPS->SetAlignment(FVector2D(0.5f, 0.5f));
			CPS->SetSize(FVector2D(1200.0f, 200.0f));
			CPS->SetPosition(FVector2D(0.0f, 0.0f));
		}
	}
	GetWorldTimerManager().SetTimer(DeathTimerHandle, this, &ALoopedCharacter::HandleDeathTimerExpired, Remaining, false);
	UE_LOG(LogLoopedCore, Display, TEXT("Death widget shown for %.2fs."), Remaining);
}

void ALoopedCharacter::HandleDeathTimerExpired()
{
	if (DeathScreenWidget)
	{
		DeathScreenWidget->RemoveFromParent();
		DeathScreenWidget = nullptr;
	}
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->EndDeathScreen();
	}
}

void ALoopedCharacter::HealPlayer(float Amount)
{
	// Curse "Bloodless": healing is suppressed this run.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		if (GI->HasCurse(TEXT("Bloodless")))
		{
			Amount *= GI->CurseBloodlessHealMult;
		}
	}
	if (Amount <= 0.0f) return;
	POCCurrentHealth = FMath::Min(POCMaxHealth, POCCurrentHealth + Amount);
	OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	SyncHealthToRunState();
}

float ALoopedCharacter::GetPOCHealthPercent() const
{
	return (POCMaxHealth > 0.0f) ? POCCurrentHealth / POCMaxHealth : 0.0f;
}

FText ALoopedCharacter::GetHealthText() const
{
	return FText::FromString(FString::Printf(TEXT("%d / %d"),
		FMath::RoundToInt(POCCurrentHealth), FMath::RoundToInt(POCMaxHealth)));
}

void ALoopedCharacter::SyncHealthToRunState()
{
	// Write-through: keep the GameInstance's persistent run health in lockstep with the pawn's
	// live values so it carries across hard OpenLevel travel. Skipped in the Hub — there's no
	// active run there, and writing would prematurely mark the run "health-initialized" before
	// the first combat room seeds it (which is how a fresh run starts at full HP).
	if (bInHubLevel) return;
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->SetRunHealth(POCCurrentHealth, POCMaxHealth);
	}
}

void ALoopedCharacter::StartSprint(const FInputActionValue& Value)
{
	bIsSprinting = true;
	ApplyMovementMods();
}

void ALoopedCharacter::StopSprint(const FInputActionValue& Value)
{
	bIsSprinting = false;
	ApplyMovementMods();
}

void ALoopedCharacter::StartCrouch(const FInputActionValue& Value)
{
	Crouch();
}

void ALoopedCharacter::StopCrouch(const FInputActionValue& Value)
{
	UnCrouch();
}

void ALoopedCharacter::ToggleHologramMenu()
{
	if (bHologramOpen) { CloseHologram(); }
	else               { OpenHologram(/*bRewardMode*/ false); }
}

void ALoopedCharacter::OpenHologramForReward()
{
	OpenHologram(/*bRewardMode*/ true);
}

void ALoopedCharacter::MountCardWidgetInMonitor(UUserWidget* CardWidget)
{
	if (!CardWidget) return;

	// Make sure the monitor is up in reward mode (this also shows CenterPanel).
	if (!bHologramOpen)
	{
		OpenHologram(/*bRewardMode*/ true);
	}
	if (!WristMenuWidget) return;

	if (UWidget* Center = WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel")))
	{
		Center->SetVisibility(ESlateVisibility::Visible);
	}
	// Hide the dummy placeholder so only the real cards show.
	if (UWidget* Placeholder = WristMenuWidget->GetWidgetFromName(TEXT("CenterPlaceholder")))
	{
		Placeholder->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Drop the card widget into the CenterPanel (a VerticalBox) and let it fill the slot so the
	// card row sits centered inside the monitor and scales with the layout.
	if (UPanelWidget* Center = Cast<UPanelWidget>(WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel"))))
	{
		if (UPanelSlot* Added = Center->AddChild(CardWidget))
		{
			if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Added))
			{
				FSlateChildSize FillSize;
				FillSize.SizeRule = ESlateSizeRule::Fill;
				FillSize.Value = 1.0f;
				VSlot->SetSize(FillSize);
				VSlot->SetHorizontalAlignment(HAlign_Fill);
				VSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
	}

	RefreshDashboard();
}

void ALoopedCharacter::OpenHologram(bool bRewardMode)
{
	bHologramOpen = true;

	// Slow time exactly the way card rewards / the old bullet-time already do.
	if (UWorld* World = GetWorld())
	{
		if (USloMoManager* SloMo = World->GetSubsystem<USloMoManager>())
		{
			SloMo->RequestSloMo(ESloMoTrigger::ActiveAbility);
		}
	}

	APlayerController* PC = Cast<APlayerController>(GetController());

	// Create the menu once, then add it to the viewport (standard full-screen menu path).
	if (!WristMenuWidget && WristMenuClass && PC)
	{
		WristMenuWidget = CreateWidget<UUserWidget>(PC, WristMenuClass);
	}
	if (WristMenuWidget && !WristMenuWidget->IsInViewport())
	{
		WristMenuWidget->AddToViewport(200); // above the HUD
	}

	// The Center (card-draft) panel is only shown in State 2 (Reward Draft).
	if (WristMenuWidget)
	{
		if (UWidget* Center = WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel")))
		{
			Center->SetVisibility(bRewardMode ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}

	// Free the cursor for menu interaction.
	if (PC)
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		PC->SetInputMode(Mode);
	}

	RefreshDashboard();
}

void ALoopedCharacter::CloseHologram()
{
	bHologramOpen = false;

	if (UWorld* World = GetWorld())
	{
		if (USloMoManager* SloMo = World->GetSubsystem<USloMoManager>())
		{
			SloMo->ReleaseSloMo(ESloMoTrigger::ActiveAbility);
		}
	}

	if (WristMenuWidget && WristMenuWidget->IsInViewport())
	{
		WristMenuWidget->RemoveFromParent();
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
	}
}

void ALoopedCharacter::RefreshDashboard()
{
	UUserWidget* W = WristMenuWidget;
	if (!W) return;
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	auto SetText = [W](const TCHAR* BlockName, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(W->GetWidgetFromName(BlockName)))
		{
			T->SetText(FText::FromString(S));
		}
	};

	// Top: currencies
	SetText(TEXT("ShardsText"), FString::Printf(TEXT("Shards: %d"), GI->GetShards()));
	SetText(TEXT("EchoesText"), FString::Printf(TEXT("Echoes: %d"), GI->GetEchoes()));

	// Bottom-right: permanent artifacts (moved into the monitor; the old floating HUD is retired).
	const FString Artifacts = GI->GetOwnedArtifactsLabel().ToString();
	SetText(TEXT("ArtifactsText"), Artifacts.IsEmpty() ? FString(TEXT("(none)")) : Artifacts);

	FString Perks;
	for (const FPassiveSlot& Slot : GI->RunDeck)
	{
		if (Slot.IsEmpty()) continue;
		Perks += FString::Printf(TEXT("%s  Lv %d\n"), *Slot.CardRowName.ToString(), Slot.Level);
	}
	SetText(TEXT("PerksText"), Perks.IsEmpty() ? FString(TEXT("(no cards yet)")) : Perks);

	// Right: relics + curses (names for now; icon wrap-boxes are a polish follow-up)
	FString Relics;
	for (const FName& Id : GI->GetRunArtifacts())
	{
		const FArtifactData* R = GI->FindArtifactRow(Id);
		Relics += FString::Printf(TEXT("%s\n"), R ? *R->DisplayName.ToString() : *Id.ToString());
	}
	SetText(TEXT("RelicsText"), Relics.IsEmpty() ? FString(TEXT("(none)")) : Relics);

	FString Curses;
	for (const FName& C : GI->GetActiveCurses())
	{
		Curses += FString::Printf(TEXT("%s\n"), *C.ToString());
	}
	SetText(TEXT("CursesText"), Curses.IsEmpty() ? FString(TEXT("(none)")) : Curses);
}

void ALoopedCharacter::AddCameraShake(float Intensity)
{
	// Take the max so overlapping shakes don't stack into a violent jolt — keeps it tight + crisp.
	CameraShakeAmount = FMath::Max(CameraShakeAmount, Intensity);
}

void ALoopedCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (JumpSound)
	{
		UGameplayStatics::PlaySound2D(this, JumpSound);
	}
}

void ALoopedCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Death cam: glide the (detached) camera from the FP view up/away to the overhead death shot,
	// then hold. While dead we skip the rest of Tick (shake/decay/etc) so nothing disturbs the shot.
	if (bInDeathCam)
	{
		if (bDeathCamMoving && FirstPersonCamera)
		{
			DeathCamMoveElapsed += DeltaSeconds;
			const float A = FMath::Clamp(DeathCamMoveElapsed / FMath::Max(0.01f, DeathCamMoveDuration), 0.0f, 1.0f);
			const float E = A * A * (3.0f - 2.0f * A); // smoothstep ease in/out
			const FVector L = FMath::Lerp(DeathCamStartLoc, DeathCamTargetLoc, E);
			const FQuat Q = FQuat::Slerp(DeathCamStartRot.Quaternion(), DeathCamTargetRot.Quaternion(), E);
			FirstPersonCamera->SetWorldLocationAndRotation(L, Q);
			if (A >= 1.0f) bDeathCamMoving = false;
		}
		return;
	}

	// Positional camera shake (no-freeze impact punch). Jitter the camera's relative location and
	// decay the amount; snap back to base when spent. Uses unscaled delta so it's unaffected by slow-mo.
	if (FirstPersonCamera)
	{
		if (CameraShakeAmount > KINDA_SMALL_NUMBER)
		{
			const float Mag = CameraShakeAmount * 4.0f; // units of jitter per unit intensity
			const FVector Jitter(
				FMath::FRandRange(-Mag, Mag),
				FMath::FRandRange(-Mag, Mag),
				FMath::FRandRange(-Mag, Mag));
			FirstPersonCamera->SetRelativeLocation(BaseCameraRelLoc + Jitter);
			CameraShakeAmount = FMath::FInterpTo(CameraShakeAmount, 0.0f, DeltaSeconds, 12.0f);
		}
		else if (!FirstPersonCamera->GetRelativeLocation().Equals(BaseCameraRelLoc, 0.01f))
		{
			FirstPersonCamera->SetRelativeLocation(BaseCameraRelLoc);
			CameraShakeAmount = 0.0f;
		}
	}

	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->AddPlaytime(DeltaSeconds);
	}

	// Secret chain link 3: while the gate is open, touching a room's Time Sphere grants an artifact.
	CheckSecretSpheres(DeltaSeconds);

	// Curse "Decay": bleed HP over the run. Accumulate and apply in ~1 HP chunks via the
	// normal damage path (handles death/stats). Curses are cleared in the Hub, so this only
	// ticks during a run.
	if (POCCurrentHealth > 0.0f)
	{
		if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasCurse(TEXT("Decay")))
			{
				DecayAccum += GI->CurseDecayPerSecond * DeltaSeconds;
				if (DecayAccum >= 1.0f)
				{
					const float Chunk = FMath::FloorToFloat(DecayAccum);
					DecayAccum -= Chunk;
					TakeDamageFromEnemy(Chunk);
				}
			}
		}
	}

	// Top-left "TARGET / Enemy X%" debug overlay removed for a clean screen — enemy HP now reads
	// only from the world-space WBP_EnemyHP bar above each enemy. (Old build-stats overlay already
	// moved to the arm monitor.) Function kept minimal; nothing pinned to the screen here anymore.
}

// --- Perk system ---

// Per-level tuning + caps moved to the FPassiveCardData DataTable (data consolidation).
// Effects read their numbers from the equipped card's row (via GameInstance RunDeck).

// Effective level under the "Brittle" curse (one tier weaker, min 1).
static int32 BrittleLevel(const ULoopedGameInstance* GI, int32 Level)
{
	return (GI && GI->HasCurse(TEXT("Brittle"))) ? FMath::Max(1, Level - 1) : Level;
}

// Fetch the per-level tuning row for an equipped card at its (Brittle-adjusted) level. Null if not equipped / no data.
static const FPassiveCardLevel* GetLevelData(const ULoopedGameInstance* GI, FName CardId)
{
	if (!GI) return nullptr;
	const int32 Level = GI->GetCardLevel(CardId);
	if (Level <= 0) return nullptr;
	const FPassiveCardData* Row = GI->FindCardRow(CardId);
	if (!Row) return nullptr;
	const int32 Eff = BrittleLevel(GI, Level);
	return Row->Levels.IsValidIndex(Eff - 1) ? &Row->Levels[Eff - 1] : nullptr;
}

int32 ALoopedCharacter::IncrementPerkLevel(FName PerkName)
{
	UE_LOG(LogLoopedCore, Display, TEXT("[Player] IncrementPerkLevel('%s') called"), *PerkName.ToString());

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[Player] GameInstance is NOT ULoopedGameInstance — perk lost. Check Project Settings."));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Red, TEXT("Perk LOST — GameInstance wrong class"));
		}
		return 0;
	}

	const int32 NewLevel = GI->IncrementPerkLevel(PerkName);
	GI->RecordPerkPicked(PerkName, NewLevel);

	// Dev visibility kept in the log only — no on-screen print (keeps the player's screen pristine).
	UE_LOG(LogLoopedCore, Verbose, TEXT("[Player] Picked: %s Lv %d"), *PerkName.ToString(), NewLevel);

	// Movement perks apply passively
	if (PerkName == TEXT("Speed") || PerkName == TEXT("Gravity"))
	{
		ApplyMovementMods();
	}
	if (PerkName == TEXT("MaxHP"))
	{
		ApplyMaxHPMod();
	}
	return NewLevel;
}

void ALoopedCharacter::ApplyMaxHPMod()
{
	const float OldMax = POCMaxHealth;
	// Base 100 + the equipped MaxHP card's row value (per level) + permanent vault Max-HP upgrade.
	float Bonus = 0.0f;
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (GI)
	{
		if (const FPassiveCardLevel* Lv = GetLevelData(GI, TEXT("MaxHP"))) Bonus += Lv->FlatMaxHP;
		Bonus += (float)GI->GetPermanentBonusMaxHP();
		Bonus += GI->GetArtifactFlatMaxHP(); // run relics (Bloodstone +25)
	}
	POCMaxHealth = 100.0f + Bonus;
	const float Delta = POCMaxHealth - OldMax;
	if (Delta > 0.0f)
	{
		// Healing on level-up so the pick feels rewarding instead of just a stat number.
		POCCurrentHealth = FMath::Min(POCCurrentHealth + Delta, POCMaxHealth);
	}
	else
	{
		POCCurrentHealth = FMath::Min(POCCurrentHealth, POCMaxHealth);
	}
	OnPlayerHealthChanged.Broadcast(GetPOCHealthPercent());
	SyncHealthToRunState();
	UE_LOG(LogLoopedCore, Display, TEXT("[Player] MaxHP applied — MaxHP %.0f (cur %.0f)"), POCMaxHealth, POCCurrentHealth);
}

int32 ALoopedCharacter::GetPerkLevel(FName PerkName) const
{
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		return GI->GetPerkLevel(PerkName);
	}
	return 0;
}

bool ALoopedCharacter::IsPerkAtMax(FName PerkName) const
{
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		return GI->IsPerkAtMax(PerkName);
	}
	return false;
}

TArray<FName> ALoopedCharacter::GetEligibleCards(const TArray<FName>& InAll) const
{
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		return GI->GetEligibleCards(InAll);
	}
	return InAll;
}

void ALoopedCharacter::ResetPerks()
{
	if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		GI->ResetPerks(); // clears RunDeck (the single source of truth)
	}
	ApplyMovementMods();
	ApplyMaxHPMod();
}

void ALoopedCharacter::ApplyEquippedEffectsTo(AEnemyBase* Target)
{
	if (!Target || !Target->IsAlive()) return;

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	// Iterate the equipped deck (single source of truth) and dispatch each on-hit effect by its
	// EffectTag, reading magnitudes from the card's per-level row data.
	const FGameplayTag TagBurn      = FGameplayTag::RequestGameplayTag(FName("Effect.Burn"), false);
	const FGameplayTag TagVenom     = FGameplayTag::RequestGameplayTag(FName("Effect.Venom"), false);
	const FGameplayTag TagLifesteal = FGameplayTag::RequestGameplayTag(FName("Effect.Lifesteal"), false);

	for (const FPassiveSlot& Slot : GI->RunDeck)
	{
		if (Slot.IsEmpty() || Slot.Level <= 0) continue;
		const FPassiveCardLevel* Lv = GetLevelData(GI, Slot.CardRowName);
		if (!Lv) continue;
		const FGameplayTagContainer& CardTags = Slot.CachedData.EffectTags;

		if (CardTags.HasTagExact(TagBurn))           Target->ApplyBurnEffect(Lv->Damage, Lv->Ticks);
		else if (CardTags.HasTagExact(TagVenom))     Target->ApplyVenomEffect(Lv->Damage, Lv->Ticks, Lv->SlowMultiplier);
		else if (CardTags.HasTagExact(TagLifesteal)) HealPlayer(Lv->HealAmount);
		// Speed/Gravity/MaxHP are passive (movement/HP). ChainSpark is handled in OnPlayerHitEnemy.
	}
}

void ALoopedCharacter::OnPlayerHitEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->IsAlive()) return;

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	// Primary hit: apply all equipped on-hit effects to the directly-hit enemy.
	ApplyEquippedEffectsTo(Enemy);

	// ChainSpark: damage every nearby enemy AND propagate equipped effects to them.
	// Values come from the equipped ChainSpark card's row. No re-chain on chained targets.
	const FPassiveCardLevel* SparkLv = GetLevelData(GI, TEXT("ChainSpark"));
	if (SparkLv)
	{
		const float ChainDamage = SparkLv->Damage;
		const float ChainRadius = SparkLv->Radius;
		const FVector PrimaryLoc = Enemy->GetActorLocation();

		for (TActorIterator<AEnemyBase> It(Enemy->GetWorld()); It; ++It)
		{
			AEnemyBase* Other = *It;
			if (!Other || Other == Enemy || !Other->IsAlive()) continue;
			if (FVector::Dist(PrimaryLoc, Other->GetActorLocation()) <= ChainRadius)
			{
				Other->TakeDamageFromPlayer(ChainDamage, this);
				ApplyEquippedEffectsTo(Other);
			}
		}
	}
}

void ALoopedCharacter::UpdateArtifactHUD()
{
	// Artifacts now live in the wrist monitor (ARTIFACTS corner), not a separate always-on HUD.
	// Just refresh the monitor if it's currently open; otherwise it'll pick up the latest on next open.
	if (bHologramOpen)
	{
		RefreshDashboard();
	}
}

void ALoopedCharacter::CheckSecretSpheres(float DeltaSeconds)
{
	// Cheap early-outs first so this is effectively free once the secret is spent or unavailable.
	if (bInHubLevel || SecretSpheres.Num() == 0) return;

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	// One-time: once the artifact is owned, the secret is spent (GrantArtifact persists it).
	if (GI->HasArtifact(SecretSphereArtifact)) return;

	// Link 2 gate: the sphere only "opens" after Gravity has ever been maxed. Until then,
	// brushing the sphere does nothing (keeps the secret hidden for normal play).
	if (!GI->HasPerkEverMaxed(TEXT("Gravity"))) return;

	// Throttle proximity polling to ~10 Hz — no need to scan every frame.
	SecretSphereCheckAccum += DeltaSeconds;
	if (SecretSphereCheckAccum < 0.1f) return;
	SecretSphereCheckAccum = 0.0f;

	const FVector P = GetActorLocation();
	const float R2 = SecretSphereTouchRadius * SecretSphereTouchRadius;
	for (const TObjectPtr<AActor>& S : SecretSpheres)
	{
		if (!S) continue;
		if (FVector::DistSquared(P, S->GetActorLocation()) <= R2)
		{
			UE_LOG(LogLoopedCore, Display, TEXT("[Secret] TP-Sphere touched — granting artifact '%s'"),
				*SecretSphereArtifact.ToString());

			// GrantArtifact is idempotent, persists to disk, refreshes the artifacts HUD,
			// and re-applies movement mods (Wing's lighter gravity kicks in immediately).
			GI->GrantArtifact(SecretSphereArtifact);

			// Player-facing story beat for the grant.
			ShowCenterMessage(SecretSphereMessage, SecretSphereMessageDuration);

			// Secret shortcut: also instantly win the run (spawns the home portal).
			if (ALoopedRunGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALoopedRunGameMode>() : nullptr)
			{
				GM->NotifyRunWon();
			}
			return;
		}
	}
}

void ALoopedCharacter::ShowCenterMessage(const FText& Message, float Duration)
{
	if (Message.IsEmpty()) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC && GetWorld()) PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_CenterMessage.WBP_CenterMessage_C"));
	if (!Cls) return;

	UUserWidget* Msg = CreateWidget<UUserWidget>(PC, Cls);
	if (!Msg) return;
	if (UTextBlock* T = Cast<UTextBlock>(Msg->GetWidgetFromName(TEXT("MessageText"))))
	{
		T->SetText(Message);
	}
	Msg->AddToViewport(200);

	// Stack vertically: each concurrent message sits a row below the previous one.
	const int32 Slot = ActiveCenterMessageCount;
	ActiveCenterMessageCount++;
	Msg->SetRenderTranslation(FVector2D(0.0f, (float)Slot * 52.0f));

	// Transient — auto-remove after Duration (matches the parkour/unlock message pattern).
	TWeakObjectPtr<UUserWidget> WeakMsg(Msg);
	TWeakObjectPtr<ALoopedCharacter> WeakThis(this);
	FTimerHandle TH;
	GetWorldTimerManager().SetTimer(TH, FTimerDelegate::CreateLambda([WeakMsg, WeakThis]()
	{
		if (WeakMsg.IsValid()) WeakMsg->RemoveFromParent();
		if (WeakThis.IsValid()) WeakThis->ActiveCenterMessageCount = FMath::Max(0, WeakThis->ActiveCenterMessageCount - 1);
	}), Duration, false);
}

void ALoopedCharacter::ApplyMovementMods()
{
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();

	// Speed/Gravity values now come from the equipped cards' per-level row data (Brittle-adjusted).
	float SpeedMul = 1.0f;
	if (const FPassiveCardLevel* SpeedLv = GetLevelData(GI, TEXT("Speed"))) SpeedMul += SpeedLv->MoveSpeedBonus;
	if (GI) SpeedMul += GI->GetArtifactSpeedBonus(); // run relics (additive fraction)

	// Wing artifact lowers the STARTING base gravity; the Gravity perk multiplies that base.
	const float BaseGravity = (GI && GI->HasArtifact(TEXT("Wing"))) ? WingGravityBase : 1.0f;
	float GravityMul = 1.0f;
	if (const FPassiveCardLevel* GravLv = GetLevelData(GI, TEXT("Gravity"))) GravityMul = GravLv->GravityScale;

	// Curse "Leaden": temporal drag — slower and heavier for the run.
	float CurseGravityMul = 1.0f;
	if (GI && GI->HasCurse(TEXT("Leaden")))
	{
		SpeedMul *= GI->CurseLeadenSpeedMult;
		CurseGravityMul = GI->CurseLeadenGravityMult;
	}

	const float ArtifactGravityMul = GI ? GI->GetArtifactGravityMult() : 1.0f; // run relics (Featherweight 0.85)
	const float NewWalk = BaseWalkSpeed * SpeedMul;
	const float FinalGravity = BaseGravity * GravityMul * CurseGravityMul * ArtifactGravityMul;
	GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? (NewWalk * SprintSpeedMultiplier) : NewWalk;
	GetCharacterMovement()->GravityScale = FinalGravity;

	UE_LOG(LogLoopedCore, Display, TEXT("Movement mods applied: speed=%.0f (mul %.2f) gravity_scale=%.2f"),
		NewWalk, SpeedMul, FinalGravity);
}
