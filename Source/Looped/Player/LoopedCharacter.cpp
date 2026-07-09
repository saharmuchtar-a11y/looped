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
#include "Core/LoopedInteractable.h"
#include "Core/LoopedRunGameMode.h"
#include "UI/LoopedPauseMenuWidget.h"
#include "Data/PassiveCardData.h"
#include "Data/EnemyVisualData.h"
#include "SloMo/SloMoManager.h"
#include "Components/ProgressBar.h"
#include "InputAction.h"
#include "Looped.h"
#include "Blueprint/UserWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
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
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "PhysicsEngine/PhysicsAsset.h"
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
	GetCharacterMovement()->SetCrouchedHalfHeight(44.0f);

	// Player-hurt SFX — loaded by path (plays when damage lands).
	static ConstructorHelpers::FObjectFinder<USoundBase> HurtSnd(TEXT("/Game/Audio/player_hurt.player_hurt"));
	if (HurtSnd.Succeeded()) PlayerHurtSound = HurtSnd.Object;

	static ConstructorHelpers::FObjectFinder<USoundBase> JumpSnd(TEXT("/Game/Audio/jump.jump"));
	if (JumpSnd.Succeeded()) JumpSound = JumpSnd.Object;

	// Same asset PortalActor uses for room travel — Lysa Second Wind cue (Sahar 2026-07-09).
	static ConstructorHelpers::FObjectFinder<USoundBase> PortalSnd(TEXT("/Game/Audio/portal.portal"));
	if (PortalSnd.Succeeded()) PortalTravelSound = PortalSnd.Object;

	// Death screen widget class — loaded from WBP_DeathScreen via path.
	static ConstructorHelpers::FClassFinder<UUserWidget> DeathScreenClassFinder(TEXT("/Game/UI/WBP_DeathScreen"));
	if (DeathScreenClassFinder.Succeeded())
	{
		DeathScreenClass = DeathScreenClassFinder.Class;
	}

	// Held-weapon mesh. In classic mode it attaches to the hand socket; in POV stick mode
	// BeginPlay reparents it to the FP camera (Manny body stays hidden).
	WeaponMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMeshComp->SetupAttachment(GetMesh(), WeaponAttachSocket);
	WeaponMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMeshComp->SetVisibility(false);
	WeaponMeshComp->SetCastShadow(false);

	// Soft refs for the Meshy Dead package (death cam only). Soft so cook stays light if unused.
	DeathBodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT(
		"/Game/new_assets/heronew/Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin/SkeletalMeshes/Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin.Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin")));
	DeathBodyAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT(
		"/Game/new_assets/heronew/Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin/SkeletalMeshes/Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin_Anim.Meshy_AI_Wasteland_Wanderer_biped_Animation_Dead_withSkin_Anim")));

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

	// Press-E interact — same auto-link pattern (mapped to E in IMC_Default).
	static ConstructorHelpers::FObjectFinder<UInputAction> InteractActionFinder(TEXT("/Game/IA_Interact"));
	if (InteractActionFinder.Succeeded())
	{
		InteractAction = InteractActionFinder.Object;
	}

	// Q skill — same auto-link pattern (mapped to Q in IMC_Default).
	static ConstructorHelpers::FObjectFinder<UInputAction> SkillActionFinder(TEXT("/Game/IA_Skill"));
	if (SkillActionFinder.Succeeded())
	{
		SkillAction = SkillActionFinder.Object;
	}

	// Esc/P pause menu — same auto-link pattern (mapped to Escape + P in IMC_Default).
	static ConstructorHelpers::FObjectFinder<UInputAction> PauseActionFinder(TEXT("/Game/IA_Pause"));
	if (PauseActionFinder.Succeeded())
	{
		PauseAction = PauseActionFinder.Object;
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
	if (bPOVStickMode)
	{
		StartPOVStickSwing();
		return;
	}
	if (AttackAnims.Num() == 0 || !GetMesh()) return;
	UAnimInstance* AnimInst = GetMesh()->GetAnimInstance();
	if (!AnimInst) return;
	const int32 Pick = FMath::RandRange(0, AttackAnims.Num() - 1);
	if (UAnimSequence* Seq = AttackAnims[Pick])
	{
		AnimInst->PlaySlotAnimationAsDynamicMontage(Seq, FName(TEXT("DefaultSlot")), 0.1f, 0.1f, 1.0f);
	}
}

void ALoopedCharacter::SetupPOVStickMode()
{
	if (!bPOVStickMode) return;

	// Alive POV: no body — Branch lives on the camera.
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetOwnerNoSee(true);
		Body->SetVisibility(false, true);
		Body->bCastHiddenShadow = false;
	}

	if (WeaponMeshComp && FirstPersonCamera)
	{
		WeaponMeshComp->AttachToComponent(FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		WeaponMeshComp->SetRelativeLocation(POVStickIdleLoc);
		WeaponMeshComp->SetRelativeRotation(POVStickIdleRot);
		WeaponMeshComp->SetRelativeScale3D(POVStickIdleScale);
		WeaponMeshComp->SetCastShadow(false);
		// Mesh is applied by WeaponHolder::EquipWeapon → SetWeaponVisualMesh.
		if (WeaponMeshComp->GetStaticMesh())
		{
			WeaponMeshComp->SetVisibility(true);
		}
	}

	BreathPhase = 0.0f;
	POVSwingElapsed = -1.0f;
	UE_LOG(LogLoopedCore, Display, TEXT("[POV] stick mode ON — body hidden, Branch on camera."));
}

void ALoopedCharacter::StartPOVStickSwing()
{
	if (!bPOVStickMode || !WeaponMeshComp) return;
	POVSwingElapsed = 0.0f;
}

void ALoopedCharacter::ApplyPOVStickPose(float SwingAlpha01, float InBreathPhase)
{
	if (!WeaponMeshComp || !bPOVStickMode) return;

	const float BreathZ = FMath::Sin(InBreathPhase) * BreathBobAmplitude;
	FVector Loc = POVStickIdleLoc + FVector(0.0f, 0.0f, BreathZ);
	FRotator Rot = POVStickIdleRot;

	if (SwingAlpha01 > KINDA_SMALL_NUMBER)
	{
		// 0→1→0 bell: ease out to peak at mid-swing, ease back to idle.
		const float Bell = FMath::Sin(SwingAlpha01 * PI);
		Rot = FMath::Lerp(POVStickIdleRot, POVSwingPeakRot, Bell);
		Loc += FVector(6.0f, -4.0f, 2.0f) * Bell; // push slightly into view on the slash
	}

	WeaponMeshComp->SetRelativeLocation(Loc);
	WeaponMeshComp->SetRelativeRotation(Rot);
	WeaponMeshComp->SetRelativeScale3D(POVStickIdleScale);
}

void ALoopedCharacter::UpdatePOVStick(float DeltaSeconds)
{
	if (!bPOVStickMode || !WeaponMeshComp || bInDeathCam) return;

	BreathPhase += DeltaSeconds * BreathBobSpeed * 2.0f * PI;

	float SwingA = 0.0f;
	if (POVSwingElapsed >= 0.0f)
	{
		POVSwingElapsed += DeltaSeconds;
		const float Dur = FMath::Max(0.05f, POVSwingDuration);
		SwingA = FMath::Clamp(POVSwingElapsed / Dur, 0.0f, 1.0f);
		if (POVSwingElapsed >= Dur)
		{
			POVSwingElapsed = -1.0f;
			SwingA = 0.0f;
		}
	}

	ApplyPOVStickPose(SwingA, BreathPhase);
}

