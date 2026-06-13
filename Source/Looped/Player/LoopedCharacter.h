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
class AEnemyBase;
class UStaticMeshComponent;
class UStaticMesh;
class UAnimSequence;
class UWidgetComponent;

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

	// Pre-formatted "Current / Max" HP for a HUD text block (e.g. "75 / 125"), rounded to ints.
	// Reads the same live pawn values as GetPOCHealthPercent, so the number and the bar always agree.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Health")
	FText GetHealthText() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POC")
	float POCMaxHealth = 100.0f;

	UPROPERTY(BlueprintReadOnly, Category = "POC")
	float POCCurrentHealth = 100.0f;

	// Death screen ("back to square one...") shown for DeathScreenDuration before OnPlayerDied broadcasts.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	TSubclassOf<class UUserWidget> DeathScreenClass;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	float DeathScreenDuration = 2.5f;

	// Death cam: how long the 3rd-person "you're dead on the floor" shot holds before traveling to
	// the Hub. The OnPlayerDied broadcast (which the BP uses to OpenLevel) is delayed by this.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	float DeathCamDuration = 2.6f;

	// How long the camera takes to glide from the first-person view up/away to the death shot
	// (instead of snapping). Should be < DeathCamDuration.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|UI")
	float DeathCamMoveDuration = 0.8f;

	// --- Perk mirror ints + tuning caps REMOVED (data consolidation). Levels live in the
	//     GameInstance RunDeck; per-level values + caps come from the FPassiveCardData rows. ---

	// Re-derives POCMaxHealth from the equipped MaxHP card's row + permanent bonus.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	void ApplyMaxHPMod();

	// Bump a perk's level (returns new level). Called from BP card-pick flow.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	int32 IncrementPerkLevel(FName PerkName);

	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	int32 GetPerkLevel(FName PerkName) const;

	// True when the perk's level has hit its cap. Use for card-pool filtering.
	UFUNCTION(BlueprintPure, Category = "LOOPED|Perks")
	bool IsPerkAtMax(FName PerkName) const;

	// Returns InAll minus any rows whose perk is at max level.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	TArray<FName> GetEligibleCards(const TArray<FName>& InAll) const;

	// Wipe all perk levels — call from death / new-run flow.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	void ResetPerks();

	// Applies every leveled per-hit perk to a hit enemy. Replaces the BP burn→venom→spark→lifesteal branch chain.
	// If ChainSpark is equipped, the chained targets also receive burn/venom/lifesteal.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	void OnPlayerHitEnemy(AEnemyBase* Enemy);

private:
	// Applies burn / venom / lifesteal heal to a single target. Does NOT recurse into chain.
	void ApplyEquippedEffectsTo(AEnemyBase* Target);

