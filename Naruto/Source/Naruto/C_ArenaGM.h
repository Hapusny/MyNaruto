// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "C_ArenaGM.generated.h"

class AC_Character;
class AC_Camera;
class AC_PlayerController;
class AC_PlayerState;
class AC_ArenaGS;
/**
 * 
 */
UCLASS()
class NARUTO_API AC_ArenaGM : public AGameMode
{
	GENERATED_BODY()
	

public:
	virtual void PostLogin(APlayerController* NewPlayer) override;//PIE中处理玩家入场
	virtual void HandleSeamlessTravelPlayer(AController*& C) override;//玩家入场后开始初始化

	//玩家选择的忍者
	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Character>Player1Pawn;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Character>Player2Pawn;


	//玩家视角摄像头
	UPROPERTY(EditAnywhere)
	TSubclassOf<AC_Camera>PlayerCameraClass;

	//对战回合时间
	UPROPERTY(EditAnywhere)
	float GameFightTime = 60.f;

	//玩家开始操作时间
	UPROPERTY(EditAnywhere)
	float PlayerStartTime = 4.f;

	UFUNCTION()
	void GameTerminate();

	void SetPlayerPauseState(int player, bool state);


protected:
	virtual void Tick(float DeltaSeconds) override;//处理时间更新及结算判定

private:

	UPROPERTY()
	TObjectPtr<AC_ArenaGS>MyGameState;

	UPROPERTY()
	TArray<APlayerController*> Players;

	//玩家1和玩家2的相关类
	UPROPERTY()
	TObjectPtr<AC_PlayerController>Player1;

	UPROPERTY()
	TObjectPtr<AC_PlayerController>Player2;

	UPROPERTY()
	TObjectPtr<AC_PlayerState>PS1;

	UPROPERTY()
	TObjectPtr<AC_PlayerState>PS2;

	void AssignTeams();//分配队伍

	void SpawnPawnToPlayer(TSubclassOf<AC_Character> PawnClass, APlayerController* Player);//为玩家生成选择的忍者

	void StartFight();//战斗回合开始

	void BackToLobby();
};