void ALoopedCharacter::BeginPOVDeathBody()
{
	// Hide the FP stick — death cam shows the Meshy corpse instead.
	if (WeaponMeshComp)
	{
		WeaponMeshComp->SetVisibility(false);
	}

	USkeletalMesh* DeadMesh = DeathBodyMesh.LoadSynchronous();
	UAnimSequence* DeadAnim = DeathBodyAnim.LoadSynchronous();
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body || !DeadMesh)
	{
		UE_LOG(LogLoopedCore, Warning, TEXT("[POV] Death body missing — falling back to current mesh ragdoll."));
		return;
	}

	Body->SetAnimInstanceClass(nullptr);
	Body->SetSkeletalMesh(DeadMesh);
	Body->SetRelativeScale3D(FVector(DeathBodyScale));
	// Capsule-centered: Meshy Dead bounds are ~1.6uu; after ×111 the feet sit near z=0 of the mesh.
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
	Body->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	Body->SetOwnerNoSee(false);
	Body->SetVisibility(true, true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetSimulatePhysics(false);
	Body->SetAllBodiesSimulatePhysics(false);

	float AnimLen = 1.2f;
	if (DeadAnim)
	{
		AnimLen = FMath::Max(0.4f, DeadAnim->GetPlayLength());
		Body->PlayAnimation(DeadAnim, false);
	}

	const float RagdollDelay = AnimLen * FMath::Clamp(DeathAnimToRagdollFraction, 0.2f, 0.95f);
	GetWorldTimerManager().SetTimer(DeathRagdollTimerHandle, this,
		&ALoopedCharacter::StartPOVDeathRagdoll, RagdollDelay, false);

	// Hold the death cam at least through most of the anim + a beat of settle.
	DeathCamDuration = FMath::Max(DeathCamDuration, AnimLen + 0.6f);

	UE_LOG(LogLoopedCore, Display, TEXT("[POV] Meshy death body up (anim %.2fs, ragdoll @ %.2fs)."),
		AnimLen, RagdollDelay);
}

void ALoopedCharacter::StartPOVDeathRagdoll()
{
	if (USkeletalMeshComponent* M = GetMesh())
	{
		M->Stop();
		M->SetCollisionProfileName(TEXT("Ragdoll"));
		M->SetSimulatePhysics(true);
		M->SetAllBodiesSimulatePhysics(true);
		M->WakeAllRigidBodies();
		M->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
}

void ALoopedCharacter::ApplyHeroVisual()
{
	if (HeroVisualRow.IsNone() || !GetMesh()) return;
	UDataTable* VT = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_EnemyVisuals.DT_EnemyVisuals"));
	const FEnemyVisualSet* Row = VT ? VT->FindRow<FEnemyVisualSet>(HeroVisualRow, TEXT("HeroVisual"), false) : nullptr;
	if (!Row) return;
	USkeletalMesh* NewBody = Row->Mesh.LoadSynchronous();
	if (!NewBody) return;

	GetMesh()->SetSkeletalMesh(NewBody);
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	GetMesh()->EmptyOverrideMaterials();
	// Same fix as the enemies: always tick the pose (degenerate Meshy ref pose); bounds from live bones.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->SetRelativeLocation(Row->MeshRelLocation);
	GetMesh()->SetRelativeRotation(FRotator(0.0f, Row->MeshRelYaw, 0.0f));
	GetMesh()->SetRelativeScale3D(FVector(Row->MeshScale));

	HeroIdle   = Row->Idle.LoadSynchronous();
	HeroWalk   = Row->Walk.LoadSynchronous();
	HeroRun    = Row->Run.LoadSynchronous();
	HeroAttack = Row->Attack.LoadSynchronous();
	HeroCast   = Row->Cast.LoadSynchronous();
	bHeroVisualDriven = true;

	// FP head-clip fix again — the swap brought a fresh (un-hidden) head.
	static const FName HeadBones[] = { FName(TEXT("Head")), FName(TEXT("head")) };
	for (const FName& Bone : HeadBones)
	{
		if (GetMesh()->GetBoneIndex(Bone) != INDEX_NONE)
		{
			GetMesh()->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
			break;
		}
	}

	// The rigs name their hands differently (Manny "hand_r", Meshy "RightHand") — auto-detect,
	// then re-seat + renormalize the held weapon on the new bone.
	if (!GetMesh()->DoesSocketExist(WeaponAttachSocket))
	{
		static const FName HandBones[] = { FName(TEXT("RightHand")), FName(TEXT("hand_r")), FName(TEXT("Hand_R")) };
		for (const FName& Hand : HandBones)
		{
			if (GetMesh()->DoesSocketExist(Hand)) { WeaponAttachSocket = Hand; break; }
		}
	}
	if (WeaponMeshComp)
	{
		WeaponMeshComp->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachSocket);
		NormalizeWeaponTransform();
	}

	PlayHeroAnim(HeroIdle ? HeroIdle.Get() : HeroWalk.Get(), true);
	UE_LOG(LogLoopedCore, Display, TEXT("[Hero] visual kit '%s' applied (weapon socket '%s')."),
		*HeroVisualRow.ToString(), *WeaponAttachSocket.ToString());
}

void ALoopedCharacter::PlayHeroAnim(UAnimSequence* Anim, bool bLoop)
{
	if (!Anim || !GetMesh() || HeroCurrentAnim == Anim) return;
	HeroCurrentAnim = Anim;
	GetMesh()->PlayAnimation(Anim, bLoop);
}

void ALoopedCharacter::UpdateHeroAnim()
{
	if (!bHeroVisualDriven || !IsAlive()) return;
	if (GetWorld() && GetWorld()->GetTimeSeconds() < HeroAttackHoldUntil) return; // swing playing
	const float Speed = GetVelocity().Size2D();
	if (Speed > 500.0f)     PlayHeroAnim(HeroRun ? HeroRun.Get() : HeroWalk.Get(), true);
	else if (Speed > 60.0f) PlayHeroAnim(HeroWalk ? HeroWalk.Get() : HeroRun.Get(), true);
	else                    PlayHeroAnim(HeroIdle ? HeroIdle.Get() : HeroWalk.Get(), true);
}

void ALoopedCharacter::PlayHeroAttackAnim(bool bMelee)
{
	if (!bHeroVisualDriven || !GetMesh()) return;
	UAnimSequence* Swing = bMelee ? (HeroAttack ? HeroAttack.Get() : HeroCast.Get())
	                              : (HeroCast ? HeroCast.Get() : HeroAttack.Get());
	if (!Swing) return;
	HeroCurrentAnim = Swing;
	GetMesh()->PlayAnimation(Swing, false); // deliberate restart — every shot swings
	HeroAttackHoldUntil = GetWorld()->GetTimeSeconds() + FMath::Min(Swing->GetPlayLength(), 0.9f);
}

