#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CompanionCage.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWidgetComponent;
class UMaterialInstanceDynamic;

// The captive companion at the center of a rescue mission (see looped_rescue_system.md).
// A ring of bars holds a glowing placeholder figure; when every enemy in the level is dead
// (the Warden falls), the cage opens: the companion RELIC is granted permanently, the beat
// plays, and the exit portal home reveals. Placeholder visuals — Sahar supplies the real
// companion model later (like Vorr / the First Hunter).
UCLASS(Blueprintable)
class LOOPED_API ACompanionCage : public AActor
{
	GENERATED_BODY()

public:
	ACompanionCage();

	// DT_Artifacts RowName of the companion relic granted on rescue (also the is-rescued flag).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FName CompanionArtifact = FName(TEXT("Lysa"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	FText CompanionDisplayName;

	// Center-screen beat when the cage opens.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue", meta = (MultiLine = true))
	FText FreedMessage;

	// Rate-limited line when the player approaches the still-locked cage.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue", meta = (MultiLine = true))
	FText CaptiveMessage;

	// Tint for the companion placeholder + glow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue|FX")
	FLinearColor CompanionColor = FLinearColor(0.2f, 0.9f, 1.0f, 1.0f);

	// Seconds between the freed beat and the exit portals revealing (cause before exit).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	float PortalRevealDelay = 2.0f;

	// TRUE (default): combat rescue — the cage opens when every enemy is dead (Lysa/Warden).
	// FALSE: puzzle rescue — only an external OpenCage() call opens it (Brann's forge), and the
	// no-enemies failsafe stays out of the way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	bool bOpenWhenEnemiesDead = true;

	// TRUE (default): opening also reveals the level's exit portals after PortalRevealDelay.
	// FALSE: a scripted flow owns the portals instead (the tutorial director gates the exit
	// behind the full monitor/card teach, not the cage-open).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rescue")
	bool bRevealPortalsOnOpen = true;

	// Opens the cage + grants the relic. Public so scripted flows can force it if ever needed.
	UFUNCTION(BlueprintCallable, Category = "Rescue")
	void OpenCage();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	// Greeting/approach trigger (does NOT open the cage — enemies do).
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Trigger;

	// Glowing placeholder figure inside the bars.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CompanionMesh;

	// The bar ring (built in the ctor). Hidden + de-collided when the cage opens.
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UStaticMeshComponent>> CageBars;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> GlowLight;

	// "LYSA — captive" / "LYSA — FREED" floating tag.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWidgetComponent> NameTagComp;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	// Bound to the GameMode's per-enemy-death broadcast; opens when no living enemy remains.
	UFUNCTION()
	void HandleEnemyDied();

private:
	void SetNameTag(const FString& Text, const FLinearColor& Color);
	void RevealExitPortals();

	// Failsafe: if the level somehow has no enemies, open anyway — never strand the player.
	void FailsafeCheck();

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CompanionMID;

	bool bOpened = false;
	double LastCaptiveMessageTime = -100.0;
	FTimerHandle PortalTimerHandle;
	FTimerHandle FailsafeTimerHandle;
};
