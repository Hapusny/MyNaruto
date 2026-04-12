// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerState.h"
#include "Net/UnrealNetwork.h"

AC_PlayerState::AC_PlayerState()
{
	NetUpdateFrequency = 60.f;
}

void AC_PlayerState::SetTeam(ETeamType TargetTeam)
{
	Team = TargetTeam;
}

void AC_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AC_PlayerState, Team);
	DOREPLIFETIME(AC_PlayerState, ClockTime);
	DOREPLIFETIME(AC_PlayerState, CharacterState);
	DOREPLIFETIME(AC_PlayerState, Attack);
	DOREPLIFETIME(AC_PlayerState, HealthValue);
}

void AC_PlayerState::PlayerGetDamage(FVector Location, float Damage)
{
	HealthValue -= Damage;
}
