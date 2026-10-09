
// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Character.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "PaperFlipbookComponent.h"
#include "PaperSpriteComponent.h"
#include "PaperZDAnimationComponent.h"
#include "PaperZDAnimInstance.h"
#include "C_PlayerController.h"
#include "C_PlayerState.h"
#include "C_ArenaGM.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"
#include "C_GrabPoinnt.h"
#include "C_PredictionComponent.h"
#include "C_AuthorityValueComponent.h"

AC_Character::AC_Character()
{
	PrimaryActorTick.bCanEverTick = true;
	//碰撞框
	AttackBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AttackBox"));
	AttackBox->SetupAttachment(RootComponent);
	AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackBox->SetCollisionObjectType(ECC_EngineTraceChannel2);
	AttackBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	AttackBox->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	AttackBox->SetIsReplicated(true);

	PlayerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlayerBox"));
	PlayerBox->SetupAttachment(RootComponent);
	PlayerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PlayerBox->SetCollisionObjectType(ECC_EngineTraceChannel1);
	PlayerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PlayerBox->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Overlap);
	PlayerBox->SetIsReplicated(true);

	//攻击框绑定
	AttackBox->OnComponentBeginOverlap.AddDynamic(this, &AC_Character::OnAttackBoxOverlap);

	//动画表现
	PlaceMark = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("PaperSpriteComponent"));
	Flipbook = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("FlipbookComponent"));
	PaperZD = CreateDefaultSubobject<UPaperZDAnimationComponent>(TEXT("PaperZDComponent"));
	if (PlaceMark) {
		PlaceMark->SetupAttachment(RootComponent);
		PlaceMark->SetRelativeRotation(FRotator(0.f, 0.0f, -90.0f));
		PlaceMark->SetIsReplicated(true);
	}
	if (Flipbook)
	{
		Flipbook->SetupAttachment(RootComponent);
		Flipbook->SetRelativeRotation(FRotator(0.f, 0.0f, -90.0f));
		Flipbook->SetIsReplicated(true);
		if (PaperZD) {
			PaperZD->InitRenderComponent(Flipbook);
		}
	}

	//预测组件（设计 3.2：构造函数里 CreateDefaultSubobject，不手动 SetupAttachment）
	PredictionComponent = CreateDefaultSubobject<UC_PredictionComponent>(TEXT("PredictionComponent"));

	//权威值表组件（设计 2.7.5 / 计划 2.4：同上，bReplicates 由组件自己的构造函数置位）
	AuthorityValueComponent = CreateDefaultSubobject<UC_AuthorityValueComponent>(TEXT("AuthorityValueComponent"));

	//网络复制
	bReplicates = true;
	SetReplicateMovement(true);
}

void AC_Character::BeginPlay()
{
	Super::BeginPlay();

	//预测系统：初始化预测上下文（设计 3.4.1 / 计划 2.1）。
	//客户端上 PlayerState 与 Controller 都是随后同步到达的，这一次多半失败 —— 失败不是错误，
	//由 Tick 里的重试补上。这里不判断返回值，避免"数据没到"被当成异常
	if (PredictionComponent)
	{
		PredictionComponent->InitializePredictionContext();
	}
}

void AC_Character::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (HasAuthority())return;
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (!PS)return;

	//Team同步完毕直接初始化，反之绑定委托等待同步
	if (Cast<AC_PlayerState>(GetPlayerState())->Team == ETeamType::None) {
		PS->OnTeamChanged.AddDynamic(this, &AC_Character::OnTeamChanged);
	}
	else MyInitialize(Cast<AC_PlayerState>(GetPlayerState())->Team);
}

void AC_Character::OnTeamChanged()
{
	//同步队伍信息进行初始化
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (PS && PS->Team != ETeamType::None)
	{
		MyInitialize(PS->Team);
	}
}

