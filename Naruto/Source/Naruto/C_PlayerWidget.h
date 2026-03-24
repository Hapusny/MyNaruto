// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "C_PlayerWidget.generated.h"

class UTextBlock;

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

public:
	UFUNCTION()
	void SetTime(int time);
};
