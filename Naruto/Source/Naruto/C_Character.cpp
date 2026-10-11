
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
	}
	else if (bPreInputLock) {
		//转向走 RPC（设计 5.8）：原来直调 Server_ChangeToward_Implementation
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		//连段推进：预输入在推进点被消费成下一段，段号就在这里写 —— 这是"段的状态变换"本身
		//（动画状态机按 PS->Attack 切段）。输入点那次 +1 只发生在"锁为假 = 开新段"那一支
		//（Server_Attack_Implementation），续段的这一段由本处写：与接入前逐位一致
		//（3.3 实机返工：写入点既不在输入点、也没有被跳过）
		PS->Attack = attack;
		MyAttack = attack;
	}
	else bAttackInputLock = false;

	//锁更正（计划 2.5 / 设计 2.8）：本函数是"连段结束"（attack == 0）与"预输入没被消费"（else）
	//两条分支的写锁点，两条都只写 bAttackInputLock，掩码就给这一位。原来 attack == 0 直接 return，
	//现在收口到函数末尾（语义不变）：三个分支共用这一次下发，与设计 2.8 的调用点表两行对应
	SendLockCorrection(LockBitAttackInputLock);
}

void AC_Character::ChangeState(ECharacterStateType target)
{
	//权限语义（设计 5.8）：CharacterState 是服务器的量，本函数只由服务器路径调用
	//（BP_Character 的 I_ChangeState 权威分支 / AN_ChangeState）。
	//客户端那一半在 3.1 接上（落点写在计划 2.7 判定表：2.7 不动本接口），它走的是
	//PredictChangeState 那条预测接口，不从这个入口进来 —— 本地值只留一个写者
	if (!HasAuthority())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	PS->CharacterState = target;
}

void AC_Character::PredictChangeState(ECharacterStateType target)
{
	//AN_ChangeState 的客户端那一半（计划 3.1 / 2.7 判定表 / 设计 2.4.1、5.9a）。
	//CharacterState 不进技能键：本函数不建键、不绑生命周期，只做"本地先行写值 + 有活跃键就写标记"，
	//之后随权威值表采用 / 回滚（已定事项 22，与 Chakra 同路）。
	//两道门：只对拥有者本地控制的角色生效（远端的模拟代理不写，那一份等表），以及本片开关 ——
	//开关关掉时客户端一个字都不写，正是接入前"权威门控、只有服务器在写"的样子（2.0.4①）
	if (!IsLocallyControlled())return;
	if (PredictionComponent == nullptr)return;
	if (!PredictionComponent->CanPredict())return;
	if (CVarPredictionEnabled.GetValueOnGameThread() == 0)return;
	if (CVarPredictionSkill.GetValueOnGameThread() == 0)return;

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	PS->CharacterState = target;

	//标记只在"写这一刻还有活跃键"时写得进去 —— MarkReplicatedAttribute 拒绝已结算的键与 KeyID == 0
	//（设计 3.4.3 冻结的两个忽略条件）。本通知跑在技能动画的 100-300ms 处，而技能键半个 RTT
	//就被回执结算了，所以常态是【没有活跃键】：只写本地值，那一帧之后由表写回权威值 ——
	//这就是计划 3.1 记下的已知代价，也是 5.9a 说的"不挂在技能键下"（它天然挂不到）。
	//标记真正生效的场合是键还活着的那一段（丢回执 / 高延迟）：本地霸体被标记保住，随该键一起结算
	const uint32 KeyID = PredictionComponent->GetActivePredictionKeyID();
	if (KeyID != 0)
	{
		PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.CharacterState")), KeyID);
	}
}

