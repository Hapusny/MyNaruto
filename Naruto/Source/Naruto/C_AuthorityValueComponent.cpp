// Fill out your copyright notice in the Description page of Project Settings.


#include "C_AuthorityValueComponent.h"
#include "C_Character.h"
#include "C_PlayerState.h"
#include "Net/UnrealNetwork.h"

UC_AuthorityValueComponent::UC_AuthorityValueComponent()
{
	// 组件本体参与复制，只带一个属性 AuthorityValueTable（设计 2.7.5）
	SetIsReplicatedByDefault(true);

	// 组件不单独 Tick：表的刷新挂在 PreReplication 钩子上（设计 2.7.5）
	PrimaryComponentTick.bCanEverTick = false;
}

void UC_AuthorityValueComponent::BeginPlay()
{
	Super::BeginPlay();

	// 初始复制"相等即不触发 OnRep"的补丁（设计 2.7.5）：
	// 客户端手上的表刚构造出来是结构体默认值，若服务器的初始值与本地相同，
	// OnRep 会被跳过，表里就一直是默认值（回滚会恢复到默认值而不是真值）。
	// 先把表按宿主当前值填一遍，等价于"服务器的权威值此刻等于本地当前值"。
	// 服务器侧填的是同一批真实属性，随后每帧 PreReplication 会原样再刷一遍。
	RefreshTableFromHost();
}

void UC_AuthorityValueComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 只注册表：被预测的属性不再逐个复制（设计 2.7.1 / 3.2）
	DOREPLIFETIME(UC_AuthorityValueComponent, AuthorityValueTable);
}

void UC_AuthorityValueComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// 组件 PreReplication 只在服务器被调用（设计 2.7.5）：
	// 它是"这个 Actor 本帧确实要发复制"时才走的钩子，因此天然零纪律
	RefreshTableFromHost();
}

void UC_AuthorityValueComponent::OnRep_AuthorityValueTable()
{
	// 客户端路径：表到达 → 交给预测组件按采用规则写回本地属性并清标记（设计 2.7.2 / 3.2）
	OnAuthorityValueTableArrived.ExecuteIfBound(AuthorityValueTable);
}

// ---- 内部 ----

void UC_AuthorityValueComponent::RefreshTableFromHost()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	// 挂在 AC_PlayerState 上（含敌方 PS）：填 PS 段（设计 2.7.5 的挂载点表）
	if (const AC_PlayerState* OwnerPS = Cast<AC_PlayerState>(Owner))
	{
		AuthorityValueTable.HealthValue = OwnerPS->HealthValue;
		AuthorityValueTable.Chakra = OwnerPS->Chakra;
		AuthorityValueTable.Attack = OwnerPS->Attack;
		AuthorityValueTable.MySkill = OwnerPS->MySkill;
		AuthorityValueTable.CharacterState = static_cast<uint8>(OwnerPS->CharacterState);
		return;
	}

	// 挂在 AC_Character 上：填 Character 段
	if (const AC_Character* OwnerCharacter = Cast<AC_Character>(Owner))
	{
		AuthorityValueTable.Toward = OwnerCharacter->Toward;
		AuthorityValueTable.LastEscapeTime = OwnerCharacter->LastEscapeTime;
		AuthorityValueTable.LastFirstSkillTime = OwnerCharacter->LastFirstSkillTime;
		AuthorityValueTable.LastSecondSkillTime = OwnerCharacter->LastSecondSkillTime;
		AuthorityValueTable.LastScrollTime = OwnerCharacter->LastScrollTime;
		AuthorityValueTable.LastSummonTime = OwnerCharacter->LastSummonTime;
	}
}
