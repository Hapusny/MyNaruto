
// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Character.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"
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

//位移根的时长（计划 3.4 重做的"位移时长"条）：MoveToForce 是【沿路径插值】的源
//（MoveFraction = (已走时间 + 本次 tick) / Duration，见 RootMotionSource.cpp:745-751），
//所以时长必须 >= 一次移动 tick，否则 Lerp 会外插到目标之外 —— 低帧率下那一步就是"冲过头"。
//移动组件的单步上限是 MaxSimulationTimeStep = 0.05（CharacterMovementComponent.cpp:560 的默认值，
//引擎自己会把更长的帧切成 <= 0.05 的步），所以 0.05 是"绝不外插"的最小取值：
//60 fps 下约三帧走完，观感与接入前的裸写位置几乎一样。想更"滑"就调大（0.1~0.2），两端自动一致
static constexpr float MoveRootMotionDuration = 0.05f;

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

	//碰撞框切片（计划 3.5 / 设计 5.9b）：把两个框的【授权初值】捕进"通知几何"。
	//受击框在构造函数里就是 QueryOnly、尺寸与偏移由蓝图配好（正常玩法下没人会在开局写它），
	//所以它的初值从组件上取 —— 等价于接入前"没人动过它"的样子，派生出来的碰撞启用也与它一致。
	//攻击框反过来：构造函数里是 NoCollision、由通知来开。如果在这里也把蓝图那份尺寸取进来，
	//派生会按"尺寸非零 → QueryOnly"在开局就把它打开 —— 站在那儿的角色凭这一份静止的框就能打人。
	//所以攻击框的尺寸初值固定为零（等价于接入前"要等第一条 AN_ChangeAttackBox 通知才生效"），
	//它的偏移初值仍然从组件上取（尺寸为零时偏移无意义，取组件值只是为了不引入一次无谓的写入）
	if (PlayerBox)
	{
		NotifiedPlayerBoxSize = PlayerBox->GetUnscaledBoxExtent();
		NotifiedPlayerBoxOffset = PlayerBox->GetRelativeLocation();
	}
	NotifiedAttackBoxSize = FVector::ZeroVector;
	if (AttackBox)
	{
		NotifiedAttackBoxOffset = AttackBox->GetRelativeLocation();
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
	//本函数两端都会跑（客户端在 OnRep_PlayerState / OnTeamChanged 里、服务器在 SpawnPawnToPlayer 里）；
	//客户端那一侧的朝向必须走 RPC 交给服务器改、再经 Toward 复制回来：
	//原来直调 Server_ChangeToward_Implementation 只会改本地那一份（设计 5.8）
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

	//碰撞框切片（计划 3.5 / 设计 5.9b）：这条路（`Server_ChangeBox` RPC -> 本函数）是【降级路径】——
	//Prediction.Box 打开时它没有调用点：蓝图 `I_ChangeBox` 已改为无条件调 `RequestChangeBox`
	//（以 BlueprintSideCallSiteInventory.md 为准，编辑器侧的最终确认见该文档的"待编辑器确认"）。
	//万一还有旧路径走到这里，就把"通知几何"
	//一并更新，让 Tick 里那一遍派生与它同值、不打架；否则派生会每帧把刚写的框拽回旧值（攻击框表现为全灭）。
	//存进去的是【通知原值】而不是上面那道闸门之后的 `TargetSize`：闸门由派生自己按同一个 PS->CharacterState
	//重算（归零会直接写进"通知几何"，离开受击态不会自己回来 —— 与接入前一致）。
	//开关关掉时这几行只是几个成员写入：派生整段不做、没有任何读数者，组件那三次写入与接入前逐位相同
	if (Box == 0)
	{
		NotifiedPlayerBoxSize = Size;
		NotifiedPlayerBoxOffset = Offset;
	}
	else
	{
		NotifiedAttackBoxSize = Size;
		NotifiedAttackBoxOffset = Offset;
	}
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

//---- 碰撞框切片（计划 3.5 / 设计 5.9b）----

void AC_Character::RequestChangeBox(FVector Size, FVector Offset, int32 Box)
{
	//统一入口（全工程每一处框的写入都从这里走：蓝图 I_ChangeBox + C++ 的九个调用点）。
	//开关打开时【两端各自就地应用】：通知在哪一端跑，哪一端当场改自己那一份几何 ——
	//拥有者本地因此不再等一个 RTT（接入前：拥有者的几何要等自己发的 RPC 到服务器、
	//服务器再 Mult_ChangeBoxSize 播回来），服务器那一份命中几何也改由服务器自己的动画
	//通知驱动（它本来就该是权威的那一份）
	if (CVarPredictionBox.GetValueOnGameThread() != 0)
	{
		ApplyChangeBoxLocal(Size, Offset, Box);
		return;
	}

	//开关关掉 = 逐字等于接入前：只有本地控制端执行（原蓝图 I_ChangeBox 的"本地控制时"门），
	//经 Server_ChangeBox RPC 交给服务器、再由 Mult_ChangeBoxSize 播给所有人
	if (!IsLocallyControlled())return;
	Server_ChangeBox(Size, Offset, Box);
}

void AC_Character::ClearBox(int32 Box)
{
	//把某个框关掉（尺寸归零 = 无碰撞）。四处服务器侧调用点共用（受击打断 / 替身落点 /
	//起身保护 / 技能输入的服务器半边）。开关打开时与 RequestChangeBox 同路（就地应用）；
	//关掉时逐字等于接入前 —— 这些调用点本来就在服务器上，接入前走的是
	//Server_ChangeBox_Implementation 本身（服务器就地应用 + 多播），不是客户端的 RPC
	if (CVarPredictionBox.GetValueOnGameThread() != 0)
	{
		ApplyChangeBoxLocal(FVector::ZeroVector, FVector::ZeroVector, Box);
		return;
	}

	Server_ChangeBox_Implementation(FVector::ZeroVector, FVector::ZeroVector, Box);
}

void AC_Character::ApplyChangeBoxLocal(FVector Size, FVector Offset, int32 Box)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS == nullptr)return;

	//保护状态受击框的写入整体丢弃（逐字保留接入前 Server_ChangeBox_Implementation 的守卫）：
	//保护期内受击框保持为无碰撞，要等下一次通知重新给尺寸
	if (Box == 0 && PS->CharacterState == ECharacterStateType::Protected)return;

	if (Box == 0)
	{
		NotifiedPlayerBoxSize = Size;
		NotifiedPlayerBoxOffset = Offset;
	}
	else
	{
		NotifiedAttackBoxSize = Size;
		NotifiedAttackBoxOffset = Offset;
	}

	//本地先行：这一帧就派生应用
	DeriveAndApplyBoxes();
}