public:

	// Re-derive walk speed + gravity scale from Speed/Gravity levels. Call from BP after card pick.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Perks")
	void ApplyMovementMods();

	// Applies an elemental status from an enemy hit (DT_ProjectileElements row fields).
	// "Slow" (Ice): walk speed * Magnitude for Duration seconds — folds into ApplyMovementMods so it
	// composes with Speed cards / sprint / curses. "Burn" (Fire) / "Poison" (Venom): Magnitude damage
	// per second for Duration seconds. Re-application refreshes (no stacking). Unknown names no-op.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Status")
	void ApplyElementalStatus(FName StatusEffect, float Magnitude, float Duration);

	// Create/refresh the on-screen owned-artifacts list (top-left HUD). Safe to call repeatedly.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void UpdateArtifactHUD();

	// --- Arm-monitor dashboard ---
	// State 2 (Reward Draft): a reward manager / chest calls this to open the monitor with the
	// Center (card-draft) panel populated. Slow-mo + free cursor, same as State 1.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void OpenHologramForReward();

	// Mount the card-draft widget INSIDE the monitor's CenterPanel (instead of a separate
	// centered viewport overlay), so the cards live in this layout and scale with it. Opens the
	// monitor in reward mode if needed, hides the center placeholder, and fills the panel slot.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void MountCardWidgetInMonitor(class UUserWidget* CardWidget);

	// Dialogue takes over the WHOLE monitor: opens it (slow-mo, cursor, no-damage shield), hides
	// every dashboard zone (deck/relics/curses/artifacts/currency/logo), and fills CenterPanel
	// with the dialogue widget — events read as the Arm Monitor analyzing the situation.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void MountDialogueInMonitor(class UUserWidget* DialogueWidget);

	// Removes the dialogue, restores the dashboard zones, and closes the monitor.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void UnmountDialogueFromMonitor(class UUserWidget* DialogueWidget);

	// Close the monitor: hide it, restore 1.0 time, re-lock the cursor to game.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void CloseHologram();

	// Push live GameInstance data (currency / stats / deck / relics / curses) into the monitor's
	// text blocks. Called on open; also Blueprint-callable for the card-draft flow.
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void RefreshDashboard();

	// Touch radius (uu) for the secret Time-Sphere artifact grant.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float SecretSphereTouchRadius = 160.0f;

	// Artifact the secret Time Sphere grants (the first artifact = "Wing").
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	FName SecretSphereArtifact = TEXT("Wing");

	// Story message shown center-screen on the secret sphere grant. Set in the constructor;
	// editable per-instance so the wording can be tuned without a recompile.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts", meta = (MultiLine = true))
	FText SecretSphereMessage;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float SecretSphereMessageDuration = 6.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnJumped_Implementation() override; // plays the jump SFX

	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	// Visible held weapon, attached to the hero hand socket. The mesh itself is data-driven —
	// the WeaponHolder applies FWeaponData.WeaponMesh on equip (none = hidden). Replaces the old
	// hardcoded POC branch hand-mesh.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMeshComp;

	// Socket/bone on the hero mesh the weapon attaches to. Default = Manny right hand bone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Weapon")
	FName WeaponAttachSocket = FName("hand_r");

	// Held-weapon size in WORLD units. Hand-bone scale varies wildly between skeletons (Manny bones
	// are scale 1; the Wanderer's Mixamo rig carries micro-scale bones its anims compensate for), so
	// a bone-relative scale is not portable — this is applied via SetWorldScale3D after attach.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Weapon")
	FVector WeaponWorldScale = FVector(0.3375f, 0.4f, 0.4f);

	// Grip offset from the hand joint, in WORLD units along the hand's axes (rotates with the hand).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Weapon")
	FVector WeaponGripOffset = FVector(12.0f, 10.6f, 26.6f);

public:
	// Show/clear the held weapon mesh. Called by the WeaponHolder on equip (nullptr hides it).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|Weapon")
	void SetWeaponVisualMesh(UStaticMesh* NewMesh);

private:
	// Applies WeaponWorldScale + WeaponGripOffset against the LIVE socket transform. Runs at equip
	// and again shortly after (the first frames may still be in the pre-anim reference pose, whose
	// bone scale differs on the Wanderer rig).
	void NormalizeWeaponTransform();
	FTimerHandle WeaponNormalizeTimerHandle;

protected:



	// Unarmed attack anims (Manny). Picked randomly on fire.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Anim")
	TArray<TObjectPtr<UAnimSequence>> AttackAnims;

	// Played when the player takes damage (loaded by path in the ctor).
	UPROPERTY()
	TObjectPtr<class USoundBase> PlayerHurtSound;

	// Played on jump (loaded by path in the ctor).
	UPROPERTY()
	TObjectPtr<class USoundBase> JumpSound;

public:
	// Add a brief positional camera shake (the no-freeze impact "punch"). Intensity ~0.6 = a melee
	// hit-landed micro-punch; ~1.2 = taking damage. Decays in Tick. Works with the FP control-rotation
	// camera because it jitters the camera's relative LOCATION (rotation is control-driven).
	void AddCameraShake(float Intensity);

