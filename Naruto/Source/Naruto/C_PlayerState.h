// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "C_PlayerState.generated.h"


UENUM(BlueprintType)
enum class ETeamType : uint8
{
    None    UMETA(DisplayName = "无队伍"),
    Blue    UMETA(DisplayName = "蓝队"),
    Red     UMETA(DisplayName = "红队")
};
/**
 * 
 */
UCLASS()
class NARUTO_API AC_PlayerState : public APlayerState
{
	GENERATED_BODY()
	
public:

    AC_PlayerState();

    UPROPERTY(Replicated)
    ETeamType Team;

    UFUNCTION()
    void SetTeam(ETeamType TargetTeam);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
