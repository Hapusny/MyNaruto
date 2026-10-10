// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "C_AuthorityValueComponent.generated.h"

// 权威值表（设计 2.7.5 / 3.3.1）：服务器权威值的统一通道
// 用带类型字段取代 TMap<FName, FString>：不再需要字符串化与解析，
// 比较交由引擎的 member-wise 比较完成（见 2.7.5）
// 位置不入表（见 2.5.1-C / 2.9）
//
// 本结构体被两处宿主复用：挂在 PlayerState 上的实例只填 PS 段字段，
// 挂在 Character 上的实例只填 Character 段字段。未使用的字段恒为默认值，
// 不参与任何判定，也不会产生歧义。
USTRUCT()
struct FAuthorityValueTable
{
	GENERATED_BODY()

	// ---- AC_PlayerState 段（含敌方 PS）----
	UPROPERTY() float HealthValue = 0.f;	// 己方不预测血量，仅敌方代理用（2.5.2）
	UPROPERTY() int32 Chakra = 0;
	UPROPERTY() int32 Attack = 0;
	UPROPERTY() int32 MySkill = 0;
	UPROPERTY() uint8 CharacterState = 0;	// ECharacterStateType；用 uint8 避免头文件循环包含

	// ---- AC_Character 段 ----
	UPROPERTY() bool Toward = true;
	UPROPERTY() float LastEscapeTime = 0.f;
	UPROPERTY() float LastFirstSkillTime = 0.f;	// 这四个时间戳需先补复制，见 5.3
	UPROPERTY() float LastSecondSkillTime = 0.f;
	UPROPERTY() float LastScrollTime = 0.f;
	UPROPERTY() float LastSummonTime = 0.f;
};

// 表到达委托（计划 W1.3）：OnRep_AuthorityValueTable 里带着收到的表触发。
// 预测组件在 InitializePredictionContext 中绑定到自己的 ApplyAuthorityValueTable（设计 3.2）。
// 按设计 3.5 的委托约定用 DECLARE_DELEGATE（非动态多播）；只有一个绑定者，重绑即覆盖。
DECLARE_DELEGATE_OneParam(FOnAuthorityValueTableArrived, const FAuthorityValueTable&);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class NARUTO_API UC_AuthorityValueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UC_AuthorityValueComponent();

	// 权威值表：服务器在 PreReplication 里从宿主的真实属性刷新，客户端从不写它（设计 2.7.5）
	UPROPERTY(ReplicatedUsing = OnRep_AuthorityValueTable)
	FAuthorityValueTable AuthorityValueTable;

	// 表到达时（客户端）触发，供预测组件按采用规则处理（设计 2.7.2 / 3.2）
	FOnAuthorityValueTableArrived OnAuthorityValueTableArrived;

	UFUNCTION()
	void OnRep_AuthorityValueTable();

	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	// 从宿主（AC_PlayerState 填 PS 段 / AC_Character 填 Character 段）的真实属性刷新表。
	// 服务器每帧在 PreReplication 里调用、BeginPlay 里再刷一次（单独跑时没有 PreReplication）。
	// 客户端一律不写表（本函数第一行就挡掉 NM_Client）——计划 2.4 的缺陷修复，2026-10-10，
	// 原设计 2.7.5 让客户端在 BeginPlay 里补一次，引擎顺序（初始复制 -> OnRep -> BeginPlay）下
	// 那一次会把刚收到的权威值覆盖掉；客户端那份表的初值改由
	// UC_PredictionComponent::BindAuthorityValueHost 绑定时的补采用负责。
	void RefreshTableFromHost();
};
