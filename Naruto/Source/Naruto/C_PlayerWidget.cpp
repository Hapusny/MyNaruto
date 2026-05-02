// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"


void UC_PlayerWidget::SetTime(int time)
{
	TimeText->SetText(FText::FromString(FString::FromInt(time)));
}

void UC_PlayerWidget::SetUIShow(float player1Health, float player2Health, int player1Chakra, int player2Chakra, float escapeCD, float skill1CD, float skill2CD, float skill3CD, float scrollCD, float summonCD)
{
	Player1Health->SetText(FText::FromString(FString::FromInt(player1Health)));
	Player2Health->SetText(FText::FromString(FString::FromInt(player2Health)));
	Player1Bar->SetPercent(player1Health / 100.f);
	Player2Bar->SetPercent(player2Health / 100.f);
	Player1Chakra->SetPercent(float(player1Chakra) / 4.0);
	Player2Chakra->SetPercent(float(player2Chakra) / 4.0);
	SetIconShow(Escape, EscapeCD, escapeCD);
	SetIconShow(FirstSkill, FirstSkillCD, skill1CD);
	SetIconShow(SecondSkill, SecondSkillCD, skill2CD);
	SetIconShow(FinalSkill, FinalSkillCD, skill3CD);
	SetIconShow(Scroll, ScrollCD, scrollCD);
	SetIconShow(Summon, SummonCD, summonCD);
}

void UC_PlayerWidget::SetIconShow(TObjectPtr<UImage>& image, TObjectPtr<UTextBlock>& cdText, float cd)
{
	if (cd == 0.f) {
		cdText->SetVisibility(ESlateVisibility::Collapsed);
		image->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else {
		cdText->SetText(FText::FromString(FString::FromInt(cd)));
		cdText->SetVisibility(ESlateVisibility::Visible);
		image->SetColorAndOpacity(FLinearColor(0.33f, 0.33f, 0.33f, 1.0f));
	}
}