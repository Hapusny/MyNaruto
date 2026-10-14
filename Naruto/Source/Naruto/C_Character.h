// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "C_Character.generated.h"

class UInputMappingContext;
class UInputAction;
class UPaperFlipbookComponent;
class UPaperZDAnimationComponent;
class UPaperSpriteComponent;
class AC_PlayerState;
class UBoxComponent;
class AC_GrabPoinnt;
class UC_PredictionComponent;
class UC_AuthorityValueComponent;
enum class ETeamType : uint8;
struct FInputActionValue;
enum class ECharacterStateType : uint8;

//攻击类型
UENUM(BlueprintType)
enum class EAttackType : uint8
{
	Push	UMETA(DisplayName = "Push"),//平推
	Launch  UMETA(DisplayName = "Launch"),//击飞
	Grab    UMETA(DisplayName = "Grab")//抓取
};

UCLASS()
class NARUTO_API AC_Character : public ACharacter
{
	GENERATED_BODY()

	//预测系统：预测组件要读写本类的私有时间戳与朝向，权威值表组件要在 PreReplication 里读它们填表
	//（计划 1.3 / 设计 5.3，纯声明、零行为；这是接入预测系统唯一的现有文件改动）
	friend class UC_PredictionComponent;
	friend class UC_AuthorityValueComponent;

	//服务器校验（计划 2.3 / 设计 5.1）：技能请求被服务器接受时，由 PlayerController 写下权威 CD 时间戳。
	//四个时间戳仍留在本类（设计 5.3：不加 UPROPERTY、不加 Replicated），服务器只写自己那一份，
	//2.4 起由权威值表下发给客户端 —— 服务器不写的话，服务器自己的 CD 判据永远是"没在冷却"
	//锁更正（计划 2.5 / 设计 2.8）：PlayerStateReset 写完 bAttackInputLock 之后也要走
	//SendLockCorrection 把权威值发给客户端，那是 private
	friend class AC_PlayerController;

public:

	AC_Character();

	//等待PS网络同步后初始化
	virtual void OnRep_PlayerState() override;

	//预测系统：初始化预测上下文（设计 3.4.1 / 计划 2.1）。
	//客户端上 PlayerState / Controller 都在 BeginPlay 之后才到达，首次调用多半失败，由 Tick 重试
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTeamChanged();

	void MyInitialize(ETeamType team);

	// 攻击框组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UBoxComponent> AttackBox;

	// 受击框组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UBoxComponent> PlayerBox;

	// 位置标记组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UPaperSpriteComponent> PlaceMark;

	//角色动画组件
	UPROPERTY(EditAnywhere)
	TObjectPtr<UPaperFlipbookComponent> Flipbook;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPaperZDAnimationComponent> PaperZD;

	//预测组件（设计 3.2 / 3.1：挂在角色上，服务器与客户端各挂一个，随角色生成销毁）。
	//预测逻辑只在客户端生效；服务器上它不参与任何逻辑（CanPredict 在服务器上同样为真，
	//服务器不先行靠的是"服务器侧没有任何建键调用"，设计 3.2）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UC_PredictionComponent> PredictionComponent;

	//权威值表组件（设计 2.7.5 / 计划 2.4）：承载 Character 段的被预测属性（Toward / LastEscapeTime /
	//四个 CD 时间戳）。服务器在 PreReplication 里从本类成员刷新，客户端收到表后按采用规则写回。
	//构造函数里创建（C_Character.cpp），随角色一起复制
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UC_AuthorityValueComponent> AuthorityValueComponent;


	//角色受到伤害
	UFUNCTION(BlueprintCallable)

	void BeDameged(float Damage, ECharacterStateType State,EAttackType Type, FVector Effect, float Time, AC_GrabPoinnt* GrabPoint);


	//设置对手停帧状态
	UFUNCTION(BlueprintCallable)
	void SetOtherPauseState(bool state);

	//更改碰撞体
	UFUNCTION(Server,Reliable,BlueprintCallable)
	void Server_ChangeBox(FVector Size, FVector Offset, int32 Box);

