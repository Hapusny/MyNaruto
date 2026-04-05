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
struct FInputActionValue;

UCLASS()
class NARUTO_API AC_Character : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AC_Character();

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

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UFUNCTION(BlueprintCallable)
	void StartPreInput();

};
