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

	if (IsLocalController()) {//生成UI
		if (PlayerWidgetClass && !PlayerWidget)PlayerWidget = CreateWidget<UC_PlayerWidget>(this, PlayerWidgetClass);
		PlayerWidget->AddToViewport();
		FInputModeUIOnly InputMode;
		SetInputMode(InputMode);
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
	
	if (PlayerWidget && PS1 && PS2 && MyPawn) {
		float FinalSkillCDState;
		if (GetPlayerState<AC_PlayerState>()->Chakra == 4)FinalSkillCDState = 0.f;
		else FinalSkillCDState = 100.f;
		PlayerWidget->SetUIShow(
			PS1->HealthValue, PS2->HealthValue, PS1->Chakra, PS2->Chakra,
			MyPawn->EscapeCDState, MyPawn->FirstSkillCDState, MyPawn->SecondSkillCDState,
			FinalSkillCDState,MyPawn->ScrollCDState, MyPawn->SummonCDState
		);
	}
}

//服务器校验（设计 5.1 / 计划 2.3）：客户端在输入函数里做的可行性判断，搬到服务器照做一遍；
//客户端那一份保留，作为本地先行判定。此刻还没有预测键，拒绝就表现为"世界状态不变"
void AC_PlayerController::Server_ChangeChakra_Implementation(int TargetChakra)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;

	//查克拉只有三个写入路径，判据逐条对应客户端（C_Character.cpp），三条之外一律拒绝 ——
	//原来这里是裸赋值，客户端说几就是几（改成 4 就是满查克拉）
	//增加：AddChakra 在自己那份未满时发"当前值加一"。它的三个蓝图调用点（命中、技能一命中、秘卷查克拉）
	//      都带权威判定，读到的就是服务器这一份，所以"当前值加一"在服务器上成立
	//替身消耗：Server_Escape 直调本函数发"当前值减一"，服务器侧同一次调用，值不会过期
	//奥义消耗：FinalSkill 在满查克拉时发清零，且排在技能请求之后（同一 Actor，可靠 RPC 保序）。
	//      再要求 MySkill 已经是 5：奥义请求本身被拒（状态不合法、服务器查克拉已变）时不能白扣查克拉
	const bool bAddChakra = (TargetChakra == PS->Chakra + 1) && (PS->Chakra < 4);
	const bool bEscapeCost = (TargetChakra == PS->Chakra - 1) && (PS->Chakra > 0);
	const bool bFinalSkillCost = (TargetChakra == 0) && (PS->Chakra == 4) && (PS->MySkill == 5);
	if (!bAddChakra && !bEscapeCost && !bFinalSkillCost)return;

	PS->Chakra = TargetChakra;
}

void AC_PlayerController::PlayerGetDamage(float Damage, ECharacterStateType State, EAttackType AttackType, FVector Effect, float EffectTime)
{
	PlayerBeAttacked.Broadcast(GetPawn()->GetActorLocation(), Damage);
	ECharacterStateType MyState = GetPlayerState<AC_PlayerState>()->CharacterState;

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
	GetPawn<AC_Character>()->LaunchState = 0;
	GetPawn<AC_Character>()->bAttackInputLock = false;
	GetPawn<AC_Character>()->Server_ChangeBox_Implementation(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (GetPawn<AC_Character>()->MyGrabPoint)GetPawn<AC_Character>()->MyGrabPoint->bIsUsing = false;
}

//服务器校验（设计 5.1 / 计划 2.3）：本函数在工程里【没有任何调用者】——C++ 侧没有调用点，
//也没有 BlueprintCallable（蓝图侧调用点盘点里 BP_PlayerController 标为"无关"），
//因此没有客户端判据可搬。段号将来由 3.3 的预测键路径决定（设计 2.4.4），
//它的校验与去向届时一并处理，本轮保持原样
void AC_PlayerController::Server_ChangeAttackState_Implementation(int TargetAttack)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->Attack = TargetAttack;
	}
}

//服务器校验（设计 5.1 / 计划 2.3）：本函数同样【没有任何调用者】，且 CharacterState 整条
//按已定事项 22 是服务器写、不由客户端输入触发（客户端那份校验只读不写），
//客户端本就无从发起这个请求：没有判据可搬，保留原样，等 3.x 决定删或改
void AC_PlayerController::Server_ChangeCharacterState_Implementation(ECharacterStateType TargetCharacterState)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		PS->CharacterState = TargetCharacterState;
	}
}

//服务器校验（设计 5.1 / 计划 2.3）：判据逐条对应 C_Character.cpp 的五个输入函数 ——
//FirstSkill(1) / SecondSkill(2) / Scroll(4，秘卷) / Summon(4，通灵) / FinalSkill(5)
void AC_PlayerController::Server_ChangeSkillState_Implementation(int TargetSkill)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AC_Character* Char = GetPawn<AC_Character>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS || !Char || !GameState)return;

	//五个输入函数共有的判据：状态是常态或保护态
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;

	switch (TargetSkill) {
	case 1:
		if (Char->FirstSkillCDState != 0.f)return;
		break;
	case 2:
		if (Char->SecondSkillCDState != 0.f)return;
		break;
	case 4:
		//秘卷与通灵共用请求值 4（两者的 MySkill 都是 4、动画相同），靠 SummonIndex 分辨是哪一个：
		//客户端在发本请求之前先发 Server_SetSummonIndex(0 秘卷 / 1 通灵)。这两条请求跨 Actor
		//（角色 / PlayerController），设计 2.11.4 说明过跨 Actor 不保证保序 —— 但服务器侧的 I_Summon
		//本来就要读 SummonIndex 决定生成哪一个，这个"先到"要求是原代码就有的，这里没有新增假设
		if (Char->SummonIndex == 1) {
			if (Char->SummonCDState != 0.f)return;
		}
		else {
			if (Char->ScrollCDState != 0.f)return;
		}
		break;
	case 5:
		//奥义的判据是满查克拉；清零由紧随其后的 Server_ChangeChakra(0) 完成（同一 Actor，保序）
		if (PS->Chakra != 4)return;
		break;
	default:
		//不在客户端请求集内的取值一律拒绝（原实现是照单全收）
		return;
	}

	PS->MySkill = TargetSkill;

	//被接受的输入在这里记下服务器侧的权威 CD 时间戳（设计 5.3 的四个时间戳）：
	//客户端在输入函数里也记一份，那是本地先行值；服务器这一份是权威值，2.4 起由权威值表下发给客户端。
	//服务器不写的话，上面那些 CD 判据永远是"没在冷却"（服务器这份时间戳恒为 0），拒绝路径形同虚设
	const float Now = GameState->GetServerWorldTimeSeconds();
	if (TargetSkill == 1)Char->LastFirstSkillTime = Now;
	else if (TargetSkill == 2)Char->LastSecondSkillTime = Now;
	else if (TargetSkill == 4) {
		if (Char->SummonIndex == 1)Char->LastSummonTime = Now;
		else Char->LastScrollTime = Now;
	}
	//奥义不记时间戳：它消耗的是查克拉，不是 CD（与客户端 FinalSkill 一致）
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