	//碰撞框变更的【统一入口】（计划 3.5 / 设计 5.9b 的路线 A：两端各自派生）。
	//全工程每一处框的写入都从这里走：蓝图 I_ChangeBox（AN_ChangeAttackBox / AN_ChangePlayerBox
	//通知的函数体）与 C++ 的九个调用点。Prediction.Box 打开时 = 就地应用自己那一份几何 ——
	//通知在哪一端跑，哪一端当场改自己的框，拥有者不再等一个 RTT 的多播（接入前它的几何必须
	//等"自己发的 RPC 到服务器、服务器再 Mult_ChangeBoxSize 播回来"）；关掉时逐字等于接入前：
	//只有本地控制端执行，经 Server_ChangeBox 交给服务器、再由 Mult_ChangeBoxSize 播给所有人
	UFUNCTION(BlueprintCallable)
	void RequestChangeBox(FVector Size, FVector Offset, int32 Box);

	//把某个框关掉（尺寸归零 = 无碰撞）。四处服务器侧调用点共用：受击打断（PlayerStateReset）、
	//替身落点、起身保护、技能输入的服务器半边。开关语义与 RequestChangeBox 同一套，差别只在
	//"接入前的老路"那一支的形状：这些调用点本来就在服务器上，接入前走的是
	//Server_ChangeBox_Implementation 本身（服务器就地应用 + 多播），不是客户端的 RPC
	//（纯 C++ 入口：调用方都在服务器侧，蓝图侧没有调用点）
	void ClearBox(int32 Box);

protected:
	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeBoxSize(FVector Size, FVector Offset, int32 Box);



public:
	//角色朝向
	//预测系统（设计 5.2b / 计划 2.4）：不再走逐属性复制（DOREPLIFETIME 已删），改由
	//AuthorityValueComponent.AuthorityValueTable 下发；UPROPERTY 与蓝图读写照旧
	UPROPERTY(BlueprintReadWrite)
	bool Toward = true;

	//角色查克拉增加
	UFUNCTION(BlueprintCallable)
	void AddChakra();

	//抓取点绑定
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	TObjectPtr<AC_GrabPoinnt> MyGrabPoint;

private:
	//被抓点绑定
	TObjectPtr<AC_GrabPoinnt> BeGrabbedPoint;

public:

	//输入操作绑定
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* AttackAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* EscapeAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* FirstSkillAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* SecondSkillAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* FinalSkillAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* ScrollAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* SummonAction;

	UFUNCTION(BlueprintImplementableEvent)
	void BP_FirstSkillEffect();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_SecondSkillEffect();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_FinalSkillEffect();

	UFUNCTION(BlueprintImplementableEvent)
	void BP_SummonEffect();

	UFUNCTION(Server,Reliable)
	void Server_SetSummonIndex(int32 target);

	UPROPERTY(BlueprintReadWrite)

	int32 SummonIndex = 0;

	//复制角色朝向
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//角色普攻变换
	UFUNCTION(BlueprintCallable)
	void ChangeAttack(int32 attack);

	//角色状态变换
	UFUNCTION(BlueprintCallable)
	void ChangeState(ECharacterStateType target);

	//角色状态变换——【拥有者客户端那一半】（计划 3.1 / 2.7 判定表 / 设计 2.4.1、5.9a）。
	//AN_ChangeState 的客户端那一半走这里，不走 ChangeState（那一条是权威写者，本地值只留一个写者）：
	//CharacterState 整条按属性预测接入（已定事项 22，与 Chakra 同路）—— 不建键、不绑生命周期，
	//只做"本地先行写值 + 有活跃键就写标记"，之后随权威值表采用 / 回滚。
	//自带两道门：IsLocallyControlled()（远端的模拟代理不写）与本片开关（关掉 = 与接入前一致：客户端不写）
	UFUNCTION(BlueprintCallable)
	void PredictChangeState(ECharacterStateType target);

