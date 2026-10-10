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
	//锁更正（计划 2.5 / 设计 2.8）：受击 / 抓取 / 击飞打断会解除普攻输入锁，客户端那一份要跟上 ——
	//本地锁的复位只有这一条来源（设计 2.6.1）。本函数只在服务器上调用（伤害是权威的），
	//也就是"服务器每一处写锁的位置"里的 PlayerStateReset 那一行
	GetPawn<AC_Character>()->SendLockCorrection(AC_Character::LockBitAttackInputLock);
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
//
//回执接线（设计 2.11.4 / 计划 2.6）：带键请求的【每一个出口】都要回执一次，否则客户端只能等超时兜底。
//写法与 2.5 的锁补发同一形状 —— 每条早退前面先发回执再返回。键无效（KeyID == 0）时 SendPredictionResolve
//自己跳过：降级路径照常校验执行、不回执（设计 3.4.2 的约定），所以生产调用点传空键时行为与改造前逐位一致
void AC_PlayerController::Server_ChangeSkillState_Implementation(int TargetSkill, FPredictionKey PredictionKey)
{
	//Result 取值：0 = Confirmed，1 = Rejected（设计 2.11.1）
	const uint8 RejectResult = 1;
	const uint8 ConfirmResult = 0;

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AC_Character* Char = GetPawn<AC_Character>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS || !Char || !GameState) { SendPredictionResolve(PredictionKey, RejectResult); return; }

	//五个输入函数共有的判据：状态是常态或保护态
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected)) { SendPredictionResolve(PredictionKey, RejectResult); return; }

	//调试开关（计划 2.6 工具表）：带键请求一律拒绝、且不写权威值 —— 回滚路径 100% 可复现。
	//只作用于【带键】请求：不带键的请求没有本地预测可回滚，拒掉它只会让开关打开时玩不了
	if (PredictionKey.IsValidKey() && IsPredictionForceRejectEnabled())
	{
		UE_LOG(LogPrediction, Log, TEXT("Prediction.ForceReject: %s rejects the keyed request (skill %d) without writing anything"), *GetName(), TargetSkill);
		SendPredictionResolve(PredictionKey, RejectResult);
		return;
	}

	switch (TargetSkill) {
	case 1:
		if (Char->FirstSkillCDState != 0.f) { SendPredictionResolve(PredictionKey, RejectResult); return; }
		break;
	case 2:
		if (Char->SecondSkillCDState != 0.f) { SendPredictionResolve(PredictionKey, RejectResult); return; }
		break;
	case 4:
		//秘卷与通灵共用请求值 4（两者的 MySkill 都是 4、动画相同），靠 SummonIndex 分辨是哪一个：
		//客户端在发本请求之前先发 Server_SetSummonIndex(0 秘卷 / 1 通灵)。这两条请求跨 Actor
		//（角色 / PlayerController），设计 2.11.4 说明过跨 Actor 不保证保序 —— 但服务器侧的 I_Summon
		//本来就要读 SummonIndex 决定生成哪一个，这个"先到"要求是原代码就有的，这里没有新增假设
		if (Char->SummonIndex == 1) {
			if (Char->SummonCDState != 0.f) { SendPredictionResolve(PredictionKey, RejectResult); return; }
		}
		else {
			if (Char->ScrollCDState != 0.f) { SendPredictionResolve(PredictionKey, RejectResult); return; }
		}
		break;
	case 5:
		//奥义的判据是满查克拉；清零由紧随其后的 Server_ChangeChakra(0) 完成（同一 Actor，保序）
		if (PS->Chakra != 4) { SendPredictionResolve(PredictionKey, RejectResult); return; }
		break;
	default:
		//不在客户端请求集内的取值一律拒绝（原实现是照单全收）
		SendPredictionResolve(PredictionKey, RejectResult);
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

	//接受：回执放在写完之后 —— SendPredictionResolve 打包的是写后的状态（设计 2.11.1 的打包方式）
	SendPredictionResolve(PredictionKey, ConfirmResult);
}

// ---- 预测回执通道（设计 2.11.1 / 2.11.4，计划 2.6）----
// 服务器发、客户端收。服务器侧只有 SendPredictionResolve 一个出口；客户端落脚在组件
// UC_PredictionComponent::ResolvePrediction（设计 3.4.5 的统一结算入口）
void AC_PlayerController::SendPredictionResolve(const FPredictionKey& Key, uint8 Result)
{
	//降级路径：调用方没建键（KeyID == 0）就没有键可回执。服务器不来这一条的话，
	//"客户端关掉预测"会被误判成"回执丢了"，超时兜底还会去翻一个不存在的键
	if (!Key.IsValidKey())return;

	//调试开关（计划 2.6 工具表）：不回执 —— 验证客户端的超时兜底（设计 2.10.1，默认 2.0s）
	if (IsPredictionDropResolveEnabled())
	{
		UE_LOG(LogPrediction, Log, TEXT("Prediction.DropResolve: %s drops the resolve for key %u (result %u); the client must fall back to the prediction timeout"),
			*GetName(), Key.KeyID, Result);
		return;
	}

	//ConfirmedStatePacked：高 4 位 CharacterState、低 4 位 MySkill（设计 2.11.1）。
	//只有【确认】时客户端才消费它（校正本地非复制状态变量）；拒绝时客户端按权威值表与记录回滚，不看这个值
	uint8 ConfirmedStatePacked = 0;
	if (const AC_PlayerState* PS = GetPlayerState<AC_PlayerState>())
	{
		ConfirmedStatePacked = (uint8)(((uint8)PS->CharacterState << 4) | ((uint8)PS->MySkill & 0x0F));
	}

	UE_LOG(LogPrediction, Verbose, TEXT("SendPredictionResolve: %s key %u result %u packed %u"), *GetName(), Key.KeyID, Result, ConfirmedStatePacked);
	Client_ResolvePrediction(Key.KeyID, Result, ConfirmedStatePacked);
}

void AC_PlayerController::Client_ResolvePrediction_Implementation(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked)
{
	//这一行是"回执到达"的观察点（计划 2.6 的验收）：键不在键表里时，上面转交下去的结算会静默返回
	//（设计 3.4.5 的竞态约定），日志里看到它就说明回执确实到了客户端、且走的是那条约定路径
	UE_LOG(LogPrediction, Verbose, TEXT("Client_ResolvePrediction: %s key %u result %u packed %u"), *GetName(), KeyID, Result, ConfirmedStatePacked);

	if (AC_Character* Char = GetPawn<AC_Character>())
	{
		if (UC_PredictionComponent* Prediction = Char->PredictionComponent)
		{
			Prediction->ResolvePrediction(KeyID, Result, ConfirmedStatePacked);
		}
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