void AC_Character::MyInitialize(ETeamType team)
{
	//根据队伍信息设置朝向和位置标记
	//本函数是在客户端上跑的（OnRep_PlayerState / OnTeamChanged），朝向必须走 RPC 交给服务器改、
	//再经 Toward 复制回来：原来直调 Server_ChangeToward_Implementation 只会改本地那一份（设计 5.8）
	if (team == ETeamType::Red) {
		if(Toward)Server_ChangeToward(false);
		PlaceMark->SetSpriteColor(FColor::Red);
	}
	else {
		if(!Toward)Server_ChangeToward(true);
		PlaceMark->SetSpriteColor(FColor::Blue);
	}

	//预测系统（计划 2.4）：本函数正是"PlayerState 与 Team 都就绪"的时刻（两端各自执行 ——
	//服务器在 SpawnPawnToPlayer 里、客户端在 OnRep_PlayerState / OnTeamChanged 里），而敌方 PS 的
	//查找要等 Team 到齐才可能命中。Tick 里的重试门槛是 CanPredict()（只看自身三项），自身到齐后
	//就不再重试，于是首次初始化若早于 Team，敌方那一格会一直空着（已定事项 20 允许留空，
	//但没有任何后续时机补绑）。这里补一次初始化：重复调用安全（重绑即覆盖，已定事项 21），
	//也不新增接口（设计 3.7 的扩展原则）
	if (PredictionComponent)
	{
		PredictionComponent->InitializePredictionContext();
	}
}

void AC_Character::BeDameged(float Damage, ECharacterStateType State, EAttackType Type, FVector Effect, float Time, AC_GrabPoinnt* GrabPoint)
{
	//抓取点绑定
	if (Type == EAttackType::Grab) {
		if(!BeGrabbedPoint || BeGrabbedPoint->bIsUsing == false) BeGrabbedPoint = GrabPoint;
	}

	//在PC中处理受击
	Cast<AC_PlayerController>(Controller)->PlayerGetDamage(Damage, State,Type, Effect, Time);
}

void AC_Character::SetOtherPauseState(bool state)
{
	if (!HasAuthority())return;
	AC_ArenaGM* MyGameMode = Cast<AC_ArenaGM>(UGameplayStatics::GetGameMode(this));
	if (!MyGameMode)return;
	if (GetPlayerState<AC_PlayerState>()->Team == ETeamType::Blue)MyGameMode->SetPlayerPauseState(1, state);
	else MyGameMode->SetPlayerPauseState(0, state);
}

void AC_Character::Mult_ChangeBoxSize_Implementation(FVector Size, FVector Offset, int32 Box)
{
	//根据标记确认更改的碰撞框
	UBoxComponent* TargetBox;
	if (Box == 0)TargetBox = PlayerBox;
	else TargetBox = AttackBox;

	FVector TargetSize = Size;
	if (TargetBox == AttackBox) {//受击时无法启动攻击框
		AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
		if (!PS)return;
		ECharacterStateType State = PS->CharacterState;
		if (State == ECharacterStateType::Staggered || State == ECharacterStateType::Launched || State == ECharacterStateType::Grabbed)TargetSize = FVector(0.f, 0.f, 0.f);
	}

	//根据碰撞体尺寸设置碰撞性
	if (TargetSize.IsNearlyZero())TargetBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	else TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	//根据朝向设置碰撞体翻转
	if (!Toward)Offset.X = -Offset.X;
	TargetBox->SetRelativeLocation(Offset);

	//更改碰撞体大小
	TargetBox->SetBoxExtent(TargetSize);
}

void AC_Character::AddChakra()
{
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (!PS)return;
	//改为走 RPC 并判空（设计 5.8）：原来直调 Server_ChangeChakra_Implementation，
	//客户端上那条路只会改本地那一份查克拉，服务器随后复制回来把它盖掉
	AC_PlayerController* PC = Cast<AC_PlayerController>(Controller);
	if (!PC)return;
	if (PS->Chakra < 4)PC->Server_ChangeChakra(PS->Chakra + 1);
}

void AC_Character::Server_ChangeBox_Implementation(FVector Size, FVector Offset, int32 Box)
{
	
	if (Box == 0) {
		//保护状态受击框保持为无碰撞
		if (GetPlayerState<AC_PlayerState>()->CharacterState == ECharacterStateType::Protected)return;
	}

	//同步更改服务器和客户端的碰撞体
	Mult_ChangeBoxSize(Size,Offset, Box);
}