void ALoopedCharacter::SetWeaponVisualMesh(UStaticMesh* NewMesh)
{
	if (!WeaponMeshComp) return;

	if (bPOVStickMode && FirstPersonCamera)
	{
		// Stay on the camera — never re-seat onto a hand bone in POV mode.
		if (WeaponMeshComp->GetAttachParent() != FirstPersonCamera)
		{
			WeaponMeshComp->AttachToComponent(FirstPersonCamera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		WeaponMeshComp->SetStaticMesh(NewMesh);
		WeaponMeshComp->SetVisibility(NewMesh != nullptr);
		WeaponMeshComp->SetRelativeLocation(POVStickIdleLoc);
		WeaponMeshComp->SetRelativeRotation(POVStickIdleRot);
		WeaponMeshComp->SetRelativeScale3D(POVStickIdleScale);
		return;
	}

	// Re-assert the socket attachment in case the socket name changed in defaults after construction.
	if (WeaponMeshComp->GetAttachSocketName() != WeaponAttachSocket)
	{
		WeaponMeshComp->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponAttachSocket);
	}
	WeaponMeshComp->SetStaticMesh(NewMesh);
	WeaponMeshComp->SetVisibility(NewMesh != nullptr);

	if (NewMesh)
	{
		// Normalize now AND once the anim pose is live — the first frames can still be in the
		// reference pose, whose bone scale differs from the animated pose on the Wanderer rig.
		NormalizeWeaponTransform();
		GetWorldTimerManager().SetTimer(WeaponNormalizeTimerHandle, this,
			&ALoopedCharacter::NormalizeWeaponTransform, 0.25f, false);
	}
}

void ALoopedCharacter::NormalizeWeaponTransform()
{
	if (!WeaponMeshComp || !GetMesh() || !WeaponMeshComp->GetStaticMesh()) return;

	// World-unit sizing + grip offset, applied against the live socket transform. UE derives the
	// (skeleton-specific) relative values from these, so the same numbers hold the weapon the same
	// way on Manny, the Wanderer, or any future hero rig. Grip ROTATION stays the BP-tuned relative
	// rotation (scale-independent).
	const FTransform SocketT = GetMesh()->GetSocketTransform(WeaponAttachSocket);
	WeaponMeshComp->SetWorldScale3D(WeaponWorldScale);
	WeaponMeshComp->SetWorldLocation(SocketT.GetLocation() + SocketT.TransformVectorNoScale(WeaponGripOffset));
	UE_LOG(LogLoopedCore, Display, TEXT("Weapon normalized: socket=%s socket_scale=%s world_scale=%s"),
		*WeaponAttachSocket.ToString(), *SocketT.GetScale3D().ToString(), *WeaponWorldScale.ToString());
}

void ALoopedCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Seed the chrono gauge from the run state (-1 = fresh run → start full).
	if (const ULoopedGameInstance* SkillGI = GetGameInstance<ULoopedGameInstance>())
	{
		SkillGauge = SkillGI->GetRunSkillGauge();
	}
	if (SkillGauge < 0.0f)
	{
		SkillGauge = GetSkillGaugeMax();
	}

	// Cache the camera's rest position so the positional shake can jitter around it + snap back.
	if (FirstPersonCamera)
	{
		BaseCameraRelLoc = FirstPersonCamera->GetRelativeLocation();
	}

	// POV stick mode: hide the body, put the Branch on the camera. Skips Manny head-hide /
	// hero-visual kit (those only matter when the body is visible).
	if (bPOVStickMode)
	{
		SetupPOVStickMode();
	}
	else
	{
		// FP head-clip fix: the eye camera sits at head height, so hide the head bone (whichever
		// naming this mesh uses — Wanderer "Head" / Manny "head"). Un-hidden by the death cam so
		// the 3rd-person corpse keeps its head.
		if (USkeletalMeshComponent* BodyMesh = GetMesh())
		{
			static const FName HeadBoneNames[] = { FName(TEXT("Head")), FName(TEXT("head")) };
			for (const FName& Bone : HeadBoneNames)
			{
				if (BodyMesh->GetBoneIndex(Bone) != INDEX_NONE)
				{
					BodyMesh->HideBoneByName(Bone, EPhysBodyOp::PBO_None);
					break;
				}
			}
		}

		// Optional hero visual kit (heronew). Re-hides the head on the new mesh itself.
		ApplyHeroVisual();
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

			// Serin's ransom sting: the curse owed to Vorr lands as the run begins.
			const FName OwedCurse = GI->TakePendingNextRunCurse();
			if (!OwedCurse.IsNone())
			{
				GI->AddCurse(OwedCurse);
				ShowCenterMessage(FText::FromString(FString::Printf(
					TEXT("Vorr's ransom comes due — %s."), *GI->GetCurseDescription(OwedCurse).ToString())), 4.0f);
				UE_LOG(LogLoopedCore, Display, TEXT("[Ransom] Next-run curse '%s' applied."), *OwedCurse.ToString());
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

		// Relic "VoidCompass": pings when a still-unclaimed secret sphere hides in this room.
		// Delayed a beat so the center-message widget infrastructure is ready.
		if (SecretSpheres.Num() > 0)
		{
			if (ULoopedGameInstance* CompassGI = GetGameInstance<ULoopedGameInstance>())
			{
				if (CompassGI->HasArtifact(TEXT("VoidCompass")) && !CompassGI->HasArtifact(SecretSphereArtifact))
				{
					FTimerHandle CompassTimer;
					GetWorldTimerManager().SetTimer(CompassTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
					{
						ShowCenterMessage(FText::FromString(TEXT("the compass trembles — something hides in this room")), 4.0f);
					}), 1.5f, false);
				}
			}
		}
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
	// Combat HP is POCCurrentHealth (TakeDamageFromEnemy never writes GAS Health).
	return GetPOCHealthPercent();
}

bool ALoopedCharacter::IsAlive() const
{
	// MUST use POC HP — GAS AttributeSet Health is never synced by the combat path, so reading
	// it left IsAlive() true after death. Hazards then kept ticking ApplyElementalStatus on a
	// ragdolling corpse (death-cam mesh overlaps) → AV. Also null-guard ASC if anything else
	// still peeks at it.
	return POCCurrentHealth > 0.0f;
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
	if (InteractAction)
	{
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &ALoopedCharacter::TryInteract);
	}
	if (SkillAction)
	{
		EIC->BindAction(SkillAction, ETriggerEvent::Started, this, &ALoopedCharacter::OnSkillPressed);
	}
	if (PauseAction)
	{
		EIC->BindAction(PauseAction, ETriggerEvent::Started, this, &ALoopedCharacter::OnPausePressed);
	}
}

void ALoopedCharacter::OnPausePressed()
{
	TogglePauseMenu();
}

