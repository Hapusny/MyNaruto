// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "C_ArenaGM.generated.h"

class AC_Character;
class AC_Camera;
class AC_PlayerController;
class AC_PlayerState;
/**
 * 
 */
UCLASS()
class NARUTO_API AC_ArenaGM : public AGameMode
{
	GENERATED_BODY()
	

public:
	virtual void PostLogin(APlayerController* NewPlayer) override;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Character>Player1Pawn;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Character>Player2Pawn;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Camera>PlayerCameraClass;


protected:
	virtual void BeginPlay()override;

private:
	UPROPERTY()
	TArray<APlayerController*> Players;

	TObjectPtr<AC_PlayerController>Player1;

	TObjectPtr<AC_PlayerController>Player2;

	TObjectPtr<AC_PlayerState>PS1;

	TObjectPtr<AC_PlayerState>PS2;

	void AssignTeams();

	void SpawnPawnToPlayer(TSubclassOf<AC_Character> PawnClass, APlayerController* Player);

	void StartFight();

	FTimerHandle ClockHandle;

	void ClockChange();
};