void AC_Character::Server_SetSummonIndex_Implementation(int32 target)
{
	SummonIndex = target;
}

void AC_Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	//预测系统（设计 5.2b / 计划 2.4）：Toward 与 LastEscapeTime 两条 DOREPLIFETIME 已删除，
	//改由 AuthorityValueComponent.AuthorityValueTable 下发。本函数保留（将来若有新的复制属性仍在此登记）
}

void AC_Character::ChangeAttack(int32 attack)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	if (attack == 0) {
		PS->Attack = 0;
		PS->MySkill = 0;
		MyAttack = 0;
		MySkill = 0;
		PS->CharacterState = ECharacterStateType::Normal;

		bAttackInputLock = false;
		return;
	}
	if (bPreInputLock) {
		//转向走 RPC（设计 5.8）：原来直调 Server_ChangeToward_Implementation
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		PS->Attack = attack;
		MyAttack = attack;
	}
	else bAttackInputLock = false;
}

void AC_Character::ChangeState(ECharacterStateType target)
{
	//权限语义（设计 5.8）：CharacterState 是服务器的量，本函数只由服务器路径调用
	//（BP_Character 的 I_ChangeState 权威分支 / AN_ChangeState）。
	//客户端那一半在 2.7 接上，届时走预测接口本地先行（设计 5.9a / 已定事项 22），
	//不从这个入口进来 —— 本地值只留一个写者
	if (!HasAuthority())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	PS->CharacterState = target;
}

void AC_Character::MakeMove(FVector Offset, FVector2D TargetToward)
{
	//权限语义（设计 5.8 / 2.9）：两端各自执行。AddActorLocalOffset 没有权威检查
	//（引擎的 MoveComponentImpl 只做移动、不报错），服务器这一半是权威位移；
	//客户端那一半在 2.7 / 3.4 接上（动画通知内先行 + RecordMoveBaseline），本函数不做端别判定

	//根据意图改变方向
	FVector MyOffset = Offset;
	if (TargetToward.Y == 0.f)MyOffset.Y = 0;
	else if (TargetToward.Y < 0.f)MyOffset.Y = -MyOffset.Y;
	if (!Toward)MyOffset.X = -MyOffset.X;

	//位置限制
	FVector MyLocation = GetActorLocation();
	if (MyLocation.X + MyOffset.X > MaxLocation.X)MyOffset.X = MaxLocation.X - MyLocation.X;
	if (MyLocation.X + MyOffset.X < MinLocation.X)MyOffset.X = MinLocation.X - MyLocation.X;
	if (MyLocation.Y + MyOffset.Y > MaxLocation.Y)MyOffset.Y = MaxLocation.Y - MyLocation.Y;
	if (MyLocation.Y + MyOffset.Y < MinLocation.Y)MyOffset.Y = MinLocation.Y - MyLocation.Y;

	AddActorLocalOffset(MyOffset);
}

void AC_Character::StartPreInput()
{
	//打开预输入窗口（AN_PreInput，计划 2.2 / 已定事项 24）。
	//置假之后，窗口内的一次普攻输入会由 Server_Attack_Implementation 把它置回真，
	//推进点的 AN_ChangeAttack -> ChangeAttack 消费它决定是否连段变换
	bPreInputLock = false;

	//同时清掉残留的移动意图：TryTargetToward 只由 Server_SetTryTargetToward 刷新，
	//玩家松开方向键后客户端不再发新值，服务器上会留着上一次的方向，
	//而 ChangeAttack / Server_Attack 的转向判定会读它
	TryTargetToward = FVector2D(0.f, 0.f);
}

void AC_Character::Server_SetTryTargetToward_Implementation(FVector2D TargetToward)
{
	TryTargetToward = TargetToward;
}

