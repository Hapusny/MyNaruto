// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "C_PlayerState.generated.h"


UENUM(BlueprintType)
enum class ETeamType : uint8
{
    None    UMETA(DisplayName = "None"),
    Blue    UMETA(DisplayName = "Blue"),
    Red     UMETA(DisplayName = "Red")
};

/*
常态：角色初始状态
硬体：技能释放期间进入，不会被常态下的非抓取攻击打断
霸体：特殊状态，不会被非抓取攻击打断
金刚体：奥义释放期间进入，不会被打断
平推：受击状态，短暂僵直
击飞：受击状态，空中下落
倒地：受击状态，只会受到扫地攻击，短暂时间后起身
被抓取：受击状态，无法替身
保护：起身后短暂进入，无法受到攻击
*/
UENUM(BlueprintType)
enum class ECharacterStateType : uint8
{
    Normal          UMETA(DisplayName = "Normal"),
    Armor           UMETA(DisplayName = "Armor"),
    Unbreakable     UMETA(DisplayName = "Unbreakable"),
    Adamantine      UMETA(DisplayName = "Adamantine"),
    Staggered       UMETA(DisplayName = "Staggered"),
    Launched        UMETA(DisplayName = "Launched"),
    Downed          UMETA(DisplayName = "Downed"),
    Grabbed         UMETA(DisplayName = "Grabbed"),
    Invincible      UMETA(DisplayName = "Invincible")
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

    UPROPERTY(Replicated,BlueprintReadWrite)
    ETeamType Team;

    UPROPERTY(Replicated, BlueprintReadWrite)
    int Attack = 0;

    UPROPERTY(Replicated,BlueprintReadWrite)
    ECharacterStateType CharacterState;

    UPROPERTY(Replicated)
    int ClockTime;

    UFUNCTION()
    void SetTeam(ETeamType TargetTeam);

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
