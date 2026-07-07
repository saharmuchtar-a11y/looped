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
	// Applies burn / venom / lifesteal heal / cryo to a single target. Does NOT recurse into chain.
	void ApplyEquippedEffectsTo(AEnemyBase* Target);

	// One full pass of the per-hit effect chain: equipped effects on the target + the ChainSpark
	// propagation. Extracted so the Echo card can re-run the whole chain on its Nth hit.
	void ApplyHitEffectsChain(AEnemyBase* Enemy, class ULoopedGameInstance* GI);

	// --- Per-hit counters (pawn-local; the pawn is rebuilt each room, so they reset per room) ---
	int32 EchoHitCounter = 0;        // Echo card — counts hits toward the next chain re-trigger
	int32 StaticCurseHitCounter = 0; // "Static" curse — odd hits fire card effects, even hits fizzle
	int32 CapacitorHitCounter = 0;   // StaticCapacitor relic — counts hits toward the next spark pulse

public:
	// Card "Momentum": kills grant a brief speed burst. The GameInstance kill hook calls this
	// with the card's Fraction; the bonus folds into ApplyMovementMods and a timer clears it.
	void TriggerMomentumBurst(float SpeedFraction);

	// Card "Momentum": burst duration in seconds (refreshed on every kill).
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Perks")
	float MomentumDuration = 3.0f;

	// Run relic "StaticCapacitor": every CapacitorHitInterval-th landed hit emits a free spark
	// pulse (reuses the enemy ChainSpark AoE). Tunables, no code change to rebalance.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	int32 CapacitorHitInterval = 8;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float CapacitorPulseDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float CapacitorPulseRadius = 500.0f;

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
	// Also wires Mira's REROLL button (shown only while her once-per-run token is unspent).
	UFUNCTION(BlueprintCallable, Category = "LOOPED|UI")
	void MountCardWidgetInMonitor(class UUserWidget* CardWidget);

	// Mira "Reroll": consume the token and re-run the manager's card roll for a fresh draft.
	UFUNCTION()
	void OnCardRerollClicked();

private:
	// The card widget currently mounted in the monitor (for the reroll handler).
	TWeakObjectPtr<class UUserWidget> MountedCardWidget;

public:

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

	// Fill the monitor's State-1 center with the guidance panel: the missions list plus the one
	// highest-priority hint pinned as a header (GI->EvaluateMissions() over DT_Missions). Rows
	// are runtime TextBlocks inside a private box parented into CenterPanel — rebuilt on every
	// dashboard open, so the numbers are always current. Only State 1 shows it: the card draft
	// and dialogue mounts call HideMissionsPanel() before they take the center.
	void RefreshMissionsPanel();

	// Collapse the missions box (card draft / dialogue owns the center, or monitor closing).
	void HideMissionsPanel();

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

	// --- Arm Monitor gate (tutorial arc) ---
	// The monitor is Orin's gift: until this artifact is owned, middle-click only nudges.
	// Granted by his tutorial rescue dialogue; legacy saves are migrated in LoadOrCreateStats
	// so existing players never lose the monitor. None = gate off.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Tutorial")
	FName MonitorGateArtifact = TEXT("Orin");

	// Tutorial director polls: chrono skill active / arm monitor open right now?
	bool IsChronoSkillActive() const { return bSkillActive; }
	bool IsHologramOpen() const { return bHologramOpen; }

	// Cap on live objectives listed in the monitor panel (Sahar: "too much text on the monitor").
	// Overflow collapses to a dim "+N more" line; the hint header doesn't count against it.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Monitor")
	int32 MaxVisibleObjectives = 3;

	// --- Hero visual kit (heronew Wasteland Wanderer; a DT_EnemyVisuals row, same system as
	// the enemies). None = keep the mannequin + AnimBP. Applied at BeginPlay: mesh swap,
	// single-node anims by velocity, weapon socket auto-detected on the new rig.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOOPED|Visual")
	FName HeroVisualRow = FName(TEXT("Hero"));

	// One-shot swing/charge on the hero body — WeaponHolder calls this on every shot, so the
	// player SEES the hit (melee = Attack anim, ranged = the rifle-charge in the Cast slot).
	void PlayHeroAttackAnim(bool bMelee);