	//角色普攻段变换——【拥有者客户端那一半】（计划 3.3 / 2.7 判定表 / 设计 2.4.4）。
	//AN_ChangeAttack 的客户端那一半走这里，不走 ChangeAttack（那一条是权威写者）：
	//推进（attack != 0）什么都不写 —— 段号已在普攻输入点写过（2.4.4 的写入点前移，推进点只切表现）；
	//收招（attack == 0）是本地先行写 0 + 有活跃键就写标记，挡住"服务器还停在最后一段"的表。
	//自带两道门：IsLocallyControlled()（远端的模拟代理不写、那一份等表）与本片开关
	UFUNCTION(BlueprintCallable)
	void PredictChangeAttack(int32 attack);

	//角色位移——【权威那一半】（计划 3.4 重做 / 设计 2.9：两端各自执行）。
	//3.4 重做起，位移不再裸写位置：折算 + 场地钳制之后，交给移动组件一份根运动源
	//（ComputeMoveOffset + ApplyMoveRootMotion）。两端的位移量同源、物理过程同一条路
	UFUNCTION(BlueprintCallable)
	void MakeMove(FVector Offset,FVector2D TargetToward);

	//角色位移——【拥有者客户端那一半】（计划 3.4 重做 / 设计 2.9）。
	//AN_MakeMove 这条通知两端都有：权威那一半（上面 MakeMove）是"对手屏幕上唯一能看到位移的地方"
	//—— 客户端施加的根运动源【到不了服务器】（5.3 的 ServerMove 载荷里没有任何根运动字段，
	//见开发计划 3.4 重做的引擎证据表），所以服务器必须自己施加它那一份；
	//拥有者客户端这一半是本地先行：同一个 ComputeMoveOffset、同一种源，差别只有时刻（客户端早 RTT/2）。
	//自带四道门：IsLocallyControlled() / HasAuthority() 早退（侦听服务器的宿主不预测，逐字同 3.3）、
	//组件与 CanPredict()、以及 Prediction.Enabled / Prediction.Move 两个开关
	UFUNCTION(BlueprintCallable)
	void PredictMakeMove(FVector Offset, FVector2D TargetToward);

	//位移预测的【校正抑制】闩锁开启点（计划 3.4 W4.1 第二次实机返工 / 已定事项 53、55、59）。
	//由【服务器】在动作被请求开起来时调用：普攻那一支在 AC_Character::Server_Attack_Implementation 的开段分支，
	//技能那一支在 AC_PlayerController::Server_ChangeSkillState_Implementation 写 MySkill 的同一处。
	//本函数置闩锁，并【就地】把移动组件的 bIgnoreClientMovementErrorChecksAndCorrection 置真 ——
	//收到的一批包在同一次收包相位里连续处理，Tick 里那一遍写插不进那个相位（见实现处的注释）。
	//合边只有一处：Tick 的服务器块（动作窗口关闭 Attack == 0 && MySkill == 0 时清闩锁并同步引擎开关）
	void MarkActionPredictedByClient();

	//打开预输入窗口（AN_PreInput，计划 2.2 / 已定事项 24）
	//置假之后，窗口内的一次普攻输入由 Server_Attack_Implementation 置回真，
	//推进点的 AN_ChangeAttack -> ChangeAttack 消费它决定是否连段变换；同时清掉残留的移动意图。
	//本函数不做权限判定：门由调用方把（BP_Character 的 I_StartPreInput 权威分支），
	//TryTargetToward 那一半不能落到客户端上（客户端那份是本地输入意图，见 Move()）
	UFUNCTION(BlueprintCallable)
	void StartPreInput();

	//复位命中标志（计划 2.5 / 设计 5.6）：bSuccessHit 是纯服务器侧的值，置位与复位都归服务器。
	//蓝图里的读取点是 BP_Menma 的 I_HitJump（只读不写），复位点是 BP_Character 的 I_StartHitCheck
	//（权威分支里那条 Set SuccsessHit = false，即下一次攻击的命中检测开始时）——本函数就是那条复位的
	//下发形态，由蓝图改调它；客户端那一份只经 Client_CorrectLocks 到达，所以复位后随即下发。
	//函数体自带权限判定（复位归服务器）：客户端调用直接忽略
	UFUNCTION(BlueprintCallable)
	void ResetSuccessHit();

