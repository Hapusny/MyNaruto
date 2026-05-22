// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "C_PlayerWidget.h"
#include "C_PlayerState.h"
#include "C_Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "C_GrabPoinnt.h"

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

	if (IsLocalController()) {//生成UI
		if (PlayerWidgetClass && !PlayerWidget)PlayerWidget = CreateWidget<UC_PlayerWidget>(this, PlayerWidgetClass);
		PlayerWidget->AddToViewport();
	}
	
}

void AC_PlayerController::Client_ChangeInputAbility_Implementation(bool target)
{
	
	if (target) {
		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);
	}
	else {
		FlushPressedKeys();
		FInputModeUIOnly InputMode;
		SetInputMode(InputMode);
	}
}

void AC_PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	

	//本地每帧更新角色UI
	if (!IsLocalController())return;
	
	if (PlayerWidget && PS1 && PS2 && MyPawn)PlayerWidget->SetUIShow(
		PS1->HealthValue,PS2->HealthValue,PS1->Chakra,PS2->Chakra,
		MyPawn->EscapeCDState,MyPawn->FirstSkillCDState,
		MyPawn->SecondSkillCDState,MyPawn->ScrollCDState, MyPawn->SummonCDState
	);
}

void AC_PlayerController::Server_ChangeChakra_Implementation(int TargetChakra)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->Chakra = TargetChakra;
	}
}

void AC_PlayerController::PlayerGetDamage(float Damage, ECharacterStateType State, EAttackType AttackType, FVector Effect, float EffectTime)
{
	PlayerBeAttacked.Broadcast(GetPawn()->GetActorLocation(), Damage);
	ECharacterStateType MyState = GetPlayerState<AC_PlayerState>()->CharacterState;
	GetPawn<AC_Character>()->LaunchState = 0;
	GetWorldTimerManager().ClearTimer(BeAttackedTimerHandle);

	//金刚体和被抓取时不受攻击改变状态
	if (MyState == ECharacterStateType::Unbreakable)return;
	if (MyState == ECharacterStateType::Grabbed)return;


	if (AttackType == EAttackType::Grab) {
		PlayerStateReset();
		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Grabbed;
		GetPawn<AC_Character>()->Mult_ChangeGravity(false); 
	}

	//硬体不受非抓取常态攻击改变状态
	if (MyState == ECharacterStateType::Armor && State == ECharacterStateType::Normal)return;

	if (AttackType == EAttackType::Launch) {//击飞
		PlayerStateReset();

		GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Launched;
		Cast<AC_Character>(GetPawn())->LaunchCharacter(Effect, true, true);
	}
	else if (AttackType == EAttackType::Push) {//平推
		PlayerStateReset();

		if (MyState == ECharacterStateType::Launched) {//击飞状态增加浮空
			GetWorldTimerManager().ClearTimer(BeAttackedTimerHandle);
			Cast<AC_Character>(GetPawn())->LaunchCharacter(FVector(0.f, 0.f, 20 * Effect.Length()), true, true);
		}
		else {//非击飞状态造成僵直
			GetPlayerState<AC_PlayerState>()->CharacterState = ECharacterStateType::Staggered;
			GetPawn()->AddActorLocalOffset(Effect);
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
	}
}

void AC_PlayerController::PlayerStateReset()
{
	GetPlayerState<AC_PlayerState>()->Attack = 0;
	GetPlayerState<AC_PlayerState>()->MySkill = 0;
	GetPawn<AC_Character>()->bAttackInputLock = false;
	GetPawn<AC_Character>()->Server_ChangeBox_Implementation(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (GetPawn<AC_Character>()->MyGrabPoint)GetPawn<AC_Character>()->MyGrabPoint->bIsUsing = false;
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

void AC_PlayerController::Client_SetWidgetTime_Implementation(int time)
{
	if (PlayerWidget)PlayerWidget->SetTime(time);
}

void AC_PlayerController::Client_SetWidgetEnd_Implementation(int res)
{
	if (PlayerWidget)PlayerWidget->WidgetGameEnd(res);
}

void AC_PlayerController::Client_ShowWidget_Implementation()
{
	if (!PlayerWidget)return;
	PlayerWidget->WidgetGameStart();

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

