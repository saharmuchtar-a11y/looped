#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortalActor.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UWidgetComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;

// How a portal decides its destination.
//   Fixed    — use TargetLevelName (default; preserves every placed actor + the boss→Hub portal).
//   StartRun — query the GameInstance for the FIRST room of the freshly generated run path.
//   NextRoom — query the GameInstance to ADVANCE one step along the run path.
UENUM(BlueprintType)
enum class ERoutePortalMode : uint8
{
	Fixed    UMETA(DisplayName = "Fixed (TargetLevelName)"),
	StartRun UMETA(DisplayName = "Start Run (first generated room)"),
	NextRoom UMETA(DisplayName = "Next Room (advance run path)")
};

UCLASS(Blueprintable)
class LOOPED_API APortalActor : public AActor
{
	GENERATED_BODY()

public:
	APortalActor();

	// Used only when Mode == Fixed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FName TargetLevelName;

	// Default Fixed = 100% backward compatible for already-placed portals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	ERoutePortalMode Mode = ERoutePortalMode::Fixed;

	// When TRUE the portal spawns hidden + non-interactive and only appears once ActivatePortal()
	// is called (e.g. after a room's card draft). Leave FALSE for always-on portals such as the
	// Hub's Start-Run portal. Place a disabled NextRoom portal in each run room, exactly where you
	// want the exit to appear, and it stays out of sight until the room is cleared.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bStartDisabled = false;

	// --- Portal FX (particles + light), baked into the portal so they hide/reveal WITH it ---
	// The swirl particle system drawn at the portal. Defaults to NS_PortalSwirl (set in ctor); swap freely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|FX")
	UNiagaraSystem* PortalFXSystem = nullptr;

	// Colour of the portal glow light (editor-tunable so we can retint without recompiling).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|FX")
	FLinearColor PortalLightColor = FLinearColor(0.0f, 0.8f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|FX")
	float PortalLightIntensity = 1100.0f;

	// Local offset for the FX (particles + light) — nudge it toward the wall / up. -X = toward wall.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal|FX")
	FVector FXLocalOffset = FVector(-25.0f, 0.0f, 90.0f);

	// Reveal + enable a disabled portal. Safe to call repeatedly. Call when the room clears.
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void ActivatePortal();

	// Assign this portal a fork room TYPE (a DT_RoomTypes RowName), set its hovering label from that
	// row's DisplayLabel, and reveal it. On overlap a typed portal loads a level of that type via
	// ULoopedGameInstance::EnterRoomType (instead of the fixed Mode behaviour).
	UFUNCTION(BlueprintCallable, Category = "Portal")
	void SetForkType(FName InRoomTypeId);

	// The fork type assigned at runtime (None = use the editor Mode behaviour). Not edited in-editor.
	UPROPERTY(BlueprintReadOnly, Category = "Portal")
	FName RoomTypeId = NAME_None;

protected:
	virtual void BeginPlay() override;

	// Toggles mesh visibility + trigger collision together. Used by BeginPlay + ActivatePortal.
	void SetPortalEnabled(bool bEnabled);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> TriggerBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> PortalMesh;

	// Floating type label ("Fight" / "Merchant" / "?") shown above a fork portal. Screen-space so it
	// always faces the camera. Hidden until SetForkType assigns a label.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> LabelComp;

	// Inward-swirl particles drawn at the portal. Toggled with the portal in SetPortalEnabled.
	UPROPERTY(VisibleAnywhere, Category = "Portal|FX")
	TObjectPtr<UNiagaraComponent> PortalFX;

	// Coloured glow. Toggled with the portal in SetPortalEnabled.
	UPROPERTY(VisibleAnywhere, Category = "Portal|FX")
	TObjectPtr<UPointLightComponent> PortalLight;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	// Seconds to fade to black before the level swap. Tuned to comfortably cover the OpenLevel hitch.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	float FadeOutDuration = 0.35f;

	// Played the moment the player commits to travel (loaded by path in the ctor).
	UPROPERTY()
	TObjectPtr<class USoundBase> PortalTravelSound;

	// Seconds before the level swaps. Kept == the fade (snappy cut, like before) — Sahar prefers the
	// quick transition over holding black to finish the whoosh, so the portal sound just tails off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	float TravelDelay = 0.35f;

private:
	// Resolved once when the fade starts, then traveled to when the fade completes.
	FName PendingDestination = NAME_None;
	// Guards against the overlap firing twice mid-fade (which would double-advance the run path).
	bool bTraveling = false;
	FTimerHandle TravelTimerHandle;

	// Single commit-to-travel point (fork + fixed/hub portals): plays the portal SFX once, fades, then
	// schedules DoTravel. Keeps the exit sound from being duplicated across the two overlap branches.
	void BeginTravel(FName Destination);

	// Fires after the fade-out completes — performs the actual OpenLevel.
	void DoTravel();
};
