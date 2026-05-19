// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "C_PlayerWidget.generated.h"

class UTextBlock;
class UImage;
class UProgressBar;

/**
 * 
 */
UCLASS()
class NARUTO_API UC_PlayerWidget : public UUserWidget
{
	GENERATED_BODY()
	
protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Player1Health;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> Player2Health;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> EscapeCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Escape;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FirstSkillCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> FirstSkill;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SecondSkillCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> SecondSkill;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FinalSkillCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> FinalSkill;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ScrollCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Scroll;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SummonCD;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> Summon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Player1Bar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Player2Bar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Player1Chakra;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> Player2Chakra;


public:
	void SetTime(int time);

	void SetUIShow(float player1Health = 100.f, float player2Health = 100.f, int player1Chakra = 2, int player2Chakra = 2,float escapeCD = 0.f,float skill1CD = 0.f,float skill2CD = 0.f,float scrollCD = 0.f,float summonCD = 0.f);

private:
	void SetIconShow(TObjectPtr<UImage>& image, TObjectPtr<UTextBlock>& cdText,float cd);
};