void ALoopedCharacter::TogglePauseMenu()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC && GetWorld()) PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;

	if (!bPauseMenuOpen)
	{
		// One menu at a time: the monitor steps aside (it also owns slo-mo, which real pause trumps).
		if (bHologramOpen)
		{
			CloseHologram();
		}
		if (!PauseMenuWidget)
		{
			// Code-only widget — constructed straight from the C++ class (no WBP asset to cook).
			PauseMenuWidget = CreateWidget<UUserWidget>(PC, ULoopedPauseMenuWidget::StaticClass());
		}
		if (!PauseMenuWidget) return;
		if (!PauseMenuWidget->IsInViewport())
		{
			PauseMenuWidget->AddToViewport(300); // above everything, monitor included
		}
		bPauseMenuOpen = true;
		UGameplayStatics::SetGamePaused(this, true);
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeGameAndUI()); // GameAndUI: IA_Pause (bTriggerWhenPaused) can still close
	}
	else
	{
		if (PauseMenuWidget && PauseMenuWidget->IsInViewport())
		{
			PauseMenuWidget->RemoveFromParent();
		}
		bPauseMenuOpen = false;
		UGameplayStatics::SetGamePaused(this, false);
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void ALoopedCharacter::OnSkillPressed()
{
	if (!IsAlive() || bHologramOpen) return;
	if (bSkillActive)
	{
		EndChronoSkill();
	}
	else if (SkillGauge >= SkillMinActivation)
	{
		StartChronoSkill();
	}
}

void ALoopedCharacter::StartChronoSkill()
{
	if (bSkillActive) return;
	bSkillActive = true;
	if (USloMoManager* SloMo = GetWorld()->GetSubsystem<USloMoManager>())
	{
		SloMo->RequestSloMo(ESloMoTrigger::SkillDodge);
	}
}

void ALoopedCharacter::EndChronoSkill()
{
	if (!bSkillActive) return;
	bSkillActive = false;
	if (USloMoManager* SloMo = GetWorld()->GetSubsystem<USloMoManager>())
	{
		SloMo->ReleaseSloMo(ESloMoTrigger::SkillDodge);
	}
}

float ALoopedCharacter::GetSkillGaugeMax() const
{
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	return SkillGaugeMaxBase + (GI ? GI->GetSkillGaugeBonusSeconds() : 0.0f);
}

void ALoopedCharacter::UpdateSkillGaugeBar()
{
	// Lazy-create the slim low-center bar (same pattern as the interact prompt).
	if (!SkillGaugeWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC && GetWorld()) PC = GetWorld()->GetFirstPlayerController();
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_SkillGauge.WBP_SkillGauge_C"));
		if (!PC || !Cls) return;
		SkillGaugeWidget = CreateWidget<UUserWidget>(PC, Cls);
		if (!SkillGaugeWidget) return;
		SkillGaugeWidget->AddToViewport(30); // under prompts/menus
	}

	const float Max = GetSkillGaugeMax();
	if (UProgressBar* Bar = Cast<UProgressBar>(SkillGaugeWidget->GetWidgetFromName(TEXT("GaugeBar"))))
	{
		Bar->SetPercent(Max > 0.0f ? SkillGauge / Max : 0.0f);
		// Active = hot white; ready = cyan; recharging from a dip = dimmer cyan.
		const FLinearColor Fill = bSkillActive ? FLinearColor(0.95f, 0.98f, 1.0f)
			: (SkillGauge >= Max - KINDA_SMALL_NUMBER) ? FLinearColor(0.20f, 0.85f, 1.0f)
			                                           : FLinearColor(0.14f, 0.55f, 0.75f);
		Bar->SetFillColorAndOpacity(Fill);
	}
}

ILoopedInteractable* ALoopedCharacter::FindBestInteractable() const
{
	// Nearest ILoopedInteractable within its own range wins (keyholes, levers, shops, altars).
	// The implementer count is tiny, so a full actor sweep is fine at poll rate.
	ILoopedInteractable* Best = nullptr;
	float BestDistSq = FLT_MAX;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		ILoopedInteractable* Target = Cast<ILoopedInteractable>(*It);
		if (!Target) continue;
		const float Range = Target->GetInteractRange();
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (DistSq <= Range * Range && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Target;
		}
	}
	return Best;
}

void ALoopedCharacter::TryInteract()
{
	if (!IsAlive() || bHologramOpen) return;
	if (ILoopedInteractable* Best = FindBestInteractable())
	{
		Best->Interact(this);
	}
}

