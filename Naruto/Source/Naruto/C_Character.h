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
	//预测逻辑只在客户端生效，服务器上它只用于读回执带回来的预测键
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

	//角色位移
	UFUNCTION(BlueprintCallable)
	void MakeMove(FVector Offset,FVector2D TargetToward);

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

	void Attack(const FInputActionValue& Value);

	void Escape(const FInputActionValue& Value);

	void FirstSkill(const FInputActionValue& Value);

	void SecondSkill(const FInputActionValue& Value);

	void FinalSkill(const FInputActionValue& Value);

	void Scroll(const FInputActionValue& Value);

	void Summon(const FInputActionValue& Value);


	UFUNCTION(Server, Reliable)
	void Server_Attack();

	UFUNCTION(Server, Reliable)
	void Server_Escape();

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
	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeProtectedAnim(bool show);

private:
	bool bInProtectAnim = false;

public:

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

	

	//保护状态动画显示
	bool bIsProtectedShow = false;


	//---- 锁更正通道（设计 2.8 / 5.5，计划 2.5）----
	//三个锁的位域。掩码的语义是"本次更正声明了哪几个锁"：绝大多数调用点就是"服务器刚写过的那几位"，
	//唯一的例外是 Server_Attack 的拒绝路径 —— 它没写 bAttackInputLock，但必须声明 bit0，
	//好把客户端本地置位的那一份拉回服务器值（设计 2.6.1：拒绝的表现就是这条更正）
	//（Server_Attack 那三个出口的掩码见 LockBitsAttackRequest）
	static constexpr uint8 LockBitAttackInputLock = 0x01;//bit0：普攻输入锁
	static constexpr uint8 LockBitPreInputLock = 0x02;//bit1：预输入
	static constexpr uint8 LockBitSuccessHit = 0x04;//bit2：命中标志

	//Server_Attack 的三个出口统一发这两位：bPreInputLock 在函数开头无条件写过；bAttackInputLock 是
	//"这次请求的锁声明"——被接受时写了它，被拒绝时没写但它必须被声明（理由同上）
	static constexpr uint8 LockBitsAttackRequest = LockBitAttackInputLock | LockBitPreInputLock;

	//按 Mask 读出三个锁的当前值并下发（设计 2.8）。服务器的每一处写锁点调用；
	//Server_Attack_Implementation 末尾那一次覆盖接受与拒绝两条路径（设计 2.6.1）
	void SendLockCorrection(uint8 LockMask);
};