void AC_Character::PredictChangeAttack(int32 attack)
{
	//AN_ChangeAttack 的客户端那一半（计划 3.3 / 2.7 判定表 / 设计 2.4.4）。
	//推进点做三件事，判据与服务器 ChangeAttack 逐条同源（含锁）：
	//【attack == 0】= 收招 —— 本地写段号 0 并开输入锁（服务器那一支也写 bAttackInputLock = false）；
	//【attack != 0 且预输入为真】= 连段推进 —— 这才是"段的状态变换"，本地写段号，动画状态机按
	//本地 PS->Attack 切到下一段（切段点因此落在动画通知这一刻，与接入前一致；3.3 实机返工的订正点）；
	//【attack != 0 且预输入为假】= 这一段的连段窗口没被消费 —— 本地开输入锁、不写段号（服务器那一支
	//只写 bAttackInputLock = false）。三处锁写都在本地落一份，理由见函数体里那段注释
	//两道门同 PredictChangeState：只管拥有者本地控制的那一份，以及本片开关
	//（开关关掉时客户端一个字都不写，正是接入前"只有服务器在写"的样子，2.0.4①）；
	//端别闸门（3.3 实机返工）：服务器不先行（设计 3.2），侦听服务器的宿主既是本机控制又是权威
	if (!IsLocallyControlled())return;
	if (HasAuthority())return;
	if (PredictionComponent == nullptr)return;
	if (!PredictionComponent->CanPredict())return;
	if (CVarPredictionEnabled.GetValueOnGameThread() == 0)return;
	if (CVarPredictionAttack.GetValueOnGameThread() == 0)return;

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;

	//锁的本地写（3.3 第二次实机返工，2026-10-11 两条实机缺陷的根因）：本通知与 AN_PreInput 一样是
	//【两端各自都有的通知】—— 服务器在它的 ChangeAttack 里写 bAttackInputLock，客户端就在自己这条
	//同源通知上写同一份，与 2.7 判定表给 bPreInputLock 的客户端半边（非权威分支直接置假）完全同构。
	//若这里不写、只等 Client_CorrectLocks，客户端的 bAttackInputLock 要比服务器晚 RTT/2 才开锁，
	//而 Attack() 的闸门 1（开新段 / 登记预输入）读的正是它：
	//  晚开的这段窗口里按下的键被本地判成"登记预输入"（不写段号），服务器却判成"开新段"（+1 并立刻
	//  切段）—— 服务器进了下一段、客户端停在原地（低延迟下要高频输入才撞得上这个窄窗口）；
	//  两端一旦因此错开一段，客户端还会在自己的本地锁已开、服务器仍在连段时判成"开新段"，多写一段
	//  再被回执拽回 —— 高延迟下这个窗口就是一个 RTT，越延迟越容易（两条实机反馈各占一条）
	if (attack == 0) {
		//收招（设计 2.4.4 的收招表：服务器写 0、拥有者客户端写 0 并置预测标记、其它端不写）
		PS->Attack = 0;
		bAttackInputLock = false;
	}
	else {
		//窗口没被消费：段号不写（服务器那一支也不写），只开输入锁
		if (!bPreInputLock)
		{
			bAttackInputLock = false;
			return;
		}
		PS->Attack = attack;
	}

	//两处段号本地写都是本地先行，标记只在"写这一刻还有活跃键"时写得进去（3.4.3 的两个忽略条件）。
	//本通知跑在动画的推进帧上，而本段键半个 RTT 就被回执结算了，所以常态是【没有活跃键】：
	//写本地值，随后由服务器同一处写入的同一个值到达（收招的 0、续段的段号，ChangeAttack 两支
	//都写）—— 值相等，采用规则不改写本地。这正是计划 3.3 记下的已知代价，与 3.1 的
	//PredictChangeState 同因同待遇。标记真正生效的场合是键还活着的那一段（丢回执 / 高延迟）
	const uint32 KeyID = PredictionComponent->GetActivePredictionKeyID();
	if (KeyID != 0)
	{
		PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.Attack")), KeyID);
	}
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
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();

	//预测接入点（设计 2.4.4 / 计划 3.3）。本地先行分两半，判据是同一个锁 ——
	//【锁为假 = 开新段】这一半"输入即出招"：服务器 Server_Attack_Implementation 在"锁为假"那一支
	//当场 +1，本地照它先行写段号；【锁为真、预输入窗口开着 = 登记预输入】这一半段号不动 ——
	//续段的那一次"状态变换"发生在动画推进点（AN_ChangeAttack，见 PredictChangeAttack），
	//这是接入前的行为，也是 3.3 实机返工订正的一处（上一版在输入点就 +1，连段在按键瞬间切段）。
	//两半都建键：一次输入、一段、一个键（设计 2.4.4 的粒度）。
	//两道闸门的服务器面完全同源：闸门 2 = 服务器的状态闸门（不通过时服务器什么都不做）；
	//闸门 1 = 这次输入算不算一段 —— 锁为假 或 窗口开着。两者都不满足时服务器会把这次输入
	//吞掉（窗口外的抢拍、同一个窗口里的第二次按键），本地也就不建键、不写值，走降级路径发空键
	//端别闸门（3.3 实机返工）：服务器不先行（设计 3.2）—— 侦听服务器的宿主既是 IsLocallyControlled
	//又是权威，光靠 IsLocallyControlled 挡不住它；它一旦本地先行，那条属于自己的 RPC 会在同一个
	//函数栈上把自己刚置好的锁看成"这次输入不算一段"，当场回执拒绝、本地段号被滚回，宿主普攻卡死
	uint32 KeyID = 0;
	if (IsLocallyControlled()
		&& !HasAuthority()
		&& PredictionComponent != nullptr
		&& PredictionComponent->CanPredict()
		&& CVarPredictionEnabled.GetValueOnGameThread() != 0
		&& CVarPredictionAttack.GetValueOnGameThread() != 0
		&& PS != nullptr
		&& (PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected)
		&& (bAttackInputLock == false || bPreInputLock == false))
	{
		//下一次普攻输入创建新键时先冻结旧键（设计 2.4.4 的"主路径"）：同一时刻只允许一个活跃键，
		//CreatePredictionKey 撞上活跃键会 ensure 并返回 0
		const uint32 OldKeyID = PredictionComponent->GetActivePredictionKeyID();
		if (OldKeyID != 0)
		{
			PredictionComponent->EndPredictionKey(OldKeyID);
		}
		KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Attack);
	}

	if (KeyID != 0) {
		//预输入标志本地先行：服务器在每一条请求上无条件把它置真（Server_Attack_Implementation 的
		//窗口采样之后那一行），本地跟着置真才守住"一个窗口只算一段"——否则同一个窗口里的第二次
		//按键会被本地算成第二段（本地多挥一段、服务器只推进一段，白晃一下）
		bPreInputLock = true;

		if (bAttackInputLock == false) {
			//开新段：与服务器同一支同步 —— 段号 +1 + 写标记。段号是表内字段（属性预测），
			//本地写值、结算交给权威值表的采用规则（设计 2.4.1 / 2.7.2）—— "被拒绝的一段"就是这条
			//标记随本键回滚：本地那一段被权威值表里的上一段拽回（设计 2.4.4 的"已接受的代价"）
			PS->Attack = PS->Attack + 1;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.Attack")), KeyID);
			//兜底（设计 2.4.4）：本键只覆盖"没有下一次预测"的结束路径 —— 收招、被打断
			//（PlayerStateReset 一次写 Attack = 0）。回执丢失时靠它冻结，正常路径由回执结算，
			//冻结与否都不影响标记的清除时机
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.Attack")), 0);

			//本地先行置锁（设计 2.6.1）：不建快照、不随段键回滚。解除有两条 —— 服务器写锁时的
			//Client_CorrectLocks 补发，和本地推进点 PredictChangeAttack 在同一条动画通知上的同名写入
			//（与 bPreInputLock 在 I_StartPreInput 客户端半边被置假同构；3.3 第二次实机返工）
			bAttackInputLock = true;

			//表现记录：攻击动画随键登记，供调试查键下内容。两个委托留空 —— 被拒时段号由权威值表回滚、
			//动画状态机按本地 PS->Attack 自己切回去，表现侧没有要手动撤销的东西
			PredictionComponent->RecordPresentation(FName(TEXT("AttackAnim")), FConfirmDelegate(), FRollbackDelegate());
		}
		//窗口那一半到此为止：段号不写（写入点在推进点，PredictChangeAttack），因此也没有标记可写。
		//本键仍然发出去 —— 它的回执说的是"服务器认没认下这次输入"，那正是本地要知道的（它只写了
		//一个锁标志，锁不进标记、也不随键回滚，解除有两条：服务器写锁时的 Client_CorrectLocks 补发，
		//以及本地推进点 PredictChangeAttack 在同一条动画通知上的同名写入）
	}

	//带键 RPC（设计 3.4.2：三个可带键 RPC 之一）。KeyID == 0 时传空键 = 非预测请求：
	//服务器照常校验执行、不回执，所以降级路径在这一行上与接入前逐位一致
	Server_Attack(KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
}

