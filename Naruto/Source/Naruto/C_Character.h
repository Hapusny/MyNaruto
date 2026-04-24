// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "C_Character.generated.h"

class UInputMappingContext;
class UInputAction;
class UPaperFlipbookComponent;
class UPaperZDAnimationComponent;
class AC_PlayerState;
class UBoxComponent;
struct FInputActionValue;

UENUM(BlueprintType)
enum class EAttackType : uint8
{
	Push	UMETA(DisplayName = "Push"),
	Launch  UMETA(DisplayName = "Launch"),
	Grab    UMETA(DisplayName = "Grab")
};

UCLASS()
class NARUTO_API AC_Character : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AC_Character();

	// 攻击框组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UBoxComponent> AttackBox;

	// 受击框组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UBoxComponent> PlayerBox;

	UFUNCTION(Server,Reliable,BlueprintCallable)
	void Server_ChangeBox(FVector Size, FVector Offset, int32 Box);

	UPROPERTY(Replicated,BlueprintReadWrite)
	bool Toward = true;

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

	virtual void PossessedBy(AController* NewController)override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UPaperFlipbookComponent> Flipbook;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UPaperZDAnimationComponent> PaperZD;

	UPROPERTY(BlueprintReadWrite)
	bool bAttackInputLock = false;

	UPROPERTY(BlueprintReadWrite)
	bool bPreInputLock = true;

	UPROPERTY(BlueprintReadWrite)
	bool bTryToChangeToward = false;

	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	float DamageValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttackType DamageType = EAttackType::Push;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector DamageEffect = FVector(0,0,0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EffectTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float EscapeCD = 15.f;


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void Move(const FInputActionValue& Value);

	void Attack(const FInputActionValue& Value);

	void Escape(const FInputActionValue& Value);

	void FirstSkill(const FInputActionValue& Value);

	void SecondSkill(const FInputActionValue& Value);

	void FinalSkill(const FInputActionValue& Value);

	void Scroll(const FInputActionValue& Value);

	void Summon(const FInputActionValue& Value);


	UFUNCTION(Server,Reliable,BlueprintCallable)
	void Server_ChangeToward();

	UFUNCTION(NetMulticast, Reliable)
	void Mult_ChangeBoxSize(FVector Size, int32 Box);

	UFUNCTION()
	void OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	int32 LaunchState = 0;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
	float LastEscapeTime = 0.f;


};
