// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "C_PlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTeamChanged);

//预测系统：权威值表组件（设计 2.7.5 / 计划 2.4，只在构造函数里创建，定义见 C_AuthorityValueComponent.h）
class UC_AuthorityValueComponent;

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

    //权威值表组件（设计 2.7.5 / 计划 2.4）：承载本 PS 上被预测属性（HealthValue / Chakra / Attack /
    //MySkill / CharacterState）的权威值。服务器在 PreReplication 里从真实属性刷新，客户端收到表后
    //按采用规则写回 —— 这五个属性已从逐属性复制中摘除（设计 5.2b），只走这条通道
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UC_AuthorityValueComponent> AuthorityValueComponent;


    //队伍相关
    FOnTeamChanged OnTeamChanged;

    UPROPERTY(ReplicatedUsing = OnRep_Team,BlueprintReadWrite)
    ETeamType Team = ETeamType::None;

    UFUNCTION()
    void OnRep_Team();

    UFUNCTION()
    void SetTeam(ETeamType TargetTeam);


    //角色数值
    //预测系统（设计 5.2b / 计划 2.4）：以下五个属性不再走逐属性复制（DOREPLIFETIME 已删），
    //改由本 PS 上的 AuthorityValueComponent.AuthorityValueTable 下发。UPROPERTY 保留，本地读写照旧；
    //也不要给它们补 ReplicatedUsing 或写 OnRep —— 采用与否由表到达时按采用规则统一决定（设计 5.2c）
    UPROPERTY(BlueprintReadWrite)
    float HealthValue = 300.f;

    UPROPERTY(BlueprintReadWrite)
    int32 Chakra = 2;


    //角色状态
    UPROPERTY(BlueprintReadWrite)
    int Attack = 0;

    UPROPERTY(BlueprintReadWrite)
    int MySkill = 0;

    UPROPERTY(BlueprintReadWrite)
    ECharacterStateType CharacterState = ECharacterStateType::Normal;


    //网络同步
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;


    //角色受击委托处理
    UFUNCTION()
    void PlayerGetDamage(FVector Location,float Damage);
};