void AC_Character::DeriveAndApplyBoxes()
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS == nullptr)return;

	//状态闸门（设计 5.9b 的"派生"）。两条判据与接入前逐条同源：
	//① 命中三态下攻击框无法启动（Mult_ChangeBoxSize 里的那一条）；
	//② 保护态下受击框保持为无碰撞（Server_ChangeBox_Implementation 的那一条）。
	//归零写进"通知几何"本身而不是只改组件：接入前那条路径把尺寸真的写成了 0，
	//状态退出后不会自己回来 —— 这里同样，要等下一次通知重新给尺寸。
	//（用 PS->CharacterState 而不是 GetInformation() 缓存下来的 MyCState：本函数也会被
	//「非 Tick 时刻」的写入点调用（技能输入、替身落点），那些时刻 MyCState 是上一帧的）
	if (PS->CharacterState == ECharacterStateType::Staggered
		|| PS->CharacterState == ECharacterStateType::Launched
		|| PS->CharacterState == ECharacterStateType::Grabbed)
	{
		NotifiedAttackBoxSize = FVector::ZeroVector;
		NotifiedAttackBoxOffset = FVector::ZeroVector;
	}
	if (PS->CharacterState == ECharacterStateType::Protected)
	{
		NotifiedPlayerBoxSize = FVector::ZeroVector;
		NotifiedPlayerBoxOffset = FVector::ZeroVector;
	}

	//受击框在前、攻击框在后（接入前每次只改一个框，这里的次序只是定个固定写法）
	ApplyBoxGeometry(PlayerBox, NotifiedPlayerBoxSize, NotifiedPlayerBoxOffset);
	ApplyBoxGeometry(AttackBox, NotifiedAttackBoxSize, NotifiedAttackBoxOffset);
}