	//输入控制变量
	UPROPERTY(BlueprintReadWrite)
	bool bAttackInputLock = false;//普攻输入锁

	UPROPERTY(BlueprintReadWrite)
	bool bPreInputLock = true;//预输入

	UPROPERTY(BlueprintReadWrite)
	FVector2D TryTargetToward = FVector2D(0.f,0.f);//移动意图

	//设置移动意图
	UFUNCTION(Server,Reliable)
	void Server_SetTryTargetToward(FVector2D TargetToward);

	//命中判断
	UPROPERTY(BlueprintReadWrite)
	bool bSuccessHit = false;

	//位移预测的校正抑制闩锁（计划 3.4 W4.1 第二次实机返工 / 已定事项 53、55、59）：
	//纯服务器侧的量，不是复制量、不进权威值表 —— 它只决定"服务器这一端要不要发位置校正"。
	//置/清都在服务器：置在 MarkActionPredictedByClient（动作被请求开起来那一次），清在 Tick 的服务器块。
	//客户端不读它，客户端那一侧唯一相关的是"Prediction.Move 关掉时本地位移整个不做"（PredictMakeMove 的门）
	bool bActionPredictedByClient = false;

	//---- 锁更正通道（设计 2.8 / 5.5，计划 2.5）----
	//三个锁都不加 Replicated、不进权威值表、不参与结算（设计 2.6.2）：服务器在每一处写锁之后，
	//用这条 Client RPC 把权威值下发给拥有者客户端，客户端收到后直接覆盖本地那一份 —— 不记录、不结算。
	//被拒绝的输入请求也靠它解除客户端已经本地置位的锁（设计 2.6.1 的核心）。
	//位域（设计 2.8）：bit0 = bAttackInputLock、bit1 = bPreInputLock、bit2 = bSuccessHit；
	//Mask 表示本次更正声明了哪几个锁，未置位的锁不动客户端那一份
	UFUNCTION(Client, Reliable)
	void Client_CorrectLocks(uint8 LockMask, uint8 LockValues);


	//攻击数值
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float DamageValue = 0.f;//伤害值

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ECharacterStateType DamageState;//伤害状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackType DamageType = EAttackType::Push;//伤害类型

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector DamageEffect = FVector(0,0,0);//伤害影响位移

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EffectTime = 0.f;//伤害影响时间


	//替身数值
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeRange = 300.f;//替身范围

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeCD = 15.f;//替身CD
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeCDState = 0.f;//替身CD状态

	//技能数值
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FirstSkillCD = 10.f;//一技能CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FirstSkillCDState = 0.f;//一技能CD状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SecondSkillCD = 12.f;//二技能CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SecondSkillCDState = 0.f;//二技能CD状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ScrollCD = 40.f;//秘卷CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ScrollCDState = 0.f;//秘卷CD状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SummonCD = 200.f;//通灵CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SummonCDState = 0.f;//通灵CD状态