void AC_Character::Escape(const FInputActionValue& Value)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr;

	//预测接入点（设计 5.4 / 计划 3.2 的"落点"条）：与技能片不同的是，替身没有"本地 CD 归零"这种前置写法，
	//门就是服务器那四条的本地镜像（设计 2.4.4 判据同源：Chakra / EscapeCDState / 两个受击态）。
	//全过才建键；任一条不过 = 降级路径：不建键、不标记、不做任何本地先行，只发空键 RPC。
	//门里读的都是本地值（Chakra / CharacterState / LastEscapeTime 走权威值表，EscapeCDState 由 Tick 从
	//LastEscapeTime 推），本地可能比服务器旧一点点 —— 那时不预测，服务器照旧裁决（与接入前一致）
	uint32 KeyID = 0;
	//端别闸门（3.3 实机返工）：服务器不先行（设计 3.2）。侦听服务器的宿主既是本机控制又是权威，
	//它一旦本地先行，那条属于自己的 Server_Escape 会在同一个函数栈上再瞬移一次（落点按已经
	//瞬移过的位置重算）—— 宿主连瞬两次
	if (IsLocallyControlled()
		&& !HasAuthority()
		&& PredictionComponent != nullptr
		&& PredictionComponent->CanPredict()
		&& CVarPredictionEnabled.GetValueOnGameThread() != 0
		&& CVarPredictionEscape.GetValueOnGameThread() != 0
		&& PS != nullptr
		&& GameState != nullptr
		&& PS->Chakra != 0
		&& EscapeCDState == 0.f
		&& (PS->CharacterState == ECharacterStateType::Staggered || PS->CharacterState == ECharacterStateType::Launched))
	{
		KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Escape);
	}

	if (KeyID != 0) {
		//本地先行（设计 3.6.2 的替身示例）：服务器那条被接受的路径写了三样属性，这里一一对应。
		//三者都是表内字段（属性预测）：本地写值 + 写标记，结算交给权威值表的采用规则（设计 2.4.1 / 2.7.2）。
		//时间戳写本地值顺带把本机 CD 立刻拉起来（EscapeCDState 由 Tick 从它推），UI 与能否再按即刻一致
		LastEscapeTime = GameState->GetServerWorldTimeSeconds();
		PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.Char.LastEscapeTime")), KeyID);
		//保护态结束（服务器 Tick 写回 Normal）即本键的生命周期终点（设计 3.4.2 的多绑定 StateName）。
		//它只是回执丢失时的兜底冻结：正常路径由回执结算，冻结与否都不影响标记的清除时机
		PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.CharacterState")), (uint8)ECharacterStateType::Normal);

		PS->CharacterState = ECharacterStateType::Protected;
		PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.CharacterState")), KeyID);
		PS->Chakra = PS->Chakra - 1;
		PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.Chakra")), KeyID);

		//位置本地先行（设计 2.9 / 5.4）：先捕获一次性基线，再瞬移。落点与服务器同源（两边都调
		//ComputeEscapeTargetPlace）。位置不写标记（设计 3.4.3：拥有者收不到自身位置复制，等不到权威值），
		//回滚只看基线；被拒 / 超时由组件用 SetActorLocation(基线, TeleportPhysics) 送回。
		//基线返回的 bool 不看：上一行刚建出键，活跃键必然在；真要返回 false（键已被冻结/结算）也只说明
		//"这次位移没有基线兜底"，服务器权威路径照常走完，本机瞬移照旧
		PredictionComponent->RecordMoveBaseline();
		GetCharacterMovement()->StopMovementImmediately();
		SetActorLocation(ComputeEscapeTargetPlace());

		//表现记录（设计 3.6.2 末段）：替身动画 / 特效随键登记，供调试查键下内容。两个委托留空 ——
		//被拒时 CharacterState 由权威值表回到原值，动画状态机自己切回去，表现侧没有要手动撤销的东西
		PredictionComponent->RecordPresentation(FName(TEXT("EscapeAnim")), FConfirmDelegate(), FRollbackDelegate());
	}

	//带键 RPC（设计 3.4.2：三个可带键 RPC 之一）。KeyID == 0 时传空键 = 非预测请求：服务器照常校验
	//执行、不回执，所以降级路径在这一行上与接入前逐位一致（接入前就是一条不带参的 Server_Escape）
	Server_Escape(KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
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

		//预测接入点（设计 3.6.1 / 计划 3.0 固定动作 1）：本机控制（函数开头已判）+ 能预测 + 本片开关
		//四条都过才建键（末条端别闸门是 3.3 实机返工补的）。任一条不过 = 降级路径：不建键、
		//不标记、不做任何本地先行，只发空键 RPC
		uint32 KeyID = 0;
		//端别闸门（3.3 实机返工）：侦听服务器的宿主既是本机控制又是权威，服务器不先行（设计 3.2）
		if (!HasAuthority()
			&& PredictionComponent != nullptr
			&& PredictionComponent->CanPredict()
			&& CVarPredictionEnabled.GetValueOnGameThread() != 0
			&& CVarPredictionSkill.GetValueOnGameThread() != 0)
		{
			KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Skill);
		}

		if (KeyID != 0) {
			//本地先行（设计 3.6.1）：MySkill 是状态量、LastFirstSkillTime 是属性，两者都"本地写值 +
			//写标记"，结算交给权威值表（设计 2.4.1 / 2.7.2 的采用规则）。技能一不消耗查克拉，
			//所以本输入没有 Chakra 项 —— 那是奥义的事（计划 3.1 的"奥义"条）
			PS->MySkill = 1;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.MySkill")), KeyID);
			//技能结束（本地 MySkill 归 0）时自动冻结本键（设计 3.4.2 / 计划 3.1 的"冻结"条）
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.MySkill")), 0);
			LastFirstSkillTime = GameState->GetServerWorldTimeSeconds();
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.Char.LastFirstSkillTime")), KeyID);
			//表现记录（设计 3.6.1 末段）：技能动画随键登记，供调试查键下内容。
			//两个委托都留空 —— 被拒时 MySkill 由权威值表回到原值，动画状态机自己切回去，
			//表现侧没有需要手动撤销的东西
			PredictionComponent->RecordPresentation(FName(TEXT("SkillAnim")), FConfirmDelegate(), FRollbackDelegate());
		}
		else {
			//降级路径照旧记本地时间戳（接入前就是这个行为），只是没有标记、也就没有"等表写回"
			LastFirstSkillTime = GameState->GetServerWorldTimeSeconds();
		}

		//带键 RPC（设计 3.4.2：三个可带键 RPC 之一，技能请求只能落在它上面）。
		//KeyID == 0 时传空键 = 非预测请求：服务器照常校验执行、不回执（设计 3.4.2 末行），
		//所以降级路径在这一行上与接入前逐位一致
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(1,
			KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
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
		//预测接入点（计划 3.1 的"横向铺开"条，与 FirstSkill 同形）：三条都过才建键，
		//建不出来就是降级路径 —— 只发空键 RPC，与接入前逐位一致
		uint32 KeyID = 0;
		//端别闸门（3.3 实机返工）：侦听服务器的宿主既是本机控制又是权威，服务器不先行（设计 3.2）
		if (!HasAuthority()
			&& PredictionComponent != nullptr
			&& PredictionComponent->CanPredict()
			&& CVarPredictionEnabled.GetValueOnGameThread() != 0
			&& CVarPredictionSkill.GetValueOnGameThread() != 0)
		{
			KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Skill);
		}

		if (KeyID != 0) {
			//本地先行：MySkill = 2 + 标记 + 冻结，CD 时间戳照旧本地记一份并写标记
			PS->MySkill = 2;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.MySkill")), KeyID);
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.MySkill")), 0);
			LastSecondSkillTime = GameState->GetServerWorldTimeSeconds();
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.Char.LastSecondSkillTime")), KeyID);
			PredictionComponent->RecordPresentation(FName(TEXT("SkillAnim")), FConfirmDelegate(), FRollbackDelegate());
		}
		else {
			LastSecondSkillTime = GameState->GetServerWorldTimeSeconds();
		}

		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(2,
			KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
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
		//预测接入点（计划 3.1"横向铺开"条的"奥义"）：奥义同样成键（每个技能取值一个键，设计 2.4.4）
		uint32 KeyID = 0;
		//端别闸门（3.3 实机返工）：侦听服务器的宿主既是本机控制又是权威，服务器不先行（设计 3.2）
		if (!HasAuthority()
			&& PredictionComponent != nullptr
			&& PredictionComponent->CanPredict()
			&& CVarPredictionEnabled.GetValueOnGameThread() != 0
			&& CVarPredictionSkill.GetValueOnGameThread() != 0)
		{
			KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Skill);
		}

		if (KeyID != 0) {
			//本地先行：MySkill = 5 + 标记 + 冻结；奥义不记时间戳（它消耗查克拉，不是 CD，与服务器一致）
			PS->MySkill = 5;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.MySkill")), KeyID);
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.MySkill")), 0);
			//查克拉预扣：下面那条 Server_ChangeChakra(0) 不带键（设计 3.4.2 禁止同一次输入发两条带键 RPC），
			//所以标记登记在【本次输入的键】下，由该键的回执结算 —— Confirmed 只清标记（服务器也扣了，本地留 0）；
			//Rejected / 超时按设计 2.7.2 从权威值表写回：服务器没扣（它那条判据还要求 MySkill 已经是 5），表里仍是 4
			PS->Chakra = 0;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.Chakra")), KeyID);
			PredictionComponent->RecordPresentation(FName(TEXT("SkillAnim")), FConfirmDelegate(), FRollbackDelegate());
		}

		//顺序（计划 2.3）：技能请求排在查克拉清零之前。服务器的判据是"满查克拉才接受奥义"，
		//清零先到的话服务器看到的就是 0 了 —— 两条请求在同一 Actor 上，同通道、按序到达
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(5,
			KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
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
		//预测接入点（计划 3.1"横向铺开"条的"秘卷"）：MySkill = 4（秘卷与通灵同值、同动画，设计 2.4.4），
		//两个的区分量 SummonIndex 不进键判据，上面那条 Server_SetSummonIndex 照旧不带键、不做预测
		Server_SetSummonIndex(0);
		uint32 KeyID = 0;
		//端别闸门（3.3 实机返工）：侦听服务器的宿主既是本机控制又是权威，服务器不先行（设计 3.2）
		if (!HasAuthority()
			&& PredictionComponent != nullptr
			&& PredictionComponent->CanPredict()
			&& CVarPredictionEnabled.GetValueOnGameThread() != 0
			&& CVarPredictionSkill.GetValueOnGameThread() != 0)
		{
			KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Skill);
		}

		if (KeyID != 0) {
			PS->MySkill = 4;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.MySkill")), KeyID);
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.MySkill")), 0);
			LastScrollTime = GameState->GetServerWorldTimeSeconds();
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.Char.LastScrollTime")), KeyID);
			PredictionComponent->RecordPresentation(FName(TEXT("SkillAnim")), FConfirmDelegate(), FRollbackDelegate());
		}
		else {
			LastScrollTime = GameState->GetServerWorldTimeSeconds();
		}

		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(4,
			KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
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
		//预测接入点（计划 3.1"横向铺开"条的"通灵"）：与秘卷同值 4、共用同一个键判据与开关，
		//差别只在上面那条 Server_SetSummonIndex(1)（不带键、不进键判据）
		Server_SetSummonIndex(1);
		uint32 KeyID = 0;
		//端别闸门（3.3 实机返工）：侦听服务器的宿主既是本机控制又是权威，服务器不先行（设计 3.2）
		if (!HasAuthority()
			&& PredictionComponent != nullptr
			&& PredictionComponent->CanPredict()
			&& CVarPredictionEnabled.GetValueOnGameThread() != 0
			&& CVarPredictionSkill.GetValueOnGameThread() != 0)
		{
			KeyID = PredictionComponent->CreatePredictionKey((uint8)EPredictionType::Skill);
		}

		if (KeyID != 0) {
			PS->MySkill = 4;
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.PS.MySkill")), KeyID);
			PredictionComponent->BindStateLifecycle(KeyID, FName(TEXT("Self.PS.MySkill")), 0);
			LastSummonTime = GameState->GetServerWorldTimeSeconds();
			PredictionComponent->MarkReplicatedAttribute(FName(TEXT("Self.Char.LastSummonTime")), KeyID);
			PredictionComponent->RecordPresentation(FName(TEXT("SkillAnim")), FConfirmDelegate(), FRollbackDelegate());
		}
		else {
			LastSummonTime = GameState->GetServerWorldTimeSeconds();
		}

		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(4,
			KeyID != 0 ? PredictionComponent->GetActivePredictionKey() : FPredictionKey());
	}
}