protected:
	float CameraShakeAmount = 0.0f;
	FVector BaseCameraRelLoc = FVector(0.0f, 0.0f, 60.0f);

	UFUNCTION(BlueprintCallable, Category = "LOOPED|Anim")
	void PlayRandomAttackAnim();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<ULoopedAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPassiveStackComponent> PassiveStack;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWeaponHolderComponent> WeaponHolder;

	// Arm-monitor dashboard — a VIEWPORT widget (created on first open; rock-solid for a full
	// menu). Replaces all UI except the HP bar (currency / stats / deck / relics / curses /
	// card-draft). Opens on Middle-Mouse (State 1) and on card reward (State 2). Class auto-linked
	// in the ctor.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class UUserWidget> WristMenuClass;

	UPROPERTY()
	TObjectPtr<class UUserWidget> WristMenuWidget;

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

	// Hold to enter manual bullet-time (world slows, player stays full speed). Bound to MiddleMouseButton.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SloMoAction;

	// Movement params
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float BaseWalkSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeedMultiplier = 1.43f;

	// Wing artifact: starting base gravity scale (vs 1.0 default). Lower = lighter from the
	// start, and since gravity perks multiply this base, the perks become stronger too.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float WingGravityBase = 0.85f;

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
	// Middle-Mouse press toggles the arm monitor (State 1, manual analysis — no card draft).
	void ToggleHologramMenu();
	// Shared open path; slows time via SloMoManager + frees the cursor. bRewardMode = State 2
	// (Center card-draft panel visible).
	void OpenHologram(bool bRewardMode);

	void HandleHealthChanged(float NewHealth, float MaxHealth);

	// Write-through: mirror POCCurrentHealth/POCMaxHealth onto the GameInstance run state so
	// the player's health survives hard OpenLevel travel. No-op in the Hub. Called on every
	// health mutation (damage / heal / max-HP change).
	void SyncHealthToRunState();

	void HandleDeathTimerExpired();

	// Death cam: ragdoll the hero + detach the camera, then glide it to a 3rd-person shot of the body.
	void EnterDeathCam();
	// Fires OnPlayerDied (BP OpenLevel→Hub) after the death cam has held for DeathCamDuration.
	void FinishDeathAndTravel();
	FTimerHandle DeathTravelTimerHandle;

	// Death-cam camera glide state (driven in Tick): from the FP transform to the overhead shot.
	bool bInDeathCam = false;
	bool bDeathCamMoving = false;
	float DeathCamMoveElapsed = 0.0f;
	FVector DeathCamStartLoc = FVector::ZeroVector;
	FRotator DeathCamStartRot = FRotator::ZeroRotator;
	FVector DeathCamTargetLoc = FVector::ZeroVector;
	FRotator DeathCamTargetRot = FRotator::ZeroRotator;

	// Re-shows the death screen for whatever time remains on the GameInstance death timer.
	// Called from BeginPlay (so hub re-creates the widget) and from the death path.
	void ShowDeathScreenIfActive();

	// Secret chain link 3: polls proximity to this level's cached Time Spheres and, once the
	// gate is open (Gravity ever maxed), grants the secret artifact on touch (one-time).
	void CheckSecretSpheres(float DeltaSeconds);

	// How many center messages are currently on screen — used to stack them vertically so multiple
	// rapid messages (e.g. dialogue outcomes) don't draw on top of each other.
	int32 ActiveCenterMessageCount = 0;

	// Show a transient centered story message via WBP_CenterMessage (auto-removed after Duration).
	// Public so external systems (e.g. dialogue events) can surface "what happened" text.
public:
	void ShowCenterMessage(const FText& Message, float Duration);
private:

	UPROPERTY()
	TObjectPtr<ULoopedAttributeSet> AttributeSet;

	UPROPERTY()
	TObjectPtr<UUserWidget> DeathScreenWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> ArtifactHUDWidget;

	FTimerHandle DeathTimerHandle;

	bool bIsSprinting = false;
	bool bHologramOpen = false; // arm-monitor open state (drives the Middle-Mouse toggle)

	// --- Secret Time-Sphere state (built fresh each level in BeginPlay) ---
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SecretSpheres;

	bool bInHubLevel = false;
	float SecretSphereCheckAccum = 0.0f;
	// Delays the "run won" beat after the sphere-touch lore so cause precedes the portal.
	FTimerHandle SecretWinTimerHandle;

	// Curse "Decay": accumulates fractional HP loss so we apply it in ~1 HP chunks (less log spam).
	float DecayAccum = 0.0f;

	// --- Curse "Dimmed": the world goes dark (camera exposure drop). UI is UMG = unaffected;
	// emissive markers/lights stand out MORE in the gloom (intended — they guide you through it).
public:
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Curses")
	float DimmedExposureBias = -1.5f; // ~65% darker; -1.0 = half brightness
private:
	float DimmedCheckAccum = 0.0f;
	bool bDimmedActive = false;
	void UpdateDimmedEffect(float DeltaSeconds);

	// --- Elemental statuses (from enemy projectiles, DT_ProjectileElements rows) ---
	// Ice slow: multiplies into ApplyMovementMods' walk speed (1.0 = no slow). Timer restores it.
	float StatusSlowMultiplier = 1.0f;
	FTimerHandle StatusSlowTimerHandle;
	void EndStatusSlow();

	// Fire burn / venom poison: flat damage per 1s tick. Re-application refreshes the tick count.
	float StatusBurnDamagePerTick = 0.0f;
	int32 StatusBurnTicksRemaining = 0;
	FTimerHandle StatusBurnTimerHandle;
	void StatusBurnTick();
};