void AC_Character::ApplyBoxGeometry(UBoxComponent* TargetBox, const FVector& Size, const FVector& Offset)
{
	if (TargetBox == nullptr)return;

	//翻转由朝向派生（设计 5.9b）：每帧按当前 Toward 重算，而不是只在通知那一刻算一次 ——
	//这是本片"客户端与服务器同一动画帧一致（容差 0）"的落点：两端那份 Toward 的到达时刻
	//可以不同、但值一致，逐帧重算就不会留下"通知跑在 Toward 到达之前"那一段错翻的窗口
	FVector TargetOffset = Offset;
	if (!Toward)TargetOffset.X = -TargetOffset.X;

	//碰撞启用由尺寸派生（与接入前 Mult_ChangeBoxSize 同一判据）
	const ECollisionEnabled::Type TargetCollision = Size.IsNearlyZero()
		? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly;

	//只在真的变了才写组件：本函数每帧都跑两次，无脑写会把渲染状态一路标脏
	const bool bSizeChanged = TargetBox->GetUnscaledBoxExtent() != Size;
	const bool bOffsetChanged = TargetBox->GetRelativeLocation() != TargetOffset;
	const bool bCollisionChanged = TargetBox->GetCollisionEnabled() != TargetCollision;

	if (bSizeChanged)TargetBox->SetBoxExtent(Size);
	if (bOffsetChanged)TargetBox->SetRelativeLocation(TargetOffset);
	if (bCollisionChanged)TargetBox->SetCollisionEnabled(TargetCollision);

	if (!bSizeChanged && !bOffsetChanged && !bCollisionChanged)return;

	//验收读数（设计 5.9b 的验收标准：客户端在任意动画帧打印 AttackBox 的
	//GetUnscaledBoxExtent() 与 GetRelativeLocation()，与服务器同一帧一致、容差 0）。
	//带上动画身份，同一个动画位次在两端直接可比：动画名 + 播放到第几帧 + 驱动派生的三个量
	const UPaperFlipbook* CurrentFlipbook = Flipbook ? Flipbook->GetFlipbook() : nullptr;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	UE_LOG(LogPrediction, Verbose, TEXT("BoxChange: %s side %s box %s anim %s frame %d state %d toward %d size %s offset %s collision %d"),
		*GetName(), HasAuthority() ? TEXT("server") : TEXT("client"),
		TargetBox == PlayerBox ? TEXT("Player") : TEXT("Attack"),
		CurrentFlipbook ? *CurrentFlipbook->GetName() : TEXT("none"),
		Flipbook ? Flipbook->GetPlaybackPositionInFrames() : -1,
		PS ? (int32)PS->CharacterState : -1, Toward ? 1 : 0,
		*Size.ToCompactString(), *TargetOffset.ToCompactString(), (int32)TargetCollision);
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

FVector AC_Character::ComputeMoveOffset(FVector Offset, FVector2D TargetToward) const
{
	//位移折算与场地钳制（计划 3.4 重做）：原 MakeMove 里那两段逐行搬来，一个字都没改 ——
	//两端的位移量靠"同一份算法 + 同源的输入量"保证一致（开发计划 3.4 重做"机制总览"那节）

	//根据意图改变方向
	FVector MyOffset = Offset;
	if (TargetToward.Y == 0.f)MyOffset.Y = 0;
	else if (TargetToward.Y < 0.f)MyOffset.Y = -MyOffset.Y;
	if (!Toward)MyOffset.X = -MyOffset.X;

	//位置限制
	const FVector MyLocation = GetActorLocation();
	if (MyLocation.X + MyOffset.X > MaxLocation.X)MyOffset.X = MaxLocation.X - MyLocation.X;
	if (MyLocation.X + MyOffset.X < MinLocation.X)MyOffset.X = MinLocation.X - MyLocation.X;
	if (MyLocation.Y + MyOffset.Y > MaxLocation.Y)MyOffset.Y = MaxLocation.Y - MyLocation.Y;
	if (MyLocation.Y + MyOffset.Y < MinLocation.Y)MyOffset.Y = MinLocation.Y - MyLocation.Y;

	return MyOffset;
}

void AC_Character::MakeMove(FVector Offset, FVector2D TargetToward)
{
	//权限语义（设计 5.8 / 2.9）：两端各自执行，本函数不做端别判定 —— 权威那一半由蓝图
	//I_MakeMove 的权威分支调用，拥有者客户端那一半走下面的 PredictMakeMove，
	//两处最后都落到 ApplyMoveRootMotion，是同一条路。
	//3.4 重做（2026-10-12）：位移不再裸写位置（旧实现在 git stash@{0}，回退理由见开发计划 3.4 重做）
	//——根因是位置写在移动组件之外：它不属于任何一次 ServerMove，引擎每段都发一次校正把它拽回原点。
	//诊断（计划 3.4 重做 W4.1）：非权威端调到这里 = 蓝图 I_MakeMove 的非权威分支还在调 MakeMove
	//（还没改成 PredictMakeMove）。这条日志出现即那一处没改；位移照常发生，观感可照常验收
	if (!HasAuthority())
	{
		UE_LOG(LogPrediction, Warning, TEXT("MakeMove was called on a non-authority end (%s): the non-authority branch of the blueprint interface I_MakeMove is still calling MakeMove instead of PredictMakeMove, so the Prediction.Move switch does not gate the local displacement"), *GetName());
	}

	ApplyMoveRootMotion(ComputeMoveOffset(Offset, TargetToward));
}

void AC_Character::PredictMakeMove(FVector Offset, FVector2D TargetToward)
{
	//AN_MakeMove 的客户端那一半（计划 3.4 重做 / 设计 2.9）。
	//位移不建键、不写标记：它走移动组件（根运动源），不是表内属性 —— 这是它与 3.1 / 3.2 / 3.3 三片
	//在结构上的唯一区别（那三片都是"本地先行写值 + 写标记"，本片只有"算同一份位移、交给移动组件"）。
	//四道门：本机控制（远端的模拟代理不预测，那一份看服务器复制的位置）+ 端别（侦听服务器的宿主
	//既是本机控制又是权威，不预测 —— 逐字同 3.3 的端别闸门）+ 上下文 + 两个开关。
	//Prediction.Move 关掉 = 客户端一个字都不做，位移只由服务器那一半施加（接入前的样子）
	if (!IsLocallyControlled())return;
	if (HasAuthority())return;
	if (PredictionComponent == nullptr)return;
	if (!PredictionComponent->CanPredict())return;
	if (CVarPredictionEnabled.GetValueOnGameThread() == 0)return;
	if (CVarPredictionMove.GetValueOnGameThread() == 0)return;

	ApplyMoveRootMotion(ComputeMoveOffset(Offset, TargetToward));
}

void AC_Character::ApplyMoveRootMotion(const FVector& MyOffset)
{
	//把折算好的偏移交给移动组件（计划 3.4 重做）。权威那一半与拥有者客户端那一半都从这里出去 ——
	//"两端同一条路"的落点：位移量与物理过程（带碰撞扫描的移动）因此一致，差别只有时刻（客户端早 RTT/2）
	if (MyOffset.IsNearlyZero())return;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (MoveComp == nullptr)return;

	//源的身份（裁决 甲，2026-10-12）：两端必须用【同一个】InstanceName —— 引擎认"同一个根运动源"就是比
	//InstanceName（FRootMotionSource::Matches，RootMotionSource.cpp:231-240：类型 + Priority + AccumulateMode
	//+ bInLocalSpace + InstanceName + Duration；两端的 Priority 都是基类默认的 0，Duration 是同一个常量）。
	//名字对不上时，更正支路（ClientAdjustRootMotionSourcePosition_Implementation，
	//CharacterMovementComponent.cpp:10888-10901：先按 LocalID 映射，再按 Matches 找配对）配不到本地那份源、
	//把它剔除（CullInvalidSources），然后把 SavedRootMotion 换成一个不含这份位移的组 —— 客户端重放时位移就没了，
	//只剩根运动留下的那个速度（ApplyMoveRootMotion 之前一版日志里那段 800 cm/s 的来源：
	//PerformMovement 的 AccumulateOverrideRootMotionVelocity 会把 Velocity 顶成根运动速度）把角色继续推着走，
	//直到摩擦磨掉或下一条校正把它按回 0 = 延迟下的"额外位移"。
	//身份取"两端都有的动作状态"：客户端预测写的 Attack / MySkill 与服务器权威写的是同一个值（3.1~3.3 的整套设计
	//就为这个）。两端万一不一致，引擎只是配不上对、行为退回改名前那一档，安全。
	//名字只用于日志与引擎配对
	const AC_PlayerState* MyPS = GetPlayerState<AC_PlayerState>();
	const int32 StateSkill = (MyPS != nullptr) ? MyPS->MySkill : -1;
	const int32 StateAttack = (MyPS != nullptr) ? MyPS->Attack : -1;
	++MoveSourceSerial;
	const FName SourceName = FName(*FString::Printf(TEXT("Move_%d_%d"), StateSkill, StateAttack));

	//源是朝目标收敛的（RootMotionSource.cpp:752-754 的解算：速度 = 剩余距离 / 本次移动 tick），
	//所以即使两端的当前坐标差着一点点（引擎容差之内），落点仍然相同
	TSharedPtr<FRootMotionSource_MoveToForce> Source = MakeShared<FRootMotionSource_MoveToForce>();
	Source->InstanceName = SourceName;
	Source->StartLocation = GetActorLocation();
	Source->TargetLocation = Source->StartLocation + MyOffset;
	Source->Duration = MoveRootMotionDuration;

	//收尾速度（不能省）：源生效期间累积模式是 Override（RootMotionSource.cpp:666 起的基类默认值），
	//移动组件的 Velocity 会被换成根运动速度（短时长下是个很大的数）。默认收尾模式是
	//MaintainLastRootMotionVelocity —— 把它【留着】，位移结束时角色会带着几千 cm/s 滑出去。
	//3.4 W4.1 第二次实机返工（开发者裁决②，2026-10-12）：这里写【两端共用的常量 0】，不再还回"施加之前"那个速度。
	//依据是第四次读数的实机证据：服务器侧 16 次施加的收尾速度 16/16 都是 V(0)（出招窗口里 Move() 已挡掉移动输入，
	//速度本就该是 0），而客户端那一份 17 次里有 9 次带着 45.56 ~ 1020.00 cm/s 的残余 —— 那是"在飞的根运动 /
	//重放 / 刚收到的校正"污染出来的值。拿它当收尾速度，位移结束之后角色会被这些残余速度继续推着走，
	//下一条 ServerMove 又把这多出来的一段算成误差 → 校正 → 又一个"额外位移"（第四次读数里那段 552 cm 的行程）。
	//写成常量而不是"施加前现采一个"，是为了让两端【逐位一致】：两端各自采样的那个值本来就可能不同（客户端早半个 RTT），
	//而这一处只要两端不同，就会变成一条永久的位置误差
	Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;

	MoveComp->ApplyRootMotionSource(Source);

	//诊断（计划 3.4 重做 W4.1 的"额外位移"排查）：把施加【这一刻】的动作状态一并记出来。
	//一段位移一条日志；同一对 (skill, attack) 在同一段动画里出现两条 = 通知被重跑了一次
	//（回滚之后动画重进同一状态、或蓝图非权威分支重复调用），是"额外位移"的第一手证据。
	//serial 是本次进程内第几次施加（两端各自从 1 开始，不参与配对），用来数一次按键被施加了几次
	UE_LOG(LogPrediction, Verbose, TEXT("MoveRootMotion: %s side %s source %s serial %d offset %s (%s -> %s) velocity kept %s state skill %d attack %d"),
		*GetName(), HasAuthority() ? TEXT("server") : TEXT("client"), *SourceName.ToString(), MoveSourceSerial,
		*MyOffset.ToCompactString(), *Source->StartLocation.ToCompactString(), *Source->TargetLocation.ToCompactString(),
		*Source->FinishVelocityParams.SetVelocity.ToCompactString(), StateSkill, StateAttack);
}

void AC_Character::MarkActionPredictedByClient()
{
	//3.4 W4.1 第二次实机返工（开发者裁决②，2026-10-12）：动作在服务器开起来时置这个闩锁 ——
	//服务器据此在这个动作窗口（Attack / MySkill 非 0）里关掉移动组件的客户端位置误差校正，见 Tick 的抑制块。
	//【为什么需要】拥有者客户端那份本地位移（PredictMakeMove）到不了服务器：5.3 的 ServerMove 载荷里
	//没有任何根运动字段（开发计划 3.4 重做的引擎证据表），所以两端各自施加自己那一份、时刻差半个 RTT。
	//这段时间差在引擎眼里就是"客户端走错了"，于是每段都发一次校正把它拽回服务器那一格 ——
	//第四次读数：4 秒里 225 次校正判定，客户端被拽向的落点带着 ± 几百厘米的摆动（-450 ~ +412）。
	//抑制的是【检查】：引擎开关 bIgnoreClientMovementErrorChecksAndCorrection 全工程只有一处读，
	//就是 ServerCheckClientError 的第一道闸（CharacterMovementComponent.cpp:9917）—— 服务器保留自己的模拟，
	//只是不再据此发校正。绝不能用孪生的 bServerAcceptClientAuthoritativePosition（那是"服务器采纳客户端位置"，
	//服务器自己那条通知会再位移一次 = 位移两倍）
	//开边条件（与已回退的旧实现有一处差别）：两个开关都开着才置。旧实现的条件是"这条请求带键吗"，
	//本版不能用它 —— 位移预测不挂键（裁决 61），"Prediction.Move 开 / 动作开关关"那一档下客户端照样本地位移，
	//而请求因为不建键而没有键，用键当条件会让那一档裸奔；而完全降级档（Enabled 或 Move 关着）必须【不置】：
	//那里客户端一点本地先行都没有（PredictMakeMove 的四道门），校正通道要原样保留 ——
	//Prediction.Enabled 0 时客户端那份位移的唯一来源就是这条校正通道（它自己不施加），抑制掉就等于"位移在
	//自己屏幕上不显示"。这两句就是 3.6 开关独立性里"客户端收到的校正与接入前逐位一致"的落点
	if (CVarPredictionEnabled.GetValueOnGameThread() == 0)return;
	if (CVarPredictionMove.GetValueOnGameThread() == 0)return;

	bActionPredictedByClient = true;

	//闩锁与移动组件那个开关必须在【同一次调用】里写（已定事项 55）：收到的一批网络包在同一次收包相位里
	//连续处理（NetDriver.cpp:1807 的 OnTickDispatch 相位），而 Tick 里那一遍写要等这个相位结束才轮得到 ——
	//当延迟大于"输入到本段位移帧"的间隔时（实机 200ms 即此档），请求与随后那批【已经带着本地位移】的
	//ServerMove 落进同一次收包相位，排在请求之后处理的那批 ServerMove 读到的开关还是旧值（假）
	//→ ServerCheckClientError 照跑 → 把本地位移拽回基线。低延迟下"没事"只是因为那时请求比这批 ServerMove
	//早得足够远，Tick 里那一遍写赶上了
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bIgnoreClientMovementErrorChecksAndCorrection = true;
	}

	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	UE_LOG(LogPrediction, Verbose, TEXT("MarkActionPredictedByClient: %s (attack %d skill %d)"),
		*GetName(), PS ? PS->Attack : -1, PS ? PS->MySkill : -1);
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
		//下一次普攻输入创建新键时先冻结旧键（设计 2.4.4 的"主路径"、2.3.2）：同一时刻只允许一个
		//活跃键，CreatePredictionKey 撞上活跃键会 ensure 并返回 0
		FreezeActivePredictionKey();
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
		//换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：被冻结的那个只冻结、不结算，
		//照旧由它自己的回执或超时结算
		FreezeActivePredictionKey();
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
	//技能输入时先关掉攻击框（碰撞框切片：改走统一入口 —— Prediction.Box 打开 = 就在这一端应用，
	//关掉 = 逐字等于接入前那条本地控制端的 Server_ChangeBox RPC）
	RequestChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
	if (FirstSkillCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);

		//预测接入点（设计 3.6.1 / 计划 3.0 固定动作 1）：本机控制（函数开头已判）+ 能预测 + 本片开关
		//【同形提醒】五个技能输入（FirstSkill / SecondSkill / FinalSkill / Scroll / Summon）的这一段逐字相同，
		//只差请求值、CD 字段与本地先行内容；改一处必须五处一起改（抽公共骨架是可选重构，见计划 5.4）
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
			//换技能 / 换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：同一时刻只允许
			//一个活跃键，冻结由"下一次预测"驱动；被冻结的那个照旧由它自己的回执或超时结算
			FreezeActivePredictionKey();
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
	//技能输入时先关掉攻击框（碰撞框切片：改走统一入口 —— Prediction.Box 打开 = 就在这一端应用，
	//关掉 = 逐字等于接入前那条本地控制端的 Server_ChangeBox RPC）
	RequestChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
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
			//换技能 / 换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：同一时刻只允许
			//一个活跃键，冻结由"下一次预测"驱动；被冻结的那个照旧由它自己的回执或超时结算
			FreezeActivePredictionKey();
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
	//技能输入时先关掉攻击框（碰撞框切片：改走统一入口 —— Prediction.Box 打开 = 就在这一端应用，
	//关掉 = 逐字等于接入前那条本地控制端的 Server_ChangeBox RPC）
	RequestChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
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
			//换技能 / 换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：同一时刻只允许
			//一个活跃键，冻结由"下一次预测"驱动；被冻结的那个照旧由它自己的回执或超时结算
			FreezeActivePredictionKey();
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
	//技能输入时先关掉攻击框（碰撞框切片：改走统一入口 —— Prediction.Box 打开 = 就在这一端应用，
	//关掉 = 逐字等于接入前那条本地控制端的 Server_ChangeBox RPC）
	RequestChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
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
			//换技能 / 换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：同一时刻只允许
			//一个活跃键，冻结由"下一次预测"驱动；被冻结的那个照旧由它自己的回执或超时结算
			FreezeActivePredictionKey();
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
	//技能输入时先关掉攻击框（碰撞框切片：改走统一入口 —— Prediction.Box 打开 = 就在这一端应用，
	//关掉 = 逐字等于接入前那条本地控制端的 Server_ChangeBox RPC）
	RequestChangeBox(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f), 1);
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
			//换技能 / 换切片时先冻结旧键（设计 2.3.2 / 计划 3.6 的"键不共享"）：同一时刻只允许
			//一个活跃键，冻结由"下一次预测"驱动；被冻结的那个照旧由它自己的回执或超时结算
			FreezeActivePredictionKey();
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
	//（碰撞框切片：改走统一入口。Prediction.Box 打开 = 就地应用，随后这一行把状态改成 Protected、
	//派生闸门接着保持它为零；关掉 = 逐字等于接入前那次直调 Server_ChangeBox_Implementation）
	ClearBox(0);
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

		//位移预测的校正抑制开边（计划 3.4 W4.1 / 已定事项 53、55、59）：动作窗口在这里被打开（Attack 顶上去），
		//闩锁与移动组件那个引擎开关就地一起写（为什么不放在函数开头、为什么必须就地写，见那处的注释）。
		//放在这一支里而不是更早：被吞掉的那一种输入（锁为真、预输入窗口也关着）走不到这里，
		//它没有对应的本地先行，也不该抑制；技能那一支在 AC_PlayerController::Server_ChangeSkillState_Implementation 的同一处
		MarkActionPredictedByClient();
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