void AC_Character::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		//获取移动意图
		TryTargetToward.X = MovementVector.Y;
		TryTargetToward.Y = MovementVector.X;
		Server_SetTryTargetToward(TryTargetToward);

		AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
		if(!PS)return;

		//移动可行性判断
		if(PS->Attack != 0 || PS->MySkill != 0)return;
		if (PS->CharacterState == ECharacterStateType::Staggered)return;
		if (PS->CharacterState == ECharacterStateType::Launched)return;
		if (PS->CharacterState == ECharacterStateType::Grabbed)return;

		//转向处理
		if (MovementVector.Y > 0 && !Toward)Server_ChangeToward(true);
		if (MovementVector.Y < 0 && Toward)Server_ChangeToward(false);
		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X / 2.0f);
	}
}

void AC_Character::Attack(const FInputActionValue& Value)
{
	Server_Attack();
}

void AC_Character::Escape(const FInputActionValue& Value)
{
	//服务器进行替身
	Server_Escape();
	
}

void AC_Character::FirstSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!GameState)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	Server_ChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (FirstSkillCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(1);
		LastFirstSkillTime = GameState->GetServerWorldTimeSeconds();
		BP_FirstSkillEffect();
	}
}

void AC_Character::SecondSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!GameState)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	Server_ChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (SecondSkillCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(2);
		LastSecondSkillTime = GameState->GetServerWorldTimeSeconds();
		BP_SecondSkillEffect();
	}
}

void AC_Character::FinalSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	Server_ChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (PS->Chakra == 4) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		//顺序（计划 2.3）：技能请求排在查克拉清零之前。服务器的判据是"满查克拉才接受奥义"，
		//清零先到的话服务器看到的就是 0 了 —— 两条请求在同一 Actor 上，同通道、按序到达
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(5);
		Cast<AC_PlayerController>(Controller)->Server_ChangeChakra(0);
		BP_FinalSkillEffect();
	}
}

void AC_Character::Scroll(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	Server_ChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (!GameState)return;
	if (ScrollCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Server_SetSummonIndex(0);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(4);
		LastScrollTime = GameState->GetServerWorldTimeSeconds();
	}
}

void AC_Character::Summon(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	Server_ChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (!GameState)return;
	if (SummonCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Server_SetSummonIndex(1);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(4);
		LastSummonTime = GameState->GetServerWorldTimeSeconds();
	}
}

void AC_Character::Server_Escape_Implementation()
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!GameState)return;
	if (!PS)return;
	if (PS->Chakra == 0)return;
	if (EscapeCDState == 0.f) {
		if (PS->CharacterState == ECharacterStateType::Staggered || PS->CharacterState == ECharacterStateType::Launched) {
			LastEscapeTime = GameState->GetServerWorldTimeSeconds();
			ProtectedStartTime = GameState->GetServerWorldTimeSeconds();
			ProtectedTime = EscapeProtectedTime;

			//受击框为无碰撞，进入保护状态
			Server_ChangeBox_Implementation(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 0);
			PS->CharacterState = ECharacterStateType::Protected;

			GetCharacterMovement()->StopMovementImmediately();

			//移动到替身位置
			FVector TargetPlace = GetActorLocation();
			for (APlayerState* OtherPS : GameState->PlayerArray) {
				if (Cast<AC_PlayerState>(OtherPS)->Team != PS->Team) {
					float Distance = FVector::Distance(OtherPS->GetPawn()->GetActorLocation(), GetActorLocation());
					if (Distance <= EscapeRange)TargetPlace = OtherPS->GetPawn()->GetActorLocation();
				}
			}
			TargetPlace.Z = 0.f;
			SetActorLocation(TargetPlace);

			//查克拉减少
			Cast<AC_PlayerController>(Controller)->Server_ChangeChakra_Implementation(PS->Chakra - 1);
		}
	}
}



void AC_Character::Server_Attack_Implementation()
{
	bPreInputLock = true;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	//判空（设计 5.8）：下一行就要读 PS->CharacterState，原来没有任何判空
	if (!PS)return;
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected))return;
	if (bAttackInputLock == false) {
		if (TryTargetToward.X > 0)Server_ChangeToward_Implementation(true);
		if (TryTargetToward.X < 0)Server_ChangeToward_Implementation(false);
		bAttackInputLock = true;
		PS->Attack = PS->Attack + 1;
		MyAttack = PS->Attack;
	}
}

