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

public:

	AC_Character();

	//等待PS网络同步后初始化
	virtual void OnRep_PlayerState() override;

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
	UPROPERTY(Replicated,BlueprintReadWrite)
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
	float EscapeRange = 100.f;//替身范围

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
	float SecondSkillCD = 10.f;//二技能CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SecondSkillCDState = 0.f;//二技能CD状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ScrollCD = 10.f;//秘卷CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ScrollCDState = 0.f;//秘卷CD状态

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SummonCD = 60.f;//通灵CD

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float SummonCDState = 0.f;//通灵CD状态

	//移动范围限制
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector MaxLocation = FVector(800.f,280.f,0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector MinLocation = FVector(-800.f, 80.f, 0.f);


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
	UPROPERTY(Replicated)
	float LastEscapeTime = 0.f;//替身

	float LastFirstSkillTime = 0.f;//一技能

	float LastSecondSkillTime = 0.f;//二技能

	float LastScrollTime = 0.f;//秘卷

	float LastSummonTime = 0.f;//通灵

	float ProtectedStartTime = 0.f;//替身

	

	//保护状态动画显示
	bool bIsProtectedShow = false;
};