//替身落点（计划 3.2 / 设计 5.4）：服务器与拥有者客户端共用本函数 —— 落点判据必须同源（设计 2.4.4）。
//逐行对应 Server_Escape_Implementation 原实现的那段循环（对手在 EscapeRange 内就取对手位置），
//只多两处判空（设计 5.8）：原实现对 Cast 结果与 GetPawn() 都没判空，前者遇到非 AC_PlayerState
//的 PlayerState 直接崩，后者在对手没有 Pawn 时崩
FVector AC_Character::ComputeEscapeTargetPlace() const
{
	FVector TargetPlace = GetActorLocation();

	const AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr;
	if (PS == nullptr || GameState == nullptr) {
		TargetPlace.Z = 0.f;
		return TargetPlace;
	}

	for (APlayerState* OtherPS : GameState->PlayerArray) {
		const AC_PlayerState* OtherMyPS = Cast<AC_PlayerState>(OtherPS);
		if (OtherMyPS == nullptr || OtherMyPS->Team == PS->Team)continue;
		const APawn* OtherPawn = OtherMyPS->GetPawn();
		if (OtherPawn == nullptr)continue;
		const float Distance = FVector::Distance(OtherPawn->GetActorLocation(), GetActorLocation());
		if (Distance <= EscapeRange)TargetPlace = OtherPawn->GetActorLocation();
	}

	TargetPlace.Z = 0.f;
	return TargetPlace;
}

