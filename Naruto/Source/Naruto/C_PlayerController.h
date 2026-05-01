// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "C_PlayerController.generated.h"

class UInputMappingContext;
class UC_PlayerWidget;

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

	//输入映射
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	//玩家UI
	UPROPERTY(EditAnywhere)
	TSubclassOf<UC_PlayerWidget>PlayerWidgetClass;

	//显示UI
	UFUNCTION(Client,Reliable)
	void Client_ShowWidget();

	//更新UI
	UFUNCTION(Client, Reliable)
	void Client_SetWidgetTime(int time);


	//角色状态相关更新
	UFUNCTION(Server,Reliable)
	void Server_ChangeCharacterState(ECharacterStateType TargetCharacterState);

	UFUNCTION(Server, Reliable)
	void Server_ChangeAttackState(int TargetAttack);

	UFUNCTION(Server, Reliable)
	void Server_ChangeSkillState(int TargetSkill);

	UFUNCTION(Server, Reliable)
	void Server_EscapeEffect();

	//角色受击
	UFUNCTION()

	void PlayerGetDamage(float Damage,EAttackType AttackType,FVector Effect,float EffectTime);

private:
	TObjectPtr<UC_PlayerWidget>PlayerWidget;

	FTimerHandle BeAttackedTimerHandle;
};
