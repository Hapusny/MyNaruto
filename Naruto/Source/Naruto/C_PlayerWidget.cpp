// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerWidget.h"
#include "Components/TextBlock.h"

void UC_PlayerWidget::SetTime(int time)
{
	TimeText->SetText(FText::FromString(FString::FromInt(time)));
}
