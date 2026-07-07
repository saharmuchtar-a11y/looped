#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/LoopedInteractable.h"
#include "GuideEntity.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UUserWidget;

// "The First Hunter" — a faded Hunter who has watched every loop; the Hub's living game index.
// Appears once the player proves they persist (same gate as Vorr's vault: first boss kill).
// Walk up to him and a BOOK opens: Cards / Enemies / Curses / Blessings / Relics — every page
// generated from the live data tables, so the guide can never go stale.
UCLASS(Blueprintable)
class LOOPED_API AGuideEntity : public AActor, public ILoopedInteractable
{
	GENERATED_BODY()

public:
	AGuideEntity();

	// Press-E to open the logbook — walking past no longer pops it.
	virtual void Interact(class ALoopedCharacter* Player) override;
	virtual float GetInteractRange() const override { return TriggerRadius + 40.0f; }
	virtual FText GetInteractPrompt() const override
	{
		return bOpen ? FText::GetEmpty() : FText::FromString(TEXT("read the logbook"));
	}

	// Display name shown as the book's author line. Lore-derived; rename freely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guide")
	FText EntityName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guide")
	float TriggerRadius = 240.0f;

	// The Hunter is at the Hub from the START (Sahar's call) — the codex itself does the gating
	// (locked "--------" rows until each thing is encountered). Flip on to hide him behind the
	// first boss kill instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guide")
	bool bRequireVaultUnlock = false;

	// The Hunter turns (yaw only) to watch the player — same behavior as Vorr.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guide")
	bool bFacePlayer = true;

	// Degrees added so the model's FRONT lines up (Meshy exports tend to need 270, like Vorr).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Guide")
	float FaceYawOffset = 270.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Guide")
	USceneComponent* SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Guide")
	USphereComponent* Trigger;

	UPROPERTY(VisibleAnywhere, Category = "Guide")
	UStaticMeshComponent* BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Guide")
	UPointLightComponent* GlowLight;

	// Floating "THE FIRST HUNTER / Logbook" tag above his head (screen-space, Vorr-style).
	UPROPERTY(VisibleAnywhere, Category = "Guide")
	class UWidgetComponent* NameTagComp;

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	void OpenGuide();
	UFUNCTION() void CloseGuide();

	// Index buttons (fixed names in WBP_Guide, same pattern as the dialogue choice buttons).
	UFUNCTION() void OnDevWipeSave();
	UFUNCTION() void OnPageCards();
	UFUNCTION() void OnPageEnemies();
	UFUNCTION() void OnPageCurses();
	UFUNCTION() void OnPageBlessings();
	UFUNCTION() void OnPageRelics();

	void ShowPage(int32 PageIndex);
	FString BuildPage(int32 PageIndex) const;

	UPROPERTY()
	UUserWidget* GuideWidget = nullptr;

	bool bOpen = false;
};
