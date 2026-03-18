// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "C_ArenaGM.generated.h"

/**
 * 
 */
UCLASS()
class NARUTO_API AC_ArenaGM : public AGameMode
{
	GENERATED_BODY()
	

public:
	virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
	virtual void BeginPlay()override;

private:
	UPROPERTY()
	TArray<APlayerController*> Players;

	UFUNCTION()
	void AssignTeams();


};
