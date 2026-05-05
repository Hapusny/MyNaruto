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
enum class ETeamType : uint8;
enum class ECharacterStateType : uint8;
struct FInputActionValue;

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

	//更改碰撞体
	UFUNCTION(Server,Reliable,BlueprintCallable)
	void Server_ChangeBox(FVector Size, FVector Offset, int32 Box);

protected:
	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeBoxSize(FVector Size, int32 Box);



public:
	//角色朝向
	UPROPERTY(Replicated,BlueprintReadWrite)
	bool Toward = true;

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


	//复制角色朝向
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	
	//输入控制变量
	UPROPERTY(BlueprintReadWrite)
	bool bAttackInputLock = false;//普攻

	UPROPERTY(BlueprintReadWrite)
	bool bPreInputLock = true;//预输入

	UPROPERTY(BlueprintReadWrite)
	bool bTryTargetToward = false;//移动意图


	//攻击数值
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float DamageValue = 0.f;//伤害值

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackType DamageType = EAttackType::Push;//伤害类型

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector DamageEffect = FVector(0,0,0);//伤害影响位移

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EffectTime = 0.f;//伤害影响时间


	//替身数值
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeRange = 0.f;//替身范围

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeCD = 15.f;//替身CD
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeCDState = 0.f;//替身CD状态


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
	float LastEscapeTime = 0.f;//替身

	//击飞状态
	int32 LaunchState = 0;
};