void AC_Character::Server_Escape_Implementation(FPredictionKey PredictionKey)
{
	//Result 取值：0 = Confirmed，1 = Rejected（设计 2.11.1）。
	//键无效（KeyID == 0）时 SendPredictionResolve 自己跳过：降级路径照常校验执行、不回执
	//（设计 3.4.2 的约定），所以生产调用点传空键时行为与改造前逐位一致
	const uint8 RejectResult = 1;
	const uint8 ConfirmResult = 0;

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr;
	//PC 的判空是新增的（设计 5.8）：本函数的每一处回执、以及末尾扣查克拉都要通过它，原实现在那条路上
	//直接 Cast<AC_PlayerController>(Controller)->... 没有判空。判空命中只可能出现在"没有控制者的角色"上，
	//那条路上原实现本来也走不完（会崩），这里退化成"不执行、不回执"更贴切
	AC_PlayerController* PC = Cast<AC_PlayerController>(Controller);
	if (PC == nullptr || !PS || !GameState)return;

	//调试开关（计划 2.6 工具表）：带键请求一律拒绝、且不写权威值 —— 回滚路径 100% 可复现。
	//本片起它还要复现【位置回滚】：替身是第一个带位置基线的切片，被拒时位置必须回到基线
	//只作用于【带键】请求：不带键的请求没有本地预测可回滚，拒掉它只会让开关打开时玩不了
	if (PredictionKey.IsValidKey() && IsPredictionForceRejectEnabled())
	{
		UE_LOG(LogPrediction, Log, TEXT("Prediction.ForceReject: %s rejects the keyed escape request without writing anything"), *GetName());
		PC->SendPredictionResolve(PredictionKey, RejectResult);
		return;
	}

	//以下三条即客户端本地门的镜像（设计 2.4.4 判据同源：Chakra / EscapeCDState / 两个受击态）。
	//三者取反就是原来的嵌套 if ——原实现在不满足时什么都不做，现在多出一条回执（只对带键请求有意义）
	if (PS->Chakra == 0) { PC->SendPredictionResolve(PredictionKey, RejectResult); return; }
	if (EscapeCDState != 0.f) { PC->SendPredictionResolve(PredictionKey, RejectResult); return; }
	if (!(PS->CharacterState == ECharacterStateType::Staggered || PS->CharacterState == ECharacterStateType::Launched)) { PC->SendPredictionResolve(PredictionKey, RejectResult); return; }

	LastEscapeTime = GameState->GetServerWorldTimeSeconds();
	ProtectedStartTime = GameState->GetServerWorldTimeSeconds();
	ProtectedTime = EscapeProtectedTime;

	//受击框为无碰撞，进入保护状态
	Server_ChangeBox_Implementation(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 0);
	PS->CharacterState = ECharacterStateType::Protected;

	GetCharacterMovement()->StopMovementImmediately();

	//移动到替身位置（计划 3.2：落点计算抽成 ComputeEscapeTargetPlace，与拥有者客户端同源）
	SetActorLocation(ComputeEscapeTargetPlace());

	//查克拉减少
	PC->Server_ChangeChakra_Implementation(PS->Chakra - 1);

	//接受：回执放在权威值写完之后（设计 2.11.1）。上面那次扣查克拉是同一 Actor 上的直接调用，保序
	PC->SendPredictionResolve(PredictionKey, ConfirmResult);
}