void ALoopedCharacter::UpdateInteractPrompt(float DeltaSeconds)
{
	// Light poll — the sweep is cheap but doesn't need to run every frame.
	InteractPromptAccum += DeltaSeconds;
	if (InteractPromptAccum < 0.15f) return;
	InteractPromptAccum = 0.0f;

	FText Prompt;
	if (IsAlive() && !bHologramOpen)
	{
		if (ILoopedInteractable* Best = FindBestInteractable())
		{
			Prompt = Best->GetInteractPrompt();
		}
	}

	if (Prompt.IsEmpty())
	{
		if (InteractPromptWidget) InteractPromptWidget->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Lazy-create the low-center prompt widget (runtime LoadClass, robust to creation order).
	if (!InteractPromptWidget)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		if (!PC && GetWorld()) PC = GetWorld()->GetFirstPlayerController();
		TSubclassOf<UUserWidget> Cls = LoadClass<UUserWidget>(nullptr, TEXT("/Game/UI/WBP_InteractPrompt.WBP_InteractPrompt_C"));
		if (!PC || !Cls) return;
		InteractPromptWidget = CreateWidget<UUserWidget>(PC, Cls);
		if (!InteractPromptWidget) return;
		InteractPromptWidget->AddToViewport(150);
	}
	if (UTextBlock* T = Cast<UTextBlock>(InteractPromptWidget->GetWidgetFromName(TEXT("MessageText"))))
	{
		T->SetText(FText::FromString(FString::Printf(TEXT("Press [E] to %s"), *Prompt.ToString())));
	}
	InteractPromptWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
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

	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		// Curse "Frailty": you take more damage this run (Iron Will softens it).
		if (GI->HasCurse(TEXT("Frailty")))
		{
			Damage *= GI->ScaleCurseMult(GI->CurseFrailtyDamageMult);
		}
		// Card "GlassCannon": the pact cuts both ways — bonus damage dealt, bonus damage TAKEN.
		if (const FPassiveCardLevel* GlassLv = GI->GetEffectiveLevelData(TEXT("GlassCannon")))
		{
			Damage *= 1.0f + GlassLv->Fraction;
		}
	}

	// Void "Weaken" status: amplify incoming damage while active (1.0 = no effect).
	Damage *= StatusWeakenMultiplier;

	// --- Rescued-companion relics (permanent, unique — see looped_rescue_system.md) ---
	if (ULoopedGameInstance* CompanionGI = GetGameInstance<ULoopedGameInstance>())
	{
		// Brann "Forged Plate": take less damage, always.
		if (CompanionGI->HasArtifact(TEXT("Brann")))
		{
			Damage *= BrannPlateDamageMult;
		}
		// Lysa "Second Wind": once per run, a lethal hit leaves you at 50% max HP instead of killing you.
		// (Was 1 HP — too fragile vs DoT; Sahar 2026-07-09.) Portal whoosh = audible revive cue.
		if (Damage >= POCCurrentHealth && CompanionGI->HasArtifact(TEXT("Lysa")) && !CompanionGI->IsSecondWindUsed())
		{
			CompanionGI->MarkSecondWindUsed();
			const float ReviveHP = FMath::Max(1.0f, POCMaxHealth * 0.5f);
			// Set HP directly — avoid negative-Damage heal math (logged as "took -N damage") and
			// any re-entrancy surprises from Max(0, HP - negative).
			POCCurrentHealth = ReviveHP;
			Damage = 0.0f;
			// Clear burn/poison DoT so the same tick chain can't finish you the frame after revive.
			GetWorldTimerManager().ClearTimer(StatusBurnTimerHandle);
			StatusBurnTicksRemaining = 0;
			StatusBurnDamagePerTick = 0.0f;
			// Same null-guard pattern as PortalActor::Travel — never PlaySound2D on a missing asset.
			if (PortalTravelSound)
			{
				UGameplayStatics::PlaySound2D(this, PortalTravelSound);
			}
			ShowCenterMessage(FText::FromString(TEXT("LYSA PULLS YOU BACK — second wind!")), 2.5f);
			UE_LOG(LogLoopedCore, Display, TEXT("[Companion] Second Wind consumed — lethal hit survived at %.0f HP (50%% max)."), ReviveHP);
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
	// Death releases the chrono skill — the world must not stay slowed over a corpse.
	EndChronoSkill();

	// Stop DoT / status timers — ragdoll can re-overlap hazards; StatusBurnTick must not re-enter
	// TakeDamageFromEnemy / death cam while we're already dead.
	GetWorldTimerManager().ClearTimer(StatusBurnTimerHandle);
	GetWorldTimerManager().ClearTimer(StatusSlowTimerHandle);
	GetWorldTimerManager().ClearTimer(StatusWeakenTimerHandle);
	StatusBurnTicksRemaining = 0;
	StatusBurnDamagePerTick = 0.0f;
	StatusSlowMultiplier = 1.0f;
	StatusWeakenMultiplier = 1.0f;

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

	if (bPOVStickMode)
	{
		// Meshy death body + anim, then physics settle. Stick is hidden inside.
		BeginPOVDeathBody();
	}
	else
	{
		// Restore the head (hidden for first-person) so the 3rd-person death shot shows a whole body.
		if (USkeletalMeshComponent* BodyMesh = GetMesh())
		{
			static const FName HeadBoneNames[] = { FName(TEXT("Head")), FName(TEXT("head")) };
			for (const FName& Bone : HeadBoneNames)
			{
				if (BodyMesh->GetBoneIndex(Bone) != INDEX_NONE)
				{
					BodyMesh->UnHideBoneByName(Bone);
					break;
				}
			}
		}

		// Ragdoll the hero so the body crumples to the floor (Manny's PA_Mannequin handles this well).
		if (USkeletalMeshComponent* M = GetMesh())
		{
			M->SetOwnerNoSee(false);
			M->SetCollisionProfileName(TEXT("Ragdoll"));
			M->SetSimulatePhysics(true);
			M->SetAllBodiesSimulatePhysics(true);
			M->WakeAllRigidBodies();
			M->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		}
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
	// Tutorial gate: no monitor until Orin straps it on (his rescue dialogue grants the
	// artifact). Programmatic opens (card drafts) bypass this on purpose — they only fire
	// post-grant anyway, and the tutorial's own draft must never dead-end.
	if (!bHologramOpen && !MonitorGateArtifact.IsNone())
	{
		const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
		if (GI && !GI->HasArtifact(MonitorGateArtifact))
		{
			ShowCenterMessage(FText::FromString(TEXT("Your bare arm itches — something belongs there.")), 2.5f);
			return;
		}
	}
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

	// If the dashboard was already open (State 1), the missions panel is showing — the card draft
	// owns the center now, so tuck it away (OpenHologram above only fires on a fresh open).
	HideMissionsPanel();

	if (UWidget* Center = WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel")))
	{
		Center->SetVisibility(ESlateVisibility::Visible);
	}
	// Hide the dummy placeholder so only the real cards show.
	if (UWidget* Placeholder = WristMenuWidget->GetWidgetFromName(TEXT("CenterPlaceholder")))
	{
		Placeholder->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Cards render as their own full-screen viewport overlay — NOT parented into CenterPanel
	// (tried that: squeezing the card canvas into the panel slot shrank it tiny, Sahar playtest
	// 2026-07-03). Z must sit ABOVE the monitor (200): below it, the monitor's CenterPanel
	// swallows every click even though the cards show through its transparent middle.
	if (!CardWidget->IsInViewport())
	{
		CardWidget->AddToViewport(210);
	}

	// A previous draft may still be mounted (Mira's reroll replaces the widget wholesale) —
	// pull it out of the panel before the new one goes in, or they stack.
	if (MountedCardWidget.IsValid() && MountedCardWidget.Get() != CardWidget)
	{
		MountedCardWidget->RemoveFromParent();
	}

	// Rarity is FELT here: each card's description line takes its tier color (the name keeps
	// its element color; the panel border brush is the icon art — hands off). CardRowName1..4
	// were set by SetCardOptions/ConfigureExtra before this widget mounted.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		for (int32 i = 1; i <= 4; ++i)
		{
			const FNameProperty* Prop = FindFProperty<FNameProperty>(
				CardWidget->GetClass(), *FString::Printf(TEXT("CardRowName%d"), i));
			if (!Prop) continue;
			const FName Card = Prop->GetPropertyValue_InContainer(CardWidget);
			if (Card.IsNone()) continue;
			if (UTextBlock* Desc = Cast<UTextBlock>(CardWidget->GetWidgetFromName(*FString::Printf(TEXT("Card%dDesc"), i))))
			{
				Desc->SetColorAndOpacity(FSlateColor(ULoopedGameInstance::GetRarityColor(GI->GetPerkRarity(Card))));
			}
		}
	}

	// Mira "Reroll": show + bind her button only while the once-per-run token is unspent.
	// C++-by-name binding, same pattern as the Boss/Room HUD buttons.
	MountedCardWidget = CardWidget;
	if (UButton* Reroll = Cast<UButton>(CardWidget->GetWidgetFromName(TEXT("RerollButton"))))
	{
		const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
		const bool bCanReroll = GI && GI->CanUseCardReroll();
		Reroll->SetVisibility(bCanReroll ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Reroll->OnClicked.RemoveDynamic(this, &ALoopedCharacter::OnCardRerollClicked);
		Reroll->OnClicked.AddDynamic(this, &ALoopedCharacter::OnCardRerollClicked);
	}

	RefreshDashboard();
}

void ALoopedCharacter::OnCardRerollClicked()
{
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI || !GI->ConsumeCardReroll()) return;

	// Hide the button — the token is spent for this run.
	if (MountedCardWidget.IsValid())
	{
		if (UButton* Reroll = Cast<UButton>(MountedCardWidget->GetWidgetFromName(TEXT("RerollButton"))))
		{
			Reroll->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// Re-run the manager's roll — it redraws ChosenCard1..3 and refills the widget texts.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->GetClass()->GetName() == TEXT("BP_CardRewardManager_C"))
		{
			if (UFunction* Fn = It->FindFunction(TEXT("TriggerCardReward")))
			{
				It->ProcessEvent(Fn, nullptr);
				ShowCenterMessage(FText::FromString(TEXT("Mira retunes the frequencies — fresh cards.")), 2.5f);
				UE_LOG(LogLoopedCore, Display, TEXT("[Companion] Mira reroll consumed — card draft redrawn."));
			}
			break;
		}
	}
}

// Dashboard widgets hidden while a dialogue owns the monitor. CenterHeader/CenterPlaceholder are
// already Collapsed by design (card-draft layout), so only these need toggling.
static const TCHAR* GDialogueHiddenZones[] = {
	TEXT("TL_Deck"), TEXT("TR_Relics"), TEXT("BL_Curses"), TEXT("BR_Stats"),
	TEXT("ShardsText"), TEXT("EchoesText"), TEXT("LogoImage")
};

void ALoopedCharacter::MountDialogueInMonitor(UUserWidget* DialogueWidget)
{
	if (!DialogueWidget) return;

	if (!bHologramOpen)
	{
		OpenHologram(/*bRewardMode*/ true); // slow-mo + free cursor + no-damage, CenterPanel shown
	}
	if (!WristMenuWidget) return;

	for (const TCHAR* Name : GDialogueHiddenZones)
	{
		if (UWidget* W = WristMenuWidget->GetWidgetFromName(Name))
		{
			W->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	// Dialogue takes the whole center — fold the guidance panel away so it doesn't sit under the
	// speaker/body/buttons (it shares CenterPanel with them).
	HideMissionsPanel();

	if (UPanelWidget* Center = Cast<UPanelWidget>(WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel"))))
	{
		Center->SetVisibility(ESlateVisibility::Visible);
		// StartDialogue runs once per NODE — only add the widget the first time.
		if (DialogueWidget->GetParent() == nullptr && !DialogueWidget->IsInViewport())
		{
			if (UPanelSlot* Added = Center->AddChild(DialogueWidget))
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
	}
}

void ALoopedCharacter::UnmountDialogueFromMonitor(UUserWidget* DialogueWidget)
{
	if (DialogueWidget)
	{
		DialogueWidget->RemoveFromParent();
	}
	if (WristMenuWidget)
	{
		for (const TCHAR* Name : GDialogueHiddenZones)
		{
			if (UWidget* W = WristMenuWidget->GetWidgetFromName(Name))
			{
				W->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
	if (bHologramOpen)
	{
		CloseHologram(); // restores time + cursor lock
	}
}

void ALoopedCharacter::OpenHologram(bool bRewardMode)
{
	// The monitor takes over time; the chrono skill releases so its gauge stops draining.
	EndChronoSkill();

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

	// The center column serves two masters: in State 2 (reward draft / dialogue) it hosts that
	// content, so the guidance panel steps aside; in State 1 (the plain dashboard) the center IS
	// the missions & hints panel, rebuilt live from GI->EvaluateMissions().
	if (WristMenuWidget)
	{
		if (bRewardMode)
		{
			HideMissionsPanel();
			if (UWidget* Center = WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel")))
			{
				Center->SetVisibility(ESlateVisibility::Visible);
			}
		}
		else
		{
			RefreshMissionsPanel(); // shows CenterPanel + fills it
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
	// Run blessings + curses as "Name — what it does" so nothing on the monitor is a mystery word.
	FString Relics;
	for (const FName& Id : GI->GetRunArtifacts())
	{
		const FArtifactData* R = GI->FindArtifactRow(Id);
		if (R && !R->Description.IsEmpty())
		{
			Relics += FString::Printf(TEXT("%s — %s\n"), *R->DisplayName.ToString(), *R->Description.ToString());
		}
		else
		{
			Relics += FString::Printf(TEXT("%s\n"), R ? *R->DisplayName.ToString() : *Id.ToString());
		}
	}
	SetText(TEXT("RelicsText"), Relics.IsEmpty() ? FString(TEXT("(none)")) : Relics);

	FString Curses;
	for (const FName& C : GI->GetActiveCurses())
	{
		const FString Desc = GI->GetCurseDescription(C).ToString();
		Curses += Desc.IsEmpty()
			? FString::Printf(TEXT("%s\n"), *C.ToString())
			: FString::Printf(TEXT("%s — %s\n"), *C.ToString(), *Desc);
	}
	SetText(TEXT("CursesText"), Curses.IsEmpty() ? FString(TEXT("(none)")) : Curses);
}

void ALoopedCharacter::RefreshMissionsPanel()
{
	if (!WristMenuWidget) return;
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	UPanelWidget* Center = Cast<UPanelWidget>(WristMenuWidget->GetWidgetFromName(TEXT("CenterPanel")));
	if (!Center) return;

	// Match the dashboard's typeface: clone the font from an existing zone header (size 17) and a
	// body block (size 14) so the guidance panel reads as part of the same monitor, not bolted on.
	FSlateFontInfo HeaderFont, BodyFont;
	if (UTextBlock* Src = Cast<UTextBlock>(WristMenuWidget->GetWidgetFromName(TEXT("DeckHeader")))) HeaderFont = Src->GetFont();
	if (UTextBlock* Src = Cast<UTextBlock>(WristMenuWidget->GetWidgetFromName(TEXT("PerksText"))))  BodyFont   = Src->GetFont();

	// Build the container once, then just refill it (rebuilding a fresh box each open would leak
	// the old one into the widget tree). Parented into CenterPanel — the State-1 center column.
	if (!MissionsBox)
	{
		MissionsBox = NewObject<UVerticalBox>(WristMenuWidget);
		Center->AddChild(MissionsBox);
	}
	MissionsBox->ClearChildren();

	auto AddLine = [&](const FString& S, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		UTextBlock* T = NewObject<UTextBlock>(WristMenuWidget);
		if (!T) return;
		T->SetText(FText::FromString(S));
		T->SetFont(Font);
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetAutoWrapText(true);
		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(MissionsBox->AddChild(T)))
		{
			VSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 3.f));
		}
	};

	const FLinearColor Amber(1.0f, 0.75f, 0.2f);
	const FLinearColor Cyan(0.35f, 0.85f, 1.0f);
	const FLinearColor Done(0.5f, 0.5f, 0.5f);

	const TArray<FMissionStatus> Rows = GI->EvaluateMissions();

	// The single highest-priority active hint is pinned as the header line (planning nudges only —
	// v1 has no live/reflex hints here, since the panel is evaluated on open, not per-frame).
	if (const FMissionStatus* Hint = Rows.FindByPredicate(
			[](const FMissionStatus& S){ return S.Category == EMissionCategory::Hint; }))
	{
		AddLine(FString::Printf(TEXT("! %s"), *Hint->DisplayText.ToString()), HeaderFont, Amber);
	}

	AddLine(TEXT("OBJECTIVES"), HeaderFont, Cyan);

	// Finished objectives drop off the board entirely (no lingering greyed [x] line) — only
	// live work stays listed. Partial progress ("3/4") still shows; it isn't complete yet.
	// Capped at MaxVisibleObjectives (priority already sorted them): the panel is a glance,
	// not a ledger — overflow collapses to a dim "+N more".
	int32 Shown = 0;
	int32 Completed = 0;
	int32 Overflow = 0;
	for (const FMissionStatus& S : Rows)
	{
		if (S.Category != EMissionCategory::Mission) continue;
		if (S.bComplete) { ++Completed; continue; }
		if (Shown >= MaxVisibleObjectives) { ++Overflow; continue; }
		++Shown;
		AddLine(FString::Printf(TEXT("[ ] %s"), *S.DisplayText.ToString()), BodyFont, FLinearColor::White);
	}
	if (Overflow > 0)
	{
		AddLine(FString::Printf(TEXT("+%d more"), Overflow), BodyFont, Done);
	}
	if (Shown == 0)
	{
		// Tell "you've cleared everything" apart from "nothing has opened up yet".
		AddLine(Completed > 0 ? TEXT("All objectives complete.") : TEXT("(no objectives yet)"),
			BodyFont, Done);
	}

	Center->SetVisibility(ESlateVisibility::Visible);
	MissionsBox->SetVisibility(ESlateVisibility::Visible);
}

void ALoopedCharacter::HideMissionsPanel()
{
	if (MissionsBox)
	{
		MissionsBox->SetVisibility(ESlateVisibility::Collapsed);
	}
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

void ALoopedCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Card "Shockwave": landing from a jump slams nearby enemies — damage + a hard stop.
	// Pairs with the Gravity build (heavier fall, same slam). Values from the card's level row.
	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	const FPassiveCardLevel* Lv = GI ? GI->GetEffectiveLevelData(TEXT("Shockwave")) : nullptr;
	if (!Lv || Lv->Damage <= 0.0f || Lv->Radius <= 0.0f) return;

	int32 HitCount = 0;
	const FVector MyLoc = GetActorLocation();
	for (TActorIterator<AEnemyBase> It(GetWorld()); It; ++It)
	{
		AEnemyBase* Enemy = *It;
		if (!Enemy || !Enemy->IsAlive()) continue;
		if (FVector::Dist(MyLoc, Enemy->GetActorLocation()) > Lv->Radius) continue;
		Enemy->TakeDamageFromPlayer(Lv->Damage, this);
		Enemy->GetCharacterMovement()->StopMovementImmediately(); // the stagger beat
		++HitCount;
	}
	if (HitCount > 0)
	{
		AddCameraShake(0.8f);
		UE_LOG(LogLoopedCore, Display, TEXT("[Card] Shockwave hit %d enemies (%.0f dmg, %.0f radius)"),
			HitCount, Lv->Damage, Lv->Radius);
	}
}

void ALoopedCharacter::TriggerMomentumBurst(float SpeedFraction)
{
	if (SpeedFraction <= 0.0f) return;
	MomentumSpeedBonus = SpeedFraction;
	ApplyMovementMods();
	// Refresh (not stack) on every kill — chain kills keep the burst alive.
	GetWorldTimerManager().SetTimer(MomentumTimerHandle, this, &ALoopedCharacter::EndMomentumBurst,
		FMath::Max(0.2f, MomentumDuration), false);
}

void ALoopedCharacter::EndMomentumBurst()
{
	MomentumSpeedBonus = 0.0f;
	ApplyMovementMods();
}

void ALoopedCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Hero body: single-node anim follows velocity (attack one-shots hold their window).
	UpdateHeroAnim();

	// POV Branch: breath bob + slash arc (skipped while dead).
	UpdatePOVStick(DeltaSeconds);

	// Proximity "Press [E] to ..." prompt (self-throttled to ~7Hz inside).
	UpdateInteractPrompt(DeltaSeconds);

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

	// Q chrono skill: drain while active, trickle back while not — in REAL seconds, so the
	// slow-mo itself can't warp the budget. Mirrored to the run state (travels like health).
	{
		const float RealDt = GetWorld()->DeltaRealTimeSeconds;
		const float Max = GetSkillGaugeMax();
		if (bSkillActive)
		{
			SkillGauge -= RealDt;
			if (SkillGauge <= 0.0f)
			{
				SkillGauge = 0.0f;
				EndChronoSkill();
			}
		}
		else if (SkillGauge < Max)
		{
			SkillGauge = FMath::Min(SkillGauge + SkillRechargePerSecond * RealDt, Max);
		}
		if (ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			GI->SetRunSkillGauge(SkillGauge);
		}
		UpdateSkillGaugeBar();
	}

	// Secret chain link 3: while the gate is open, touching a room's Time Sphere grants an artifact.
	CheckSecretSpheres(DeltaSeconds);
	UpdateDimmedEffect(DeltaSeconds);

	// Curse "Decay": bleed HP over the run. Accumulate and apply in ~1 HP chunks via the
	// normal damage path (handles death/stats). Curses are cleared in the Hub, so this only
	// ticks during a run.
	if (POCCurrentHealth > 0.0f)
	{
		if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
		{
			if (GI->HasCurse(TEXT("Decay")))
			{
				// Iron Will slows the rot.
				DecayAccum += GI->CurseDecayPerSecond * GI->GetCurseSeverityMult() * DeltaSeconds;
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

// Fetch the per-level tuning row for an equipped card at its (Brittle-adjusted) level. Null if not
// equipped / no data. (Brittle logic now lives in the GameInstance so other systems — e.g. the
// weapon crit roll — read the exact same effective level.)
static const FPassiveCardLevel* GetLevelData(const ULoopedGameInstance* GI, FName CardId)
{
	return GI ? GI->GetEffectiveLevelData(CardId) : nullptr;
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
		Bonus += (float)GI->GetRunBonusMaxHP(); // Void Vigor cache purchases (this run)
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
	// BP_CardRewardManager's one call site treats the result AS the draft (it displays
	// index 0..N-1 verbatim), so return the rarity-weighted OFFER, not the raw pool.
	if (const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>())
	{
		return GI->GetCardOffer(InAll);
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
	const FGameplayTag TagCryo      = FGameplayTag::RequestGameplayTag(FName("Effect.Cryo"), false);

	for (const FPassiveSlot& Slot : GI->RunDeck)
	{
		if (Slot.IsEmpty() || Slot.Level <= 0) continue;
		const FPassiveCardLevel* Lv = GetLevelData(GI, Slot.CardRowName);
		if (!Lv) continue;
		const FGameplayTagContainer& CardTags = Slot.CachedData.EffectTags;

		if (CardTags.HasTagExact(TagBurn))           Target->ApplyBurnEffect(Lv->Damage, Lv->Ticks);
		else if (CardTags.HasTagExact(TagVenom))     Target->ApplyVenomEffect(Lv->Damage, Lv->Ticks, Lv->SlowMultiplier);
		else if (CardTags.HasTagExact(TagLifesteal)) HealPlayer(Lv->HealAmount);
		else if (CardTags.HasTagExact(TagCryo))      Target->ApplyCryoEffect(Lv->FreezeDuration);
		// Speed/Gravity/MaxHP are passive (movement/HP). ChainSpark is handled in the hit chain;
		// Echo (chain re-trigger) and Deadeye (weapon crit) don't dispatch per-target here.
	}
}

void ALoopedCharacter::ApplyHitEffectsChain(AEnemyBase* Enemy, ULoopedGameInstance* GI)
{
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

void ALoopedCharacter::OnPlayerHitEnemy(AEnemyBase* Enemy)
{
	if (!Enemy || !Enemy->IsAlive()) return;

	ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	// Run relic "StaticCapacitor": every Nth landed hit discharges a free spark pulse around the
	// target. A relic, not a card — deliberately NOT silenced by the Static curse below.
	if (GI->HasRunArtifact(TEXT("StaticCapacitor")))
	{
		if (++CapacitorHitCounter >= FMath::Max(1, CapacitorHitInterval))
		{
			CapacitorHitCounter = 0;
			Enemy->ApplyChainSparkEffect(CapacitorPulseDamage, CapacitorPulseRadius);
			UE_LOG(LogLoopedCore, Display, TEXT("[Relic] Static Capacitor discharged (%.0f dmg, %.0f radius)"), CapacitorPulseDamage, CapacitorPulseRadius);
		}
	}

	// Curse "Static": card effects only fire on every other landed hit — off-beat hits fizzle.
	if (GI->HasCurse(TEXT("Static")) && (++StaticCurseHitCounter % 2 == 0))
	{
		return;
	}

	ApplyHitEffectsChain(Enemy, GI);

	// Echo card: every Nth hit re-triggers the whole effect chain once, so every proc lands twice.
	if (const FPassiveCardLevel* EchoLv = GetLevelData(GI, TEXT("Echo")))
	{
		if (EchoLv->EchoInterval > 0 && ++EchoHitCounter >= EchoLv->EchoInterval)
		{
			EchoHitCounter = 0;
			ApplyHitEffectsChain(Enemy, GI);
			UE_LOG(LogLoopedCore, Display, TEXT("[Card] Echo re-triggered the effect chain"));
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

			// Beat 2, after the lore lands: SAY what it means, THEN open the way home — the portal
			// appearing is now a consequence the player just read about, not an unexplained pop.
			GetWorldTimerManager().SetTimer(SecretWinTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				ShowCenterMessage(FText::FromString(TEXT("SECRET CLAIMED — THE RUN IS WON.\nThe way home has opened.")), 5.0f);
				if (ALoopedRunGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALoopedRunGameMode>() : nullptr)
				{
					GM->NotifyRunWon();
				}
			}), 2.2f, false);
			return;
		}
	}
}

void ALoopedCharacter::UpdateDimmedEffect(float DeltaSeconds)
{
	// Cheap 3Hz poll: while the "Dimmed" curse is active the world drops to a fraction of its
	// brightness via a camera exposure override. UMG (monitor / dialogues / shop / HP bar) renders
	// after post-processing so it stays fully readable; emissive markers glow THROUGH the dark.
	DimmedCheckAccum += DeltaSeconds;
	if (DimmedCheckAccum < 0.33f) return;
	DimmedCheckAccum = 0.0f;

	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();
	const bool bShouldDim = GI && GI->HasCurse(TEXT("Dimmed"));
	if (bShouldDim == bDimmedActive || !FirstPersonCamera) return;

	bDimmedActive = bShouldDim;
	FirstPersonCamera->PostProcessSettings.bOverride_AutoExposureBias = bShouldDim;
	FirstPersonCamera->PostProcessSettings.AutoExposureBias = DimmedExposureBias;
	UE_LOG(LogLoopedCore, Display, TEXT("[Curse] Dimmed %s (exposure bias %.1f)"),
		bShouldDim ? TEXT("ON — the world darkens") : TEXT("OFF"), bShouldDim ? DimmedExposureBias : 0.0f);
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

	// Stack down from the top-anchored widget: claim the LOWEST free row, release exactly it on
	// expiry. Guarantees two live messages can never share a row (the old counter could).
	int32 Slot = 0;
	while (OccupiedCenterSlots.Contains(Slot)) { Slot++; }
	OccupiedCenterSlots.Add(Slot);
	Msg->SetRenderTranslation(FVector2D(0.0f, (float)Slot * 36.0f)); // rows sized for the 26pt font

	// Transient — auto-remove after Duration (matches the parkour/unlock message pattern).
	TWeakObjectPtr<UUserWidget> WeakMsg(Msg);
	TWeakObjectPtr<ALoopedCharacter> WeakThis(this);
	FTimerHandle TH;
	GetWorldTimerManager().SetTimer(TH, FTimerDelegate::CreateLambda([WeakMsg, WeakThis, Slot]()
	{
		if (WeakMsg.IsValid()) WeakMsg->RemoveFromParent();
		if (WeakThis.IsValid()) WeakThis->OccupiedCenterSlots.Remove(Slot);
	}), Duration, false);
}

void ALoopedCharacter::ApplyMovementMods()
{
	const ULoopedGameInstance* GI = GetGameInstance<ULoopedGameInstance>();

	// Speed/Gravity values now come from the equipped cards' per-level row data (Brittle-adjusted).
	float SpeedMul = 1.0f;
	if (const FPassiveCardLevel* SpeedLv = GetLevelData(GI, TEXT("Speed"))) SpeedMul += SpeedLv->MoveSpeedBonus;
	if (GI) SpeedMul += GI->GetArtifactSpeedBonus(); // run relics (additive fraction)
	SpeedMul += MomentumSpeedBonus;                  // card "Momentum": active kill-burst

	// Wing artifact lowers the STARTING base gravity; the Gravity perk multiplies that base.
	const float BaseGravity = (GI && GI->HasArtifact(TEXT("Wing"))) ? WingGravityBase : 1.0f;
	float GravityMul = 1.0f;
	if (const FPassiveCardLevel* GravLv = GetLevelData(GI, TEXT("Gravity"))) GravityMul = GravLv->GravityScale;

	// Curse "Leaden": temporal drag — slower and heavier for the run (Iron Will softens both).
	float CurseGravityMul = 1.0f;
	if (GI && GI->HasCurse(TEXT("Leaden")))
	{
		SpeedMul *= GI->ScaleCurseMult(GI->CurseLeadenSpeedMult);
		CurseGravityMul = GI->ScaleCurseMult(GI->CurseLeadenGravityMult);
	}

	const float ArtifactGravityMul = GI ? GI->GetArtifactGravityMult() : 1.0f; // run relics (Featherweight 0.85)
	const float NewWalk = BaseWalkSpeed * SpeedMul * StatusSlowMultiplier; // ice slow folds in here
	const float FinalGravity = BaseGravity * GravityMul * CurseGravityMul * ArtifactGravityMul;
	GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? (NewWalk * SprintSpeedMultiplier) : NewWalk;
	GetCharacterMovement()->GravityScale = FinalGravity;

	UE_LOG(LogLoopedCore, Display, TEXT("Movement mods applied: speed=%.0f (mul %.2f) gravity_scale=%.2f"),
		NewWalk, SpeedMul, FinalGravity);
}

void ALoopedCharacter::ApplyElementalStatus(FName StatusEffect, float Magnitude, float Duration)
{
	if (StatusEffect.IsNone() || Duration <= 0.0f || !IsAlive())
	{
		return;
	}

	if (StatusEffect == TEXT("Slow"))
	{
		// Magnitude = speed multiplier while chilled (e.g. 0.55). Clamp so data can't fully root the player.
		const bool bWasSlowed = StatusSlowMultiplier < 1.0f;
		StatusSlowMultiplier = FMath::Clamp(Magnitude, 0.25f, 1.0f);
		ApplyMovementMods();
		GetWorldTimerManager().SetTimer(StatusSlowTimerHandle, this, &ALoopedCharacter::EndStatusSlow, Duration, false);
		if (!bWasSlowed)
		{
			ShowCenterMessage(FText::FromString(TEXT("CHILLED — movement slowed!")), 1.2f);
		}
		UE_LOG(LogLoopedCore, Display, TEXT("Status SLOW applied: x%.2f for %.1fs"), StatusSlowMultiplier, Duration);
	}
	else if (StatusEffect == TEXT("Burn") || StatusEffect == TEXT("Poison") || StatusEffect == TEXT("Venom"))
	{
		// Magnitude = damage per 1s tick. Re-application refreshes the clock, never stacks.
		const bool bWasBurning = StatusBurnTicksRemaining > 0;
		StatusBurnDamagePerTick = Magnitude;
		StatusBurnTicksRemaining = FMath::Max(1, FMath::CeilToInt(Duration));
		GetWorldTimerManager().SetTimer(StatusBurnTimerHandle, this, &ALoopedCharacter::StatusBurnTick, 1.0f, true);
		if (!bWasBurning)
		{
			const bool bPoison = (StatusEffect != TEXT("Burn"));
			ShowCenterMessage(FText::FromString(bPoison ? TEXT("POISONED!") : TEXT("BURNING!")), 1.2f);
		}
		UE_LOG(LogLoopedCore, Display, TEXT("Status %s applied: %.1f dmg/s x%d ticks"), *StatusEffect.ToString(), Magnitude, StatusBurnTicksRemaining);
	}
	else if (StatusEffect == TEXT("Weaken"))
	{
		// Magnitude = extra damage taken (0.3 = +30%). Void amplifies all incoming damage for a
		// duration; clamp so data can't double you up beyond +100%. Re-application refreshes the clock.
		const bool bWasWeak = StatusWeakenMultiplier > 1.0f;
		StatusWeakenMultiplier = 1.0f + FMath::Clamp(Magnitude, 0.0f, 1.0f);
		GetWorldTimerManager().SetTimer(StatusWeakenTimerHandle, this, &ALoopedCharacter::EndStatusWeaken, Duration, false);
		if (!bWasWeak)
		{
			ShowCenterMessage(FText::FromString(TEXT("WEAKENED — you take more damage!")), 1.2f);
		}
		UE_LOG(LogLoopedCore, Display, TEXT("Status WEAKEN applied: x%.2f for %.1fs"), StatusWeakenMultiplier, Duration);
	}
}

void ALoopedCharacter::EndStatusSlow()
{
	StatusSlowMultiplier = 1.0f;
	ApplyMovementMods();
}

void ALoopedCharacter::EndStatusWeaken()
{
	StatusWeakenMultiplier = 1.0f;
}

void ALoopedCharacter::StatusBurnTick()
{
	if (StatusBurnTicksRemaining <= 0 || !IsAlive())
	{
		GetWorldTimerManager().ClearTimer(StatusBurnTimerHandle);
		StatusBurnTicksRemaining = 0;
		return;
	}
	StatusBurnTicksRemaining--;
	// Routed through the normal damage path so HUD/death/run-state sync all behave.
	TakeDamageFromEnemy(StatusBurnDamagePerTick);
	if (StatusBurnTicksRemaining <= 0)
	{
		GetWorldTimerManager().ClearTimer(StatusBurnTimerHandle);
	}
}
