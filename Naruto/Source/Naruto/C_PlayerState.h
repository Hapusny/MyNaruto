// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "C_PlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTeamChanged);

//队伍
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
    Protected       UMETA(DisplayName = "Protected")
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


    //队伍相关
    FOnTeamChanged OnTeamChanged;

    UPROPERTY(ReplicatedUsing = OnRep_Team,BlueprintReadWrite)
    ETeamType Team = ETeamType::None;

    UFUNCTION()
    void OnRep_Team();

    UFUNCTION()
    void SetTeam(ETeamType TargetTeam);


    //角色数值
    UPROPERTY(Replicated, BlueprintReadWrite)
    float HealthValue = 300.f;

    UPROPERTY(Replicated, BlueprintReadWrite)
    int32 Chakra = 2;


    //角色状态
    UPROPERTY(Replicated, BlueprintReadWrite)
    int Attack = 0;

    UPROPERTY(Replicated, BlueprintReadWrite)
    int MySkill = 0;

    UPROPERTY(Replicated,BlueprintReadWrite)
    ECharacterStateType CharacterState = ECharacterStateType::Normal;


    //网络同步
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;


    //角色受击委托处理
    UFUNCTION()
    void PlayerGetDamage(FVector Location,float Damage);
};