void AC_Character::Server_ChangeToward_Implementation(bool TargetToward)
{
	if(TargetToward) Flipbook->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
	else Flipbook->SetRelativeRotation(FRotator(180.f, 0.f, -90.f));
	Toward = TargetToward;
}

void AC_Character::OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor == this)return;
	if (HasAuthority()) {
		FVector Effect = DamageEffect;
		if (Toward == false)Effect.X = -Effect.X;

		//成功命中
		bSuccessHit = true;

		//被攻击的对象受到伤害
		Cast<AC_Character>(OtherActor)->BeDameged(DamageValue, DamageState,DamageType, Effect, EffectTime,MyGrabPoint);
	}
}

// Called every frame
void AC_Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//每帧获取角色信息
	GetInformation();

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!GameState)return;

	//预测系统：上下文重试 + 超时兜底（设计 5.7 / 3.4.5）。
	//位置必须在这里 —— 上一行是 PS / GameState 的空指针早退，早退期间进不到这里，
	//否则角色数据未就绪的那段时间超时检查会静默停摆（设计 5.7）
	if (PredictionComponent)
	{
		//PlayerState / Controller 在客户端是逐帧到达的：上下文还无效时每帧重试一次初始化。
		//就绪后 CanPredict() 为真即不再重试（重绑即覆盖，重复调用本就安全，设计 3.4.1）
		if (!PredictionComponent->CanPredict())
		{
			PredictionComponent->InitializePredictionContext();
		}

		//超时兜底（阶段二还没有键，这里是空转；Prediction.Draw 的绘制也挂在这次调用的开头）
		PredictionComponent->TickPredictionTimeout(DeltaTime);
	}

	//根据角色高度同步动画高度
	if (GetActorLocation().Z > 0) {
		Flipbook->SetRelativeLocation(FVector(0, -GetActorLocation().Z,0));
	}

	//角色移动范围限制
	if (PS->CharacterState != ECharacterStateType::Grabbed) {//被抓取时不限制
		if (GetActorLocation().X > MaxLocation.X)SetActorLocation(FVector(MaxLocation.X, GetActorLocation().Y, GetActorLocation().Z));
		if (GetActorLocation().Y > MaxLocation.Y)SetActorLocation(FVector(GetActorLocation().X, MaxLocation.Y, GetActorLocation().Z));
		if (GetActorLocation().X < MinLocation.X)SetActorLocation(FVector(MinLocation.X, GetActorLocation().Y, GetActorLocation().Z));
		if (GetActorLocation().Y < MinLocation.Y)SetActorLocation(FVector(GetActorLocation().X, MinLocation.Y, GetActorLocation().Z));
	}

	//受击处理
	if (HasAuthority()) {//服务器控制
		if (!PS)return;
		if (PS->CharacterState == ECharacterStateType::Protected) {
			if (bInProtectAnim == false) {
				Mult_ChangeProtectedAnim(true);
				bInProtectAnim = true;
			}

			//结束保护
			if ((GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - ProtectedStartTime) > ProtectedTime) {
				if (bInProtectAnim == true) {
					Mult_ChangeProtectedAnim(false);
					bInProtectAnim = false;
				}
				PS->CharacterState = ECharacterStateType::Normal;
			}
		}
		if (PS->CharacterState == ECharacterStateType::Launched) {
			if (LaunchState == 0 && GetActorLocation().Z > 3.f)LaunchState = 1;
			if (LaunchState == 1 && GetActorLocation().Z < 3.f) {
				LaunchCharacter(FVector(0.f, 0.f, 200.f), true, true);
				LaunchState = 2;
			}
			if (LaunchState == 2 && GetActorLocation().Z > 3.f)LaunchState = 3;
			if (LaunchState == 3 && GetActorLocation().Z < 3.f) {
				//受击框为无碰撞，进入起身保护状态
				Server_ChangeBox_Implementation(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 0);
				PS->CharacterState = ECharacterStateType::Protected;
				ProtectedStartTime = GameState->GetServerWorldTimeSeconds();
				ProtectedTime = StandProtectedTime;
				LaunchState = 0;
			}
		}
		if (PS->CharacterState == ECharacterStateType::Grabbed) {
			//触发条件与频率（设计 5.8）：被抓期间每帧比对"自己"与"被抓点"的位置，不一致就发一次多播，
			//两端一起挪过去 —— 这是连续跟随（抓取点会随抓取者的动画移动），不是"状态变化时调一次"；
			//抓取点静止且自己已在位时不发。多播本身是 Reliable 的，发送频率由"抓取点是否移动"决定
			if (BeGrabbedPoint && BeGrabbedPoint->bIsUsing) {
				const FVector GrabLocation = BeGrabbedPoint->GetActorLocation();
				if(GetActorLocation() != GrabLocation)Mult_ChangeGrabLocation(GrabLocation);
			}
			else {
				Mult_ChangeGravity(true);
				GetCharacterMovement()->GravityScale = 1.f;
				PS->CharacterState = ECharacterStateType::Launched;
			}
		}
	}

	//CD处理
	//替身
	if (LastEscapeTime != 0.f) {
		EscapeCDState = EscapeCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastEscapeTime);
		if (EscapeCDState <= 0.f)EscapeCDState = 0.f;
	}
	else EscapeCDState = 0.f;

	//一技能
	if (LastFirstSkillTime != 0.f) {
		FirstSkillCDState = FirstSkillCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastFirstSkillTime);
		if (FirstSkillCDState <= 0.f)FirstSkillCDState = 0.f;
	}
	else FirstSkillCDState = 0.f;


	//二技能
	if (LastSecondSkillTime != 0.f) {
		SecondSkillCDState = SecondSkillCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastSecondSkillTime);
		if (SecondSkillCDState <= 0.f)SecondSkillCDState = 0.f;
	}
	else SecondSkillCDState = 0.f;

	//秘卷
	if (LastScrollTime != 0.f) {
		ScrollCDState = ScrollCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastScrollTime);
		if (ScrollCDState <= 0.f)ScrollCDState = 0.f;
	}
	else ScrollCDState = 0.f;

	//通灵
	if (LastSummonTime != 0.f) {
		SummonCDState = SummonCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastSummonTime);
		if (SummonCDState <= 0.f)SummonCDState = 0.f;
	}
	else SummonCDState = 0.f;
}

