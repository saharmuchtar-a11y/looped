#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoopedPauseMenuWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UButton;

// Code-only pause menu: built entirely in RebuildWidget() so it needs no WBP asset and ships
// with the C++ module. Two pages (Main / Settings) toggled by visibility. Buttons drive the
// player's TogglePauseMenu() (resume/quit) and the GameInstance settings functions.
UCLASS()
class LOOPED_API ULoopedPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	ULoopedPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	// --- Page switching ---
	UFUNCTION() void OnResumeClicked();
	UFUNCTION() void OnSettingsClicked();
	UFUNCTION() void OnBackClicked();
	UFUNCTION() void OnQuitClicked();
	UFUNCTION() void OnMotionBlurClicked();

	// Build helpers (theme: dark panel, gold title, cyan accents — matches the merchant/monitor).
	UButton* MakeButton(const FString& Label, const FLinearColor& Accent);
	void RefreshMotionBlurLabel();
	void ShowSettingsPage(bool bSettings);

	UPROPERTY() TObjectPtr<UVerticalBox> MainPage;
	UPROPERTY() TObjectPtr<UVerticalBox> SettingsPage;
	UPROPERTY() TObjectPtr<UTextBlock>   MotionBlurLabel;

	bool bBuilt = false;
};
