// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PaperZDAnimBP.h"
#include "C_PaperZDAnimBP.generated.h"

/**
 * 
 */
UCLASS()
class NARUTO_API UC_PaperZDAnimBP : public UPaperZDAnimBP
{
	GENERATED_BODY()
	

public:

	UC_PaperZDAnimBP();

	UFUNCTION()
	void SyncSpeed(float speed);

	UPROPERTY(EditDefaultsOnly);
	TObjectPtr<UPaperZDAnimationSource>AnimSource;

protected:
	UPROPERTY(VisibleAnywhere)
	float Speed = 0.f;

};
