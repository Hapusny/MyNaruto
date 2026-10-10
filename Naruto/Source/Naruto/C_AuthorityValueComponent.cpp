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

	// 权威侧的一次初始刷新：表按宿主当前值填一遍。服务器上随后每帧 PreReplication 还会原样再刷；
	// 单独跑（无 NetDriver）时没有 PreReplication，这一次就是表的初值。
	// 客户端会被函数内部挡掉——理由见 RefreshTableFromHost（计划 2.4 的缺陷修复，2026-10-10）。
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

	// 只有权威侧写表——本组件自己的契约"客户端从不写表"（见头文件）。
	// 计划 2.4 的缺陷修复（2026-10-10）：设计 2.7.5 把客户端那次 BeginPlay 初始化写成了"兜底"，
	// 前提是"OnRep 会在这之后到达、把它覆盖回真值"，但引擎的顺序恰好相反：对新复制过来的
	// Actor 是【先应用初始复制、再调 OnRep、最后才 BeginPlay】
	// （DataChannel.cpp:3223 PostReceivedBunch -> :3237 PostNetInit；Actor.cpp:4124 在 PostNetInit 里才
	// DispatchBeginPlay，那句注释原文是"After all properties have been initialized, call PostNetInit"）。
	// 于是客户端这次刷新是把刚收到的服务器值覆盖成本地默认值，而 OnRep 只在【变化】时来
	// —— 那份权威值再也回不来（表里就一直是宿主默认值）。
	// 症状：红方客户端的 Toward 被卡在类默认 true（服务器是 false），Move() 里"要不要发转向请求"
	// 读的就是本地 Toward，于是转向请求永远发不出去 = 红方客户端不能转向；蓝方默认恰好是 true，看不出来。
	// 表在客户端的初值本来也不需要这里补：OnRep 被跳过只发生在"收到的值等于本地当前值"，
	// 那种情况表里已经是服务器的值。真正的空窗是"首次到达时委托还没绑"——那一次由
	// UC_PredictionComponent::BindAuthorityValueHost 的绑定补采用负责。
	if (Owner->GetNetMode() == NM_Client)
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