	//移动范围限制
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector MaxLocation = FVector(800.f,280.f,0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector MinLocation = FVector(-800.f, 80.f, 0.f);

	//保护状态持续时间
	UPROPERTY(EditAnywhere)
	float EscapeProtectedTime = 1.5f;//替身无敌时间

	UPROPERTY(EditAnywhere)
	float StandProtectedTime = 1.0f;//起身无敌时间

private:
	float ProtectedTime = 0.f;//保护时间


protected:
	//输入绑定函数
	void Move(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable)
	void Attack(const FInputActionValue& Value);

	void Escape(const FInputActionValue& Value);

	void FirstSkill(const FInputActionValue& Value);

	void SecondSkill(const FInputActionValue& Value);

	void FinalSkill(const FInputActionValue& Value);

	void Scroll(const FInputActionValue& Value);

	void Summon(const FInputActionValue& Value);


	//普攻请求（计划 3.3 / 设计 3.4.2）：键参数按值传（同 Server_Escape 的约定，UHT 生成的代码需要）。
	//KeyID == 0 = 非预测请求：服务器照常校验执行、不回执（设计 3.4.2 末行）
	UFUNCTION(Server, Reliable)
	void Server_Attack(FPredictionKey PredictionKey);

	//替身请求（计划 3.2 / 设计 3.4.2）：键参数按值传（同 Server_ChangeSkillState 的约定，UHT 生成的代码需要）。
	//KeyID == 0 = 非预测请求：服务器照常校验执行、不回执（设计 3.4.2 末行）
	UFUNCTION(Server, Reliable)
	void Server_Escape(FPredictionKey PredictionKey);

	//替身落点（计划 3.2 / 设计 5.4）：服务器与拥有者客户端各算一次，两处共用本函数 —— 落点判据必须同源
	//（设计 2.4.4）。语义与 Server_Escape_Implementation 原实现逐行一致：从自身位置出发，对手在
	//EscapeRange 内时取其位置，Z 归零；只多两处判空（设计 5.8：原实现对 Cast 与 GetPawn 都没有判空）
	FVector ComputeEscapeTargetPlace() const;

	//位移折算与场地钳制（计划 3.4 重做）：MakeMove 与 PredictMakeMove 共用。
	//把原实现在 MakeMove 里的那两段（按移动意图与朝向折算、按 MaxLocation / MinLocation 钳制）
	//一字不改地搬进来 —— 两端的位移量必须逐字同源，所以这条函数就是"两端同一份算法"。
	//它读的三个量在两端各自都有一份：Offset 来自动画通知（同一份资产）、TargetToward 由
	//AN_LockTargetToward 锁定（客户端那一半见开发计划 3.4 重做的蓝图待办）、Toward 是复制量
	FVector ComputeMoveOffset(FVector Offset, FVector2D TargetToward) const;

	//把【已经折算并钳制好】的偏移变成一份根运动源交给移动组件（两端同一条路）。
	//位移因此进了移动系统，三处好处：带碰撞扫描（撞墙不再穿过去）、进 saved move，
	//以及服务器校正之后的回放会把它恢复回来（FSavedMove_Character::PrepMoveFor 里
	//CurrentRootMotion = SavedRootMotion）—— 旧实现裸写 AddActorLocalOffset 不属于任何 move，
	//校正一覆盖就永久没了，那正是"先位移、随即被拽回原点"的机制
	void ApplyMoveRootMotion(const FVector& MyOffset);

	//改变角色朝向
	UFUNCTION(Server,Reliable,BlueprintCallable)
	void Server_ChangeToward(bool TargetToward);

	
	//攻击碰撞检测
	UFUNCTION()
	void OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	

public:	
	//每帧获取角色信息并处理状态
	virtual void Tick(float DeltaTime) override;

	//同步改变角色保护动画
	//（保护表现的驱动源已改为 Tick 里的 DeriveAndApplyProtectedVisual：按 PS->CharacterState
	//每帧派生。本多播保留、不再是驱动源，理由见实现处的注释；旧的闩锁 bInProtectAnim 随驱动源
	//的更换一并删除 —— 它只有"计时器到期"一条出口会清，正是半透明卡住的根因）
	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeProtectedAnim(bool show);

	//同步改变角色被抓取位置
	UFUNCTION(NetMulticast,Reliable)
	void Mult_ChangeGrabLocation(FVector target);

	//击飞状态
	int32 LaunchState = 0;

	//同步改变角色重力
	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeGravity(bool able);

	//获取角色信息
	UPROPERTY(BlueprintReadOnly)
	double MySpeed = 0.f;
	UPROPERTY(BlueprintReadOnly)
	int32 MyAttack = 0;
	UPROPERTY(BlueprintReadOnly)
	ECharacterStateType MyCState;
	UPROPERTY(BlueprintReadOnly)
	int32 MySkill = 0;

private:
	void GetInformation();

public:
	//输入绑定
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:

	//时间戳
	//预测系统（设计 5.2b / 5.3 / 计划 2.4）：LastEscapeTime 不再走逐属性复制，改由
	//AuthorityValueTable 下发；下面四个时间戳本来就是普通成员（连 UPROPERTY 都没有），
	//现在进表后也保持原样 —— 这正是权威值表相对逐属性复制省事的地方（设计 5.3）
	UPROPERTY()
	float LastEscapeTime = 0.f;//替身

	float LastFirstSkillTime = 0.f;//一技能

	float LastSecondSkillTime = 0.f;//二技能

	float LastScrollTime = 0.f;//秘卷

	float LastSummonTime = 0.f;//通灵

	float ProtectedStartTime = 0.f;//替身

	

	//保护状态动画显示：DeriveAndApplyProtectedVisual 里"上一次写出去的半透明值"，
	//用它做"只在真的变了才写"的那道门（多播实现里也同步它，见那里注释）
	bool bIsProtectedShow = false;


	//---- 预测键的建键入口（设计 2.3.2 / 计划 3.6 的"键不共享"）----
	//同一时刻只允许一个活跃（未冻结）预测键：CreatePredictionKey 撞上活跃键会 ensure 并返回 0
	//（设计 3.4.2），所以【冻结旧键的责任在调用方】。冻结由"下一次预测"驱动、不由轮询驱动（设计 2.3.2）——
	//七个建键点（普攻 / 替身 / 技能一 / 技能二 / 奥义 / 秘卷 / 通灵）在建键前一律先调本函数。
	//换切片也一样：技能进行中按普攻时，键从技能键换成普攻键；被冻结的那个只冻结、不结算，
	//照旧进未结算缓冲池，仍由它自己的回执或超时结算（设计 2.3.1）
	void FreezeActivePredictionKey();

	//---- 锁更正通道（设计 2.8 / 5.5，计划 2.5）----
	//三个锁的位域。掩码的语义是"本次更正声明了哪几个锁"：绝大多数调用点就是"服务器刚写过的那几位"，
	//唯一的例外是 Server_Attack 的拒绝路径 —— 它没写 bAttackInputLock，但必须声明 bit0，
	//好把客户端本地置位的那一份拉回服务器值（设计 2.6.1：拒绝的表现就是这条更正）
	//（Server_Attack 那三个出口的掩码见 LockBitsAttackRequest）
	static constexpr uint8 LockBitAttackInputLock = 0x01;//bit0：普攻输入锁
	static constexpr uint8 LockBitPreInputLock = 0x02;//bit1：预输入
	static constexpr uint8 LockBitSuccessHit = 0x04;//bit2：命中标志

	//Server_Attack 三个出口的掩码只声明 bit0（3.3 第二次实机返工的第 2 条修订）：bAttackInputLock 是
	//"这次请求的锁声明"——被接受时写了它，被拒绝时没写但它必须被声明（理由同上）。bPreInputLock 曾经
	//也在这两位里（函数开头无条件写过它），但那是一份【发送时刻的快照】：客户端那份 bPreInputLock 由
	//同源事件自己维护（输入点 Attack 置真、自己那条 AN_PreInput 置假），这份晚到的旧值只会把它改回真
	//—— 表现就是"预输入窗口之前按的键也让客户端在推进点自己进了下一段"。更改正只覆盖服务器独有的写点
	static constexpr uint8 LockBitsAttackRequest = LockBitAttackInputLock;

	//按 Mask 读出三个锁的当前值并下发（设计 2.8）。服务器的每一处写锁点调用；
	//Server_Attack_Implementation 末尾那一次覆盖接受与拒绝两条路径（设计 2.6.1）
	void SendLockCorrection(uint8 LockMask);

	//位移源的施加计数（计划 3.4 重做）。源的名字由 ApplyMoveRootMotion 起，两端的取值必须一致
	//（FRootMotionSource::Matches 比的就是名字，见那里的注释）；本计数只进日志，用来数"一次按键被
	//施加了几次"，两端各自从 1 开始、不参与引擎配对
	int32 MoveSourceSerial = 0;

	//---- 碰撞框切片（计划 3.5 / 设计 5.9b）----
	//"通知写入的几何"：每条通知（AN_ChangeAttackBox / AN_ChangePlayerBox）带着它那一刻的尺寸与偏移，
	//两端各自把自己收到的那一份存下来，Tick 再按 PS->CharacterState / Toward 派生（尺寸过状态闸门、
	//偏移按朝向翻转）写进组件。四个量都不是复制量、不进权威值表 —— 设计 5.9b 明说这三个量不参与
	//预测（框的尺寸 / 偏移 / 翻转不进权威值表、不建键、不写标记），两端一致靠的是"同一条通知
	//+ 同一套派生算法"，不是靠网络同步
	FVector NotifiedPlayerBoxSize = FVector::ZeroVector;
	FVector NotifiedPlayerBoxOffset = FVector::ZeroVector;
	FVector NotifiedAttackBoxSize = FVector::ZeroVector;
	FVector NotifiedAttackBoxOffset = FVector::ZeroVector;

	//写入"通知几何"并立即派生应用一次（本地先行：通知这一帧就生效）。
	//逐条保留接入前 Server_ChangeBox_Implementation + Mult_ChangeBoxSize_Implementation 的语义：
	//保护状态受击框的写入整体丢弃（该框保持为无碰撞）
	void ApplyChangeBoxLocal(FVector Size, FVector Offset, int32 Box);

	//按已复制状态派生两个框的最终几何并应用到组件（两端每帧、Tick 里调，本地写入点也顺手调一次）。
	//闸门与接入前同源：命中三态（Staggered / Launched / Grabbed）→ 攻击框归零；保护态 → 受击框归零。
	//归零写进的是"通知几何"本身（闩锁）—— 与接入前 PlayerStateReset 那次
	//Server_ChangeBox(0,0,0,1) 等效：状态退出后不会自己回来，要等下一次通知重新给尺寸
	void DeriveAndApplyBoxes();

	//把一份几何写进组件（尺寸 / 偏移 / 碰撞启用）。只在真的变了才写 —— 本函数每帧都被调用两次
	void ApplyBoxGeometry(UBoxComponent* TargetBox, const FVector& Size, const FVector& Offset);

	//---- 远端模拟代理的朝向补值（本次修复）----
	//客户端上，非本机控制的角色那份 Toward 没有任何下发通道：设计 5.2b 把 Toward 从逐属性复制
	//挪进了权威值表，而表里 Character 段只有 Self.Char.*（Enemy.Char.* 不存在、敌方角色的权威值表
	//组件也没有被绑定）⇒ 那份 Toward 恒为类默认 true，而框的翻转（ApplyBoxGeometry / 降级路
	//Mult_ChangeBoxSize）与蓝图侧客户端本地生成的特效读的都是它。
	//那份权威朝向其实一直跟着 Flipbook 的翻转在复制（Server_ChangeToward 设 Flipbook 的相对旋转、
	//Flipbook 在构造函数里 SetIsReplicated(true)），这里由它反推补齐 —— 纯本地派生，不是预测
	void SyncRemoteTowardFromFlipbook();

	//---- 保护（无敌）表现由状态派生 ----
	//半透明不再依赖服务器那一次多播，改为每帧按 PS->CharacterState 派生：多播是"进保护发 true、
	//计时器到期发 false"，而保护期间允许出招 —— 收招通知 ChangeAttack(0) 会把状态直接写成
	//Normal，闩锁留在 true，半透明再也回不来（旧实现的实际缺陷）。按状态派生后没有状态位可漏
	void DeriveAndApplyProtectedVisual();
};
