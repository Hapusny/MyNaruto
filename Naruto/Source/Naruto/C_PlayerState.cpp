// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "C_AuthorityValueComponent.h"

AC_PlayerState::AC_PlayerState()
{
	NetUpdateFrequency = 100.f;//网络同步频率
	//预测系统（设计 2.7.5 / 计划 2.4）：这一行就是权威值表的下发频率上限，必须保留。

	//权威值表组件（设计 2.7.5 / 计划 2.4）：构造函数里创建，随本 PS 复制到各客户端；
	//bReplicates 由组件自己的构造函数置位（设计 2.7.5），这里不用再调 SetIsReplicatedByDefault
	AuthorityValueComponent = CreateDefaultSubobject<UC_AuthorityValueComponent>(TEXT("AuthorityValueComponent"));
}

void AC_PlayerState::OnRep_Team()
{
	OnTeamChanged.Broadcast();//队伍分配情况广播给忍者
}

void AC_PlayerState::SetTeam(ETeamType TargetTeam)
{
	Team = TargetTeam;
}

void AC_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	//预测系统（设计 5.2b / 计划 2.4）：CharacterState / Attack / HealthValue / Chakra / MySkill
	//五条 DOREPLIFETIME 已删除，改由 AuthorityValueTable 下发。Team 不参与预测，继续走逐属性复制
	DOREPLIFETIME(AC_PlayerState, Team);
}

void AC_PlayerState::PlayerGetDamage(FVector Location, float Damage)
{
	HealthValue -= Damage;
}