void AC_Character::Server_Attack_Implementation(FPredictionKey PredictionKey)
{
	//回执出口（设计 2.11.1 / 计划 3.3）：0 = 接受，1 = 拒绝。KeyID == 0 的非预测请求由
	//SendPredictionResolve 自己跳过 —— 降级路径照常校验执行、不回执（设计 3.4.2 的约定），
	//所以生产调用点传空键时行为与改造前逐位一致
	const uint8 RejectResult = 1;
	const uint8 ConfirmResult = 0;
	//回执只对带键请求有意义，而带键请求必然来自拥有者客户端（有 PC）。PC 判空纯属防御：
	//命中时权威路径照走（原实现整条路都不用 PC），只是不回执
	AC_PlayerController* PC = Cast<AC_PlayerController>(Controller);

	//服务器侧强制拒绝（工具表 Prediction.ForceReject，仅非 Shipping）：带键请求一律拒绝、且一个字段都不写，
	//回滚路径 100% 可复现。只作用于【带键】请求 —— 不带键的请求没有本地预测可回滚，拒掉它只会让开关打开时玩不了。
	//拒绝路径没写锁，所以返回前要补发一次更正：客户端的本地段号与本地锁都还停在预测值上（设计 2.6.1）
#if !UE_BUILD_SHIPPING
	if (PredictionKey.IsValidKey() && IsPredictionForceRejectEnabled())
	{
		UE_LOG(LogPrediction, Log, TEXT("Prediction.ForceReject: %s rejects the keyed attack request without writing anything"), *GetName());
		SendLockCorrection(LockBitsAttackRequest);
		if (PC != nullptr)PC->SendPredictionResolve(PredictionKey, RejectResult);
		return;
	}
#endif

	//窗口状态必须在下面那次置真【之前】采样（设计 2.4.4）：本次输入算不算一段，看的是"输入到达时"
	//的那个窗口 —— 窗口开着（bPreInputLock 为假）= 这次输入会被推进点消费成连段；锁为假 = 这次输入开新段。
	//两者都不满足 = 服务器会把这次输入吞掉（窗口外的抢拍、或同一个窗口里的第二次按键）
	const bool bPreInputWindowOpen = (bPreInputLock == false);
	const bool bThisInputCounts = (bAttackInputLock == false) || bPreInputWindowOpen;

	bPreInputLock = true;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	//判空（设计 5.8）：下一行就要读 PS->CharacterState，原来没有任何判空
	//锁更正（设计 2.6.1 / 2.8）：本函数三个出口（含两条拒绝路径）都要在返回前按当时的最新锁值
	//补发一次 —— 被拒绝时服务器并没有写锁，客户端本地那次置位靠这一次才被解除，
	//否则本地闸门 1 会自锁（设计 2.6.1）。掩码只声明 bit0，理由见 LockBitsAttackRequest
	if (!PS) {
		SendLockCorrection(LockBitsAttackRequest);
		if (PC != nullptr)PC->SendPredictionResolve(PredictionKey, RejectResult);
		return;
	}
	if (!(PS->CharacterState == ECharacterStateType::Normal || PS->CharacterState == ECharacterStateType::Protected)) {
		SendLockCorrection(LockBitsAttackRequest);
		if (PC != nullptr)PC->SendPredictionResolve(PredictionKey, RejectResult);
		return;
	}

	//段号写入点（计划 3.3 / 设计 2.4.4）：服务器写的是"已请求的段数"，但一次输入只有在
	//【锁为假 = 开新段】时才当场 +1（顺带结算转向）；【锁为真、窗口开着 = 登记预输入】不算段 ——
	//续段的那一段由动画推进点的 ChangeAttack 写（它的 bPreInputLock 支），那里才是连段的状态变换。
	//被吞掉的那一种（锁为真、窗口也关着）一个字段都不写，只回拒绝
	//（3.3 实机返工：上一版让这次 +1 也覆盖了窗口那一支，两端都在按键瞬间切段，连段提前一段）
	if (bAttackInputLock == false) {
		if (TryTargetToward.X > 0)Server_ChangeToward_Implementation(true);
		if (TryTargetToward.X < 0)Server_ChangeToward_Implementation(false);
		bAttackInputLock = true;
		PS->Attack = PS->Attack + 1;
		MyAttack = PS->Attack;
	}

	//接受与否在权威值写完之后回执（设计 2.11.1）：同一 Actor 上的直接调用保序。
	//被吞掉的一次输入也要回执：客户端本地已经把锁先行置真（窗口那一半就只有这一个写入），
	//"服务器不认这次输入"是它唯一拿得到的信号；收到之后本地不会在推进点预测那一段
	//（设计 2.4.4 的"被拒绝的一段"：多挥一下随即弹回，段号由权威值表拽回）
	if (PC != nullptr)PC->SendPredictionResolve(PredictionKey, bThisInputCounts ? ConfirmResult : RejectResult);

	//被接受时的写锁下发也由这一次覆盖（设计 2.8 的第一行与第二行是同一个调用点）
	SendLockCorrection(LockBitsAttackRequest);
}

