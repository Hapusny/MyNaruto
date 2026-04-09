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


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	void Move(const FInputActionValue& Value);

	void Attack(const FInputActionValue& Value);

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

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
