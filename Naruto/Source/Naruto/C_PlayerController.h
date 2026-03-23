// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "C_PlayerController.generated.h"

class UInputMappingContext;
class UC_PlayerWidget;
/**
 * 
 */
UCLASS()
class NARUTO_API AC_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	virtual void BeginPlay()override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UC_PlayerWidget>PlayerWidgetClass;

	UFUNCTION(Client,Reliable)
	void Client_ShowWidget();

private:
	TObjectPtr<UC_PlayerWidget>PlayerWidget;
};