void AC_Character::Server_ChangeToward_Implementation(bool TargetToward)
{
	if(TargetToward) Flipbook->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
	else Flipbook->SetRelativeRotation(FRotator(180.f, 0.f, -90.f));
	Toward = TargetToward;
}

void AC_Character::SendLockCorrection(uint8 LockMask)
{
	//按掩码读出当前值打包（设计 2.8）：掩码外的位留 0，客户端也只写掩码覆盖的锁
	uint8 LockValues = (uint8)((bAttackInputLock ? LockBitAttackInputLock : 0)
		| (bPreInputLock ? LockBitPreInputLock : 0)
		| (bSuccessHit ? LockBitSuccessHit : 0));
	LockValues = (uint8)(LockValues & LockMask);

	UE_LOG(LogPrediction, Verbose, TEXT("SendLockCorrection: %s mask %u values %u"), *GetName(), LockMask, LockValues);
	Client_CorrectLocks(LockMask, LockValues);
}

void AC_Character::Client_CorrectLocks_Implementation(uint8 LockMask, uint8 LockValues)
{
	//客户端：直接覆盖本地锁值（设计 2.6.1 / 2.8）—— 不建键、不记录、不做任何结算。
	//只写掩码覆盖的锁：服务器没声明的那几位是客户端自己的本地先行值，不能拿服务器的旧值抹掉。
	//普攻被拒绝时，本地那次置位就是靠这条更正解除的。bPreInputLock 现在没有任何更正调用点：
	//它的两份（真/假）都由两端的同源事件各自写（输入点 Attack / 各自的 AN_PreInput，3.3 第二次实机返工）
	if (LockMask & LockBitAttackInputLock)bAttackInputLock = (LockValues & LockBitAttackInputLock) != 0;
	if (LockMask & LockBitPreInputLock)bPreInputLock = (LockValues & LockBitPreInputLock) != 0;
	if (LockMask & LockBitSuccessHit)bSuccessHit = (LockValues & LockBitSuccessHit) != 0;

	UE_LOG(LogPrediction, Verbose, TEXT("Client_CorrectLocks: %s mask %u values %u -> attackInputLock %d preInputLock %d successHit %d"),
		*GetName(), LockMask, LockValues, bAttackInputLock ? 1 : 0, bPreInputLock ? 1 : 0, bSuccessHit ? 1 : 0);
}

