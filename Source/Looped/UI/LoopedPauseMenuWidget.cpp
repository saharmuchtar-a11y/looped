#include "LoopedPauseMenuWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Player/LoopedCharacter.h"
#include "Core/LoopedGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

// Palette (project theme).
static const FLinearColor kGold(1.00f, 0.78f, 0.28f);
static const FLinearColor kCyan(0.35f, 0.85f, 1.00f);
static const FLinearColor kInk (0.02f, 0.03f, 0.05f, 0.92f);
static const FLinearColor kBtn (0.06f, 0.09f, 0.13f, 1.00f);

ULoopedPauseMenuWidget::ULoopedPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UButton* ULoopedPauseMenuWidget::MakeButton(const FString& Label, const FLinearColor& Accent)
{
	UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Btn->SetBackgroundColor(kBtn);

	UTextBlock* Txt = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Txt->SetText(FText::FromString(Label));
	Txt->SetColorAndOpacity(FSlateColor(Accent));
	FSlateFontInfo Font = Txt->GetFont();
	Font.Size = 22;
	Txt->SetFont(Font);
	Txt->SetJustification(ETextJustify::Center);
	Btn->AddChild(Txt);
	return Btn;
}

TSharedRef<SWidget> ULoopedPauseMenuWidget::RebuildWidget()
{
	if (!bBuilt && WidgetTree)
	{
		bBuilt = true;

		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		// Full-screen dim.
		UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
		Dim->SetBrushColor(kInk);
		if (UCanvasPanelSlot* DS = Root->AddChildToCanvas(Dim))
		{
			DS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			DS->SetOffsets(FMargin(0.f));
		}

		// Centered column that holds both pages.
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		Dim->SetContent(Column);
		Dim->SetHorizontalAlignment(HAlign_Center);
		Dim->SetVerticalAlignment(VAlign_Center);

		auto AddToColumn = [&](UWidget* W, float Bottom = 14.f)
		{
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Column->AddChild(W)))
			{
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
			}
		};

		// Title.
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
		Title->SetText(FText::FromString(TEXT("PAUSED")));
		Title->SetColorAndOpacity(FSlateColor(kGold));
		{
			FSlateFontInfo F = Title->GetFont(); F.Size = 42; Title->SetFont(F);
		}
		Title->SetJustification(ETextJustify::Center);
		AddToColumn(Title, 28.f);

		// --- Main page ---
		MainPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainPage"));
		AddToColumn(MainPage, 0.f);
		{
			UButton* Resume   = MakeButton(TEXT("Resume"),          kCyan);
			UButton* Settings = MakeButton(TEXT("Settings"),        kCyan);
			UButton* Quit     = MakeButton(TEXT("Quit to Desktop"), FLinearColor(1.f, 0.45f, 0.4f));
			Resume->OnClicked.AddDynamic(this,   &ULoopedPauseMenuWidget::OnResumeClicked);
			Settings->OnClicked.AddDynamic(this, &ULoopedPauseMenuWidget::OnSettingsClicked);
			Quit->OnClicked.AddDynamic(this,     &ULoopedPauseMenuWidget::OnQuitClicked);
			for (UButton* B : { Resume, Settings, Quit })
			{
				if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(MainPage->AddChild(B)))
				{
					S->SetHorizontalAlignment(HAlign_Fill);
					S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
				}
			}
		}

		// --- Settings page (hidden until opened) ---
		SettingsPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsPage"));
		AddToColumn(SettingsPage, 0.f);
		{
			UButton* MotionBlur = MakeButton(TEXT("Motion Blur: ON"), kCyan);
			MotionBlur->OnClicked.AddDynamic(this, &ULoopedPauseMenuWidget::OnMotionBlurClicked);
			// Grab the label so we can flip ON/OFF in place.
			MotionBlurLabel = Cast<UTextBlock>(MotionBlur->GetChildAt(0));
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(SettingsPage->AddChild(MotionBlur)))
			{
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
			}

			// Key-binds header + live lines from IMC_Default.
			UTextBlock* KeysHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			KeysHeader->SetText(FText::FromString(TEXT("CONTROLS")));
			KeysHeader->SetColorAndOpacity(FSlateColor(kGold));
			{ FSlateFontInfo F = KeysHeader->GetFont(); F.Size = 18; KeysHeader->SetFont(F); }
			KeysHeader->SetJustification(ETextJustify::Center);
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(SettingsPage->AddChild(KeysHeader)))
			{
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			}

			if (const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(UGameplayStatics::GetGameInstance(this)))
			{
				for (const FText& Line : GI->GetKeyBindLines())
				{
					UTextBlock* KT = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
					KT->SetText(Line);
					KT->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.9f, 0.95f)));
					{ FSlateFontInfo F = KT->GetFont(); F.Size = 15; KT->SetFont(F); }
					KT->SetJustification(ETextJustify::Center);
					if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(SettingsPage->AddChild(KT)))
					{
						S->SetHorizontalAlignment(HAlign_Fill);
						S->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
					}
				}
			}

			UButton* Back = MakeButton(TEXT("Back"), kCyan);
			Back->OnClicked.AddDynamic(this, &ULoopedPauseMenuWidget::OnBackClicked);
			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(SettingsPage->AddChild(Back)))
			{
				S->SetHorizontalAlignment(HAlign_Fill);
				S->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
			}
		}

		ShowSettingsPage(false);
		RefreshMotionBlurLabel();
	}

	return Super::RebuildWidget();
}

void ULoopedPauseMenuWidget::ShowSettingsPage(bool bSettings)
{
	if (MainPage)     MainPage->SetVisibility(bSettings ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (SettingsPage) SettingsPage->SetVisibility(bSettings ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void ULoopedPauseMenuWidget::RefreshMotionBlurLabel()
{
	if (!MotionBlurLabel) return;
	const ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(UGameplayStatics::GetGameInstance(this));
	const bool bOn = GI ? GI->IsMotionBlurEnabled() : true;
	MotionBlurLabel->SetText(FText::FromString(FString::Printf(TEXT("Motion Blur: %s"), bOn ? TEXT("ON") : TEXT("OFF"))));
}

void ULoopedPauseMenuWidget::OnResumeClicked()
{
	if (ALoopedCharacter* PC = Cast<ALoopedCharacter>(GetOwningPlayerPawn()))
	{
		PC->TogglePauseMenu(); // unpauses, restores input, removes this widget
	}
}

void ULoopedPauseMenuWidget::OnSettingsClicked() { ShowSettingsPage(true); }
void ULoopedPauseMenuWidget::OnBackClicked()     { ShowSettingsPage(false); }

void ULoopedPauseMenuWidget::OnMotionBlurClicked()
{
	if (ULoopedGameInstance* GI = Cast<ULoopedGameInstance>(UGameplayStatics::GetGameInstance(this)))
	{
		GI->SetMotionBlurEnabled(!GI->IsMotionBlurEnabled());
	}
	RefreshMotionBlurLabel();
}

void ULoopedPauseMenuWidget::OnQuitClicked()
{
	// Unpause first so QuitGame's fade isn't frozen, then hard-quit to desktop.
	UGameplayStatics::SetGamePaused(this, false);
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
