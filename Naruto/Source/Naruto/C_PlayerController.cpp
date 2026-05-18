// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "C_PlayerWidget.h"
#include "C_PlayerState.h"
#include "C_Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"

void AC_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	//绑定输入映射
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}

void AC_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	

	//本地每帧更新角色UI
	if (!IsLocalController())return;
	
	if (PlayerWidget && PS1 && PS2 && MyPawn)PlayerWidget->SetUIShow(
		PS1->HealthValue,PS2->HealthValue,PS1->Chakra,PS2->Chakra,
		MyPawn->EscapeCDState,MyPawn->FirstSkillCDState,
		MyPawn->SecondSkillCDState,MyPawn->ScrollCDState
	);
}

void AC_PlayerController::Server_ChangeChakra_Implementation(int TargetChakra)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->Chakra = TargetChakra;
	}
}

void AC_PlayerController::PlayerGetDamage(float Damage, EAttackType AttackType, FVector Effect, float EffectTime)
{
	PlayerBeAttacked.Broadcast(GetPawn()->GetActorLocation(), Damage);
	GetPlayerState<AC_PlayerState>()->Attack = 0;
	if (GetPlayerState<AC_PlayerState>()->CharacterState == ECharacterStateType::Grabbed)return;
	if (AttackType == EAttackType::Launch) {
		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Launched;
		Cast<AC_Character>(GetPawn())->LaunchCharacter(Effect, true, true);
	}
	else if (AttackType == EAttackType::Push) {
		if (GetPlayerState<AC_PlayerState>()->CharacterState == ECharacterStateType::Normal) {
			GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Staggered;
			GetPawn()->AddActorLocalOffset(Effect);
			GetWorldTimerManager().ClearTimer(BeAttackedTimerHandle);
			GetWorldTimerManager().SetTimer(
				BeAttackedTimerHandle,
				[this]()
				{
					GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Normal;
				},
				EffectTime,
				false
			);
		}
		else if (GetPlayerState<AC_PlayerState>()->CharacterState == ECharacterStateType::Launched) {
			GetWorldTimerManager().ClearTimer(BeAttackedTimerHandle);
			FVector NewEffect = Effect;
			NewEffect.Z = fabs(Effect.X * 30);
			NewEffect.X = 0;
			Cast<AC_Character>(GetPawn())->LaunchCharacter(NewEffect, true, true);
		}
	}
	else {
		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Grabbed;
		GetPawn<AC_Character>()->Mult_ChangeGravity(false);
	}
}

void AC_PlayerController::Server_ChangeAttackState_Implementation(int TargetAttack)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->Attack = TargetAttack;
	}
}

void AC_PlayerController::Server_ChangeCharacterState_Implementation(ECharacterStateType TargetCharacterState)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->CharacterState = TargetCharacterState;
	}
}

void AC_PlayerController::Server_ChangeSkillState_Implementation(int TargetSkill)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->MySkill = TargetSkill;
	}
}

void AC_PlayerController::Server_EscapeEffect_Implementation()
{
	//替身后查克拉减少
	GetPlayerState<AC_PlayerState>()->Chakra--;
}


void AC_PlayerController::Client_SetWidgetTime_Implementation(int time)
{
	if (PlayerWidget)PlayerWidget->SetTime(time);
}

void AC_PlayerController::Client_ShowWidget_Implementation()
{
	if (PlayerWidgetClass && !PlayerWidget)PlayerWidget = CreateWidget<UC_PlayerWidget>(this, PlayerWidgetClass);
	PlayerWidget->AddToViewport();

	//获取双方玩家状态以及自身忍者以更新UI
	if (AGameStateBase* GS = GetWorld()->GetGameState())
	{
		for (APlayerState* PS : GS->PlayerArray)
		{
			AC_PlayerState* MyPS = Cast<AC_PlayerState>(PS);
			if (MyPS && MyPS->Team == ETeamType::Blue)PS1 = MyPS;
			if (MyPS && MyPS->Team == ETeamType::Red)PS2 = MyPS;
		}
	}
	MyPawn = Cast<AC_Character>(GetPawn());
}

