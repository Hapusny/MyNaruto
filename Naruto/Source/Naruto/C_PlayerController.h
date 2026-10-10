// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
// 预测键 / 回执通道（计划 2.6）：Server_ChangeSkillState 的键参数按值传，UHT 生成的代码需要
// 完整类型（FPredictionKey 在 C_PredictionComponent.h）。该头文件只前向声明本类，不成环
#include "C_PredictionComponent.h"
#include "C_PlayerController.generated.h"

class UInputMappingContext;
class UC_PlayerWidget;
class AC_PlayerState;
class AC_Character;
enum class ECharacterStateType : uint8;
enum class EAttackType : uint8;

DECLARE_MULTICAST_DELEGATE_TwoParams(FBeAttacked, FVector, float);
/**
 * 
 */
UCLASS()
class NARUTO_API AC_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:

	//角色受击委托
	FBeAttacked PlayerBeAttacked;

	virtual void BeginPlay()override;

	UFUNCTION(Client,Reliable)
	void Client_ChangeInputAbility(bool target);

	//输入映射
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	//玩家UI
	UPROPERTY(EditAnywhere)
	TSubclassOf<UC_PlayerWidget>PlayerWidgetClass;

	//更新UI
	UFUNCTION(Client,Reliable)
	void Client_ShowWidget();

	UFUNCTION(Client, Reliable)
	void Client_SetWidgetTime(int time);

	UFUNCTION(Client, Reliable)
	void Client_SetWidgetEnd(int res);

	//角色状态相关更新
	UFUNCTION(Server,Reliable)
	void Server_ChangeCharacterState(ECharacterStateType TargetCharacterState);

	UFUNCTION(Server, Reliable)
	void Server_ChangeAttackState(int TargetAttack);

	//技能请求。设计 3.4.2 的挂载约束：全工程只有三个 RPC 可以带键，这是其中之一。
	//键由客户端输入点创建（切片 3.1）；阶段二 2.6 起签名先带上，生产调用点一律传空键
	//（KeyID == 0 = 非预测请求：服务器照常校验执行、不回执，见设计 3.4.2 约定）
	UFUNCTION(Server, Reliable)
	void Server_ChangeSkillState(int TargetSkill, FPredictionKey PredictionKey);

	UFUNCTION(Server, Reliable)
	void Server_ChangeChakra(int TargetChakra);

	//预测回执（设计 2.11.1）：宿主是 PlayerController —— 引擎保证每个连接唯一，路由最直接，
	//且角色换人 / 重生（AC_ArenaGM::SpawnPawnToPlayer 会 Destroy 再 Spawn）不会让在途回执丢目标。
	//KeyID 在客户端键表里找不到时静默返回（回执迟到于超时回滚属正常竞态，见设计 3.4.5）
	UFUNCTION(Client, Reliable)
	void Client_ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked);

	//服务器侧唯一的回执出口（设计 2.11.4 / 计划 2.6）：带键请求的每一个出口都调它一次。
	//KeyID == 0（降级路径）不回执；Prediction.DropResolve 打开时不发（验证超时兜底）。
	//必须在写权威值【之后】调用：确认时打包的就是被接受的那个状态（设计 2.11.1 的打包方式）
	void SendPredictionResolve(const FPredictionKey& Key, uint8 Result);

	//角色受击
	void PlayerGetDamage(float Damage, ECharacterStateType State,EAttackType AttackType,FVector Effect,float EffectTime);

private:
	void PlayerStateReset();

protected:
	virtual void Tick(float DeltaSeconds) override;//更新UI

private:
	TObjectPtr<UC_PlayerWidget>PlayerWidget;

	FTimerHandle BeAttackedTimerHandle;

	//双方玩家状态
	UPROPERTY()
	TObjectPtr<AC_PlayerState>PS1;

	UPROPERTY()
	TObjectPtr<AC_PlayerState>PS2;

	//玩家的忍者
	UPROPERTY()
	TObjectPtr<AC_Character>MyPawn;

};
