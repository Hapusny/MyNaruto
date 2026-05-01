// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "C_ArenaGS.generated.h"

/**
 * 
 */
UCLASS()
class NARUTO_API AC_ArenaGS : public AGameState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	float FightStartTime = 0.f;
};
