// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "C_PlayerWidget.h"
#include "C_PlayerState.h"
#include "C_Character.h"

void AC_PlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(DefaultMappingContext, 0);
	}
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}

void AC_PlayerController::PlayerGetDamage(float Damage,EAttackType AttackType, FVector Effect)
{
	PlayerBeAttacked.Broadcast(GetPawn()->GetActorLocation(), Damage);
	GetPlayerState<AC_PlayerState>()->Attack = 0;
	if (AttackType == EAttackType::Push) {
		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Staggered;
		GetWorldTimerManager().ClearTimer(StaggeredTimerHandle);
		GetWorldTimerManager().SetTimer(
			StaggeredTimerHandle,
			[this]()
			{
				GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Normal;
			},
			Effect.Z,
			false
		);
		Effect.Z = 0;
		GetPawn()->AddActorLocalOffset(Effect);
	}
	else if (AttackType == EAttackType::Launch) {
		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Launched;
		Cast<AC_Character>(GetPawn())->LaunchCharacter(Effect,true, true);
	}
	else {

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


void AC_PlayerController::Client_SetWidgetTime_Implementation(int time)
{
	if (PlayerWidget)PlayerWidget->SetTime(time);
}

void AC_PlayerController::Client_ShowWidget_Implementation()
{
	if (PlayerWidgetClass && !PlayerWidget)PlayerWidget = CreateWidget<UC_PlayerWidget>(this, PlayerWidgetClass);
	PlayerWidget->AddToViewport();
}