private:
	void ApplyHeroVisual();
	void UpdateHeroAnim();
	void PlayHeroAnim(class UAnimSequence* Anim, bool bLoop);

	UPROPERTY() TObjectPtr<class UAnimSequence> HeroIdle;
	UPROPERTY() TObjectPtr<class UAnimSequence> HeroWalk;
	UPROPERTY() TObjectPtr<class UAnimSequence> HeroRun;
	UPROPERTY() TObjectPtr<class UAnimSequence> HeroAttack;
	UPROPERTY() TObjectPtr<class UAnimSequence> HeroCast;
	UPROPERTY() TObjectPtr<class UAnimSequence> HeroCurrentAnim;
	bool bHeroVisualDriven = false;
	double HeroAttackHoldUntil = 0.0;

public:

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnJumped_Implementation() override; // plays the jump SFX
	virtual void Landed(const FHitResult& Hit) override; // card "Shockwave": landing AoE

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

	// The missions/hints box built at runtime inside the monitor's CenterPanel (State 1 only).
	// Owned by the widget tree once parented; kept here so refreshes rebuild instead of stack.
	UPROPERTY()
	TObjectPtr<class UVerticalBox> MissionsBox;

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

	// Press-E interact (forge keyholes / levers — anything ILoopedInteractable). Loaded in the
	// ctor like SloMo (mapped to E in IMC_Default), so no per-Blueprint assignment is needed.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	// Q — the equipped SKILL (default: chrono dodge). Loaded in the ctor (mapped to Q in
	// IMC_Default). More skills later share this one key.
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> SkillAction;

	// --- Q chrono skill (the default skill): world slows, player slows LESS — dodge windows.
	// Gauge in seconds; drains 1:1 real-time while active, recharges SkillRechargePerSecond.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Skill")
	float SkillGaugeMaxBase = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Skill")
	float SkillRechargePerSecond = 0.2f; // 1s of use back per 5s idle (empty→full in 25s)

	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Skill")
	float SkillMinActivation = 0.5f; // don't allow sputtering activations on an empty gauge

	// Movement params
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float BaseWalkSpeed = 700.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeedMultiplier = 1.43f;

	// Wing artifact: starting base gravity scale (vs 1.0 default). Lower = lighter from the
	// start, and since gravity perks multiply this base, the perks become stronger too.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float WingGravityBase = 0.85f;

	// Brann "Forged Plate" (rescued-companion relic): all incoming damage is multiplied by this
	// while Brann is rescued. The ONLY flat damage-reduction in the game — keep it unique.
	UPROPERTY(EditDefaultsOnly, Category = "LOOPED|Artifacts")
	float BrannPlateDamageMult = 0.9f;

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
	// E press: find the nearest ILoopedInteractable in range and fire it (keyholes, levers).
	void TryInteract();

	// Shared scan for TryInteract + the proximity prompt: nearest implementer within its range.
	class ILoopedInteractable* FindBestInteractable() const;

	// Proximity prompt ("Press [E] to <verb>"): polled on a light Tick accumulator; shows a
	// low-center viewport widget while the nearest interactable offers a non-empty verb.
	void UpdateInteractPrompt(float DeltaSeconds);
	float InteractPromptAccum = 0.0f;

	UPROPERTY()
	TObjectPtr<UUserWidget> InteractPromptWidget;

	// --- Q chrono skill state ---
	// Q press: toggle. Tick drains/recharges in REAL seconds (dilation must not warp the math)
	// and mirrors the gauge to the run state so it travels between rooms like health.
	void OnSkillPressed();
	void StartChronoSkill();
	void EndChronoSkill();
	float GetSkillGaugeMax() const; // base + Lysa's permanent training ranks
	void UpdateSkillGaugeBar();     // lazy-creates WBP_SkillGauge, sets percent + fill color

	float SkillGauge = -1.0f;       // seeded from run state / max on BeginPlay
	bool bSkillActive = false;

	UPROPERTY()
	TObjectPtr<UUserWidget> SkillGaugeWidget;

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

	// The occupied message rows. Each live message claims the LOWEST free row and releases that
	// exact row on expiry — a plain counter overlapped when an older message outlived a newer one.
	TSet<int32> OccupiedCenterSlots;

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

	// Card "Momentum": active kill-burst speed bonus (additive fraction; 0 = none). Timer-cleared.
	float MomentumSpeedBonus = 0.0f;
	FTimerHandle MomentumTimerHandle;
	void EndMomentumBurst();

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

	// Void "Weaken": incoming damage is multiplied by this (1.0 = none) for a duration — the void
	// unravels your defenses. Applied in TakeDamageFromEnemy alongside the Frailty curse; the timer
	// restores it to 1.0. Re-application refreshes (no stacking).
	float StatusWeakenMultiplier = 1.0f;
	FTimerHandle StatusWeakenTimerHandle;
	void EndStatusWeaken();
};