void AC_Character::Mult_ChangeProtectedAnim_Implementation(bool show)
{
	if (!Flipbook)return;
	if(show)Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f));
	else Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
}

void AC_Character::Mult_ChangeGravity_Implementation(bool able)
{
	if (able) {
		GetCharacterMovement()->GravityScale = 1.f;
		return;
	}
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->GravityScale = 0.f;
}

void AC_Character::Mult_ChangeGrabLocation_Implementation(FVector target)
{
	SetActorLocation(target);
}

void AC_Character::GetInformation()
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	MySpeed = GetVelocity().Length();
	MyAttack = PS->Attack;
	MyCState = PS->CharacterState;
	MySkill = PS->MySkill;
}

// Called to bind functionality to input
void AC_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	//输入绑定
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AC_Character::Move);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AC_Character::Attack);
		EnhancedInputComponent->BindAction(EscapeAction, ETriggerEvent::Triggered, this, &AC_Character::Escape);
		EnhancedInputComponent->BindAction(FirstSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FirstSkill);
		EnhancedInputComponent->BindAction(SecondSkillAction, ETriggerEvent::Triggered, this, &AC_Character::SecondSkill);
		EnhancedInputComponent->BindAction(FinalSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FinalSkill);
		EnhancedInputComponent->BindAction(ScrollAction, ETriggerEvent::Triggered, this, &AC_Character::Scroll);
		EnhancedInputComponent->BindAction(SummonAction, ETriggerEvent::Triggered, this, &AC_Character::Summon);
	}
}