void AC_Character::SyncRemoteTowardFromFlipbook()
{
	//远端模拟代理的朝向补值：客户端上，非本机控制的角色那份 Toward 没有任何下发通道 ——
	//设计 5.2b 把 Toward 从逐属性复制挪进了权威值表，而表里 Character 段只有 Self.Char.*
	//（Enemy.Char.* 不存在、敌方角色那份权威值表组件也没有被绑定）=> 那份 Toward 恒为类默认 true，
	//而框的翻转（ApplyBoxGeometry / 降级路 Mult_ChangeBoxSize）与蓝图侧客户端本地生成的特效都读它，
	//表现就是"服务器玩家的框和特效永远正朝向"。
	//那份权威朝向其实一直跟着 Flipbook 的翻转在复制（Server_ChangeToward 设相对旋转、
	//Flipbook 在构造函数里 SetIsReplicated(true)），这里由它反推补齐 —— 纯本地派生，不是预测。
	//本机控制的角色不走这里：它的 Toward 由权威值表那条 Self.Char.Toward 写（见设计 5.2b）
	if (HasAuthority() || IsLocallyControlled() || Flipbook == nullptr)return;

	const FRotator FlipRotation = Flipbook->GetRelativeRotation();
	//判据不能用 Pitch。翻转写的是 (180,0,-90) 那个 Pitch，但它经四元数绕了一圈回来：
	//SetRelativeRotation 走 SetRelativeLocationAndRotation -> MoveComponentImpl ->
	//RelativeRotationCache.QuatToRotator_ReadOnly，回读到的 RelativeRotation 已经规范化成
	//|Pitch| <= 90 的等价形式（FQuat::Rotator 的 Pitch 就是 asin，值域只有 [-90,90]），
	//那个 180 再也看不回来 —— 写成 |Pitch| < 90 的话判据对任何旋转都为真、恒等于"朝向 = true"。
	//改用【组件局部 X 轴】：未翻转的 (0,0,-90) 把 X 轴放在 (1,0,0)，含 180 度俯仰的翻转把它
	//放到恰好相反的一侧。轴是"这个旋转本身"的不变量，与回读成哪个等价形式无关，判据才稳
	const FVector FlipAxisX = FlipRotation.Vector();
	const bool bDerivedToward = FlipAxisX.X > 0.f;
	if (bDerivedToward == Toward)return;

	Toward = bDerivedToward;
	UE_LOG(LogPrediction, Verbose, TEXT("TowardSync: %s side client toward %d (flip rot %.1f/%.1f/%.1f axis %.2f/%.2f/%.2f)"),
		*GetName(), Toward ? 1 : 0, FlipRotation.Pitch, FlipRotation.Yaw, FlipRotation.Roll,
		FlipAxisX.X, FlipAxisX.Y, FlipAxisX.Z);
}