void AC_Character::ResetSuccessHit()
{
	//复位点（设计 5.6 / 计划 2.5）：置位与复位都归服务器，客户端调用直接忽略。
	//设计 5.6 那句"复位点与读取点同处"写在"读取点会消费它"的旧前提上，而蓝图的读取点
	//（BP_Menma 的 I_HitJump）只读不写——真正的复位点是 BP_Character 的 I_StartHitCheck（权威分支）。
	//本包按"不引入新的复位时机"保留这个既有复位点，只把它接进更正通道（已定事项 34）
	if (!HasAuthority())return;

	bSuccessHit = false;

	//不做"已经是假就不下发"的提前返回：客户端那一份可能还停在真上（比如它收到的那次置位更正之后
	//再没收到过别的），这次下发正好把它拉回来；重复下发是幂等的
	SendLockCorrection(LockBitSuccessHit);
}

void AC_Character::OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor == this)return;
	if (HasAuthority()) {
		FVector Effect = DamageEffect;
		if (Toward == false)Effect.X = -Effect.X;

		//成功命中
		bSuccessHit = true;

		//锁更正（计划 2.5 / 设计 2.8）：命中标志的置位点。复位点见 ResetSuccessHit
		//（蓝图 BP_Character 的 I_StartHitCheck 权威分支调用它，已定事项 34）
		SendLockCorrection(LockBitSuccessHit);

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

