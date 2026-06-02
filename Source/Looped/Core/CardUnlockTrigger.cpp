#include "CardUnlockTrigger.h"
#include "Components/BoxComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Core/LoopedGameInstance.h"
#include "Looped.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ACardUnlockTrigger::ACardUnlockTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	Trigger->SetBoxExtent(FVector(160.0f, 160.0f, 100.0f));
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	RootComponent = Trigger;

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ACardUnlockTrigger::OnOverlapBegin);

	static ConstructorHelpers::FClassFinder<UUserWidget> MsgWidget(TEXT("/Game/UI/WBP_CenterMessage"));
	if (MsgWidget.Succeeded())
	{
		MessageWidgetClass = MsgWidget.Class;
	}
}

void ACardUnlockTrigger::OnOverlapBegin(UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (bFired || !OtherActor) return;
	if (CardToUnlock.IsNone() && ArtifactToGrant.IsNone()) return;

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled()) return;

	UWorld* World = GetWorld();
	if (!World) return;
	ULoopedGameInstance* GI = World->GetGameInstance<ULoopedGameInstance>();
	if (!GI) return;

	// Optional gate: stay inert until the required perk has been maxed (don't consume the trigger).
	if (!RequiredMaxedPerk.IsNone() && !GI->HasPerkEverMaxed(RequiredMaxedPerk))
	{
		return;
	}

	// Grant card and/or artifact. Track whether anything was NEW (for the one-time message).
	bool bSomethingNew = false;
	if (!CardToUnlock.IsNone())
	{
		if (!GI->IsCardUnlocked(CardToUnlock)) bSomethingNew = true;
		GI->UnlockCard(CardToUnlock); // idempotent
	}
	if (!ArtifactToGrant.IsNone())
	{
		if (!GI->HasArtifact(ArtifactToGrant)) bSomethingNew = true;
		GI->GrantArtifact(ArtifactToGrant); // idempotent
	}
	bFired = true;
	UE_LOG(LogLoopedCore, Display, TEXT("[Trigger] fired — card='%s' artifact='%s' new=%s"),
		*CardToUnlock.ToString(), *ArtifactToGrant.ToString(), bSomethingNew ? TEXT("yes") : TEXT("no"));

	if (!bSomethingNew || UnlockMessage.IsEmpty() || !MessageWidgetClass) return;

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return;
	UUserWidget* Msg = CreateWidget<UUserWidget>(PC, MessageWidgetClass);
	if (!Msg) return;
	if (UTextBlock* T = Cast<UTextBlock>(Msg->GetWidgetFromName(TEXT("MessageText"))))
	{
		T->SetText(UnlockMessage);
	}
	Msg->AddToViewport(200);

	// Auto-remove after MessageDuration (death-screen-style transient message).
	TWeakObjectPtr<UUserWidget> WeakMsg(Msg);
	FTimerHandle TimerHandle;
	World->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([WeakMsg]()
	{
		if (WeakMsg.IsValid())
		{
			WeakMsg->RemoveFromParent();
		}
	}), MessageDuration, false);
}