void AC_Character::FreezeActivePredictionKey()
{
	//冻结旧键（设计 2.3.2）：只冻结、不结算 —— 结算仍由它自己的回执或超时执行（设计 2.3.1），
	//所以换切片 / 换技能不会丢掉上一个键的待结算状态，它带着自己的标记进未结算缓冲池。
	//组件与键都在时才动：没有活跃键时本函数什么都不做（这就是"没有下一次预测"的常态）
	if (PredictionComponent == nullptr)return;
	const uint32 OldKeyID = PredictionComponent->GetActivePredictionKeyID();
	if (OldKeyID != 0)
	{
		PredictionComponent->EndPredictionKey(OldKeyID);
	}
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

	//碰撞框派生（计划 3.5 / 设计 5.9b）：两端每帧按"通知写入的几何 + PS->CharacterState + Toward"
	//重算两个框的尺寸 / 位置 / 碰撞启用。这里是"任意动画帧两端一致"的来源 —— 两端各自跑同一套
	//派生，服务器那一份也不再由客户端的通知经多播驱动。
	//位置必须在 GetInformation() 之后、空指针早退之前：本函数自己判 PS 的空，
	//不跟在 PS / GameState 早退后面（框的派生与 GameState 无关）。
	//开关关掉时整段不做：那时几何的唯一下发路径是老路（RPC + 多播），派生会把它拽回来
	//在此之前先把远端模拟代理那份 Toward 补齐（本地派生）：框的翻转与客户端本地生成的特效都读它
	SyncRemoteTowardFromFlipbook();
	if (CVarPredictionBox.GetValueOnGameThread() != 0)
	{
		DeriveAndApplyBoxes();
	}

	//保护（无敌）表现：同样按状态每帧派生，不再依赖服务器那一次多播（理由见函数实现处的注释）
	DeriveAndApplyProtectedVisual();

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

		//超时兜底（3.1 起常态有键；Prediction.Draw 的绘制也挂在这次调用的开头）
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

		//位移预测的校正抑制 —— 合边与兜底（计划 3.4 W4.1 / 已定事项 53、55、59）。
		//窗口 = 动作窗口（Attack / MySkill 非 0），正是 Move() 那条移动拒绝判据的第一行：这段里角色位置
		//只有两端的同源写点（位移通知、场地夹取、替身落点），移动输入又已被 Move() 挡掉 ——
		//服务器没有"客户端只能靠校正才知道"的位移，抑制没有损失。
		//受击三态（Staggered / Launched / Grabbed）不在此窗口：那里的平推（C_PlayerController.cpp:123）
		//与击飞（:113 + 本函数下面的浮空循环）是服务器独有的位移，客户端唯一的来源就是这条校正通道，
		//一并抑制会让被击飞 / 被平推的一方在自己屏幕上不动 —— 所以判据收窄到"自己在出招"。
		//收窄自带解除：被打断时 PlayerStateReset 先清零 Attack / MySkill，窗口即合，那一下照常到达。
		//开边在 MarkActionPredictedByClient（带键 / 非带键请求落地的就地写），这里只做两件事：
		//窗口关闭时清闩锁、以及把闩锁与 Prediction.Move 的当前值同步到引擎开关上（运行中被关开关也走这里）
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			if (PS->Attack == 0 && PS->MySkill == 0)bActionPredictedByClient = false;

			const bool bSuppressMoveCorrection = bActionPredictedByClient
				&& (CVarPredictionMove.GetValueOnGameThread() != 0);
			if (MoveComp->bIgnoreClientMovementErrorChecksAndCorrection != bSuppressMoveCorrection)
			{
				MoveComp->bIgnoreClientMovementErrorChecksAndCorrection = bSuppressMoveCorrection;
				UE_LOG(LogPrediction, Verbose, TEXT("Move correction suppression: %s -> %d (attack %d skill %d)"),
					*GetName(), bSuppressMoveCorrection ? 1 : 0, PS->Attack, PS->MySkill);
			}
		}

		if (PS->CharacterState == ECharacterStateType::Protected) {
			//表现不再在这里发（原为进保护发一次 true、计时器到期发一次 false）：
			//那一对多播的"退出"只有一个出口，而保护期间允许出招 —— 收招会把状态直接写成 Normal
			//（ChangeAttack(0)），多播发不出去，闩锁留在 true 就是"一直半透明"。
			//改由两端各自在 Tick 里按 PS->CharacterState 派生（DeriveAndApplyProtectedVisual），
			//本分支只剩计时器的状态出口这一个职责

			//结束保护
			if ((GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - ProtectedStartTime) > ProtectedTime) {
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
				//（碰撞框切片：同上，改走统一入口）
				ClearBox(0);
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
	//本多播已不是保护表现的驱动源（改由 DeriveAndApplyProtectedVisual 按 PS->CharacterState 每帧派生）。
	//保留它只为一件事：外部若仍有调用（如蓝图侧），表现照旧生效，不留空实现
	if (!Flipbook)return;
	if(show)Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f));
	else Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	//同步"上一次写出去的半透明值"这道门：否则派生那边以为自己写的还是旧值，会少写一次
	bIsProtectedShow = show;
}

void AC_Character::DeriveAndApplyProtectedVisual()
{
	//保护（无敌）半透明按 PS->CharacterState 每帧派生，两端各跑一遍（状态两端一致：
	//服务器是权威、客户端那份经权威值表的 Enemy.PS.* / Self.PS.* 到达）。
	//为什么不用"进保护发 true、计时器到期发 false"那一对多播：退出只有一个出口，
	//而保护期间允许出招（Attack 里的 Protected 分支）—— 收招通知会把状态直接写成 Normal
	//（ChangeAttack(0)），多播发不出去，半透明就再也回不来。派生的状态下没有会漏的状态位
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS == nullptr || Flipbook == nullptr)return;

	const bool bShow = (PS->CharacterState == ECharacterStateType::Protected);
	if (bShow == bIsProtectedShow)return;

	bIsProtectedShow = bShow;
	if (bShow)Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f));
	else Flipbook->SetSpriteColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));

	UE_LOG(LogPrediction, Verbose, TEXT("ProtectedVisual: %s side %s show %d state %d"),
		*GetName(), HasAuthority() ? TEXT("server") : TEXT("client"), bShow ? 1 : 0, (int32)PS->CharacterState);
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

