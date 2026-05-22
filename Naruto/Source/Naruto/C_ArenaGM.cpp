// Fill out your copyright notice in the Description page of Project Settings.


#include "C_ArenaGM.h"
#include "C_PlayerController.h"
#include "C_PlayerState.h"
#include "C_Character.h"
#include "C_Camera.h"
#include "C_ArenaGS.h"
#include "GameFramework/GameStateBase.h"


void AC_ArenaGM::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (NewPlayer)
    {
        Players.Add(NewPlayer);
        UE_LOG(LogTemp, Warning, TEXT("PlayerAdd"));
    }
    if (Players.Num() == 2) //玩家数为2时分配队伍
    {
        AssignTeams();
    }
}


void AC_ArenaGM::HandleSeamlessTravelPlayer(AController*& C)
{
    Super::HandleSeamlessTravelPlayer(C);

    APlayerController* PC = Cast<APlayerController>(C);
    if (PC)
    {
        Players.AddUnique(PC);

        if (Players.Num() == 2)
        {
            //玩家数为2时分配队伍
            FTimerHandle TimerHandle;
            GetWorldTimerManager().SetTimer(TimerHandle, this, &AC_ArenaGM::AssignTeams, 0.2f, false);
        }
    }
}

void AC_ArenaGM::GameTerminate()
{
    Player1->Client_ChangeInputAbility(false);
    Player2->Client_ChangeInputAbility(false);
    if (PS1->HealthValue > PS2->HealthValue) {
        if (Player1->GetPlayerState<AC_PlayerState>()->Team == ETeamType::Blue) {//蓝方获胜
            Player1->Client_SetWidgetEnd(0);
            Player2->Client_SetWidgetEnd(0);
        }
        else {//红方获胜
            Player1->Client_SetWidgetEnd(1);
            Player2->Client_SetWidgetEnd(1);
        }
    }
    else if (PS1->HealthValue < PS2->HealthValue) {
        if (Player2->GetPlayerState<AC_PlayerState>()->Team == ETeamType::Blue) {//蓝方获胜
            Player1->Client_SetWidgetEnd(0);
            Player2->Client_SetWidgetEnd(0);
        }
        else {//红方获胜
            Player1->Client_SetWidgetEnd(1);
            Player2->Client_SetWidgetEnd(1);
        }
    }
    else {//平局
        Player1->Client_SetWidgetEnd(2);
        Player2->Client_SetWidgetEnd(2);
    }
    
    //3s后结束对战
    FTimerHandle ExitTimerHandle;
    GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &AC_ArenaGM::BackToLobby, 3.0f, false);
}

void AC_ArenaGM::SetPlayerPauseState(int player, bool state)
{
    AC_PlayerController* TargetPlayer;
    if (player) {//目标为红方
        if (PS1->Team == ETeamType::Red)TargetPlayer = Player1;
        else TargetPlayer = Player2;
    }
    else {//目标为蓝方
        if (PS1->Team == ETeamType::Blue)TargetPlayer = Player1;
        else TargetPlayer = Player2;
    }
    if (state) {
        TargetPlayer->GetPawn()->CustomTimeDilation = 1.0f;
        TargetPlayer->Client_ChangeInputAbility(true);
    }
    else {
        TargetPlayer->GetPawn()->CustomTimeDilation = 0.0f;
        TargetPlayer->Client_ChangeInputAbility(true);
    }
}

void AC_ArenaGM::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    //更新时间
    if (MyGameState && MyGameState->FightStartTime != 0.f) {//战斗开始启动时钟后
        float ClockTime = GameFightTime - (GameState->GetServerWorldTimeSeconds() - Cast<AC_ArenaGS>(GameState)->FightStartTime);//比赛剩余时间
        if (Player1 && Player2) {
            //更新时间展示
            Player1->Client_SetWidgetTime(ClockTime);
            Player2->Client_SetWidgetTime(ClockTime);
        }
        if (Player1->GetPlayerState<AC_PlayerState>()->HealthValue <= 0.f || Player2->GetPlayerState<AC_PlayerState>()->HealthValue <= 0.f || ClockTime <= 0.f) {
            GameTerminate();
        }
    }
}

void AC_ArenaGM::AssignTeams()
{
    if (Players.Num() != 2) return;

    //随机分配红蓝方
    bool bPlayer1IsBlue = FMath::RandBool();

    ETeamType Player1Team = bPlayer1IsBlue ? ETeamType::Blue : ETeamType::Red;
    ETeamType Player2Team = bPlayer1IsBlue ? ETeamType::Red : ETeamType::Blue;

    PS1 = Players[0]->GetPlayerState<AC_PlayerState>();
    PS2 = Players[1]->GetPlayerState<AC_PlayerState>();

    if (!(PS1 && PS2))return;
    //更新PS数据
    PS1->SetTeam(Player1Team);
    PS2->SetTeam(Player2Team);

    //生成并控制选择的忍者
    SpawnPawnToPlayer(Player1Pawn, Players[0]);
    SpawnPawnToPlayer(Player2Pawn, Players[1]);

    //生成并绑定摄像头
    AC_Camera* Player1Camera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
    AC_Camera* Player2Camera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
    Player1Camera->SetOwner(Players[0]);
    Player2Camera->SetOwner(Players[1]);

    //短暂延迟后启用摄像头并开始战斗回合
    APlayerController* PlayerNo1 = Players[0];
    APlayerController* PlayerNo2 = Players[1];
    FTimerHandle TimerHandle;
    GetWorldTimerManager().SetTimer(TimerHandle, [this, Player1Camera, PlayerNo1, Player2Camera, PlayerNo2]()
    {
        Player1Camera->Client_Activate(PlayerNo1);
        Player2Camera->Client_Activate(PlayerNo2);

        StartFight();
    }, 0.2f, false);
}

void AC_ArenaGM::SpawnPawnToPlayer(TSubclassOf<AC_Character> PawnClass, APlayerController* Player)
{
    AC_PlayerState* PS = Player->GetPlayerState<AC_PlayerState>();

    //根据队伍选择对应出生点
    AActor* PlayerStart;
    if (PS->Team == ETeamType::Blue)PlayerStart = FindPlayerStart(Player, "Blue");
    else PlayerStart = FindPlayerStart(Player, "Red");

    //生成忍者
    if (Player->GetPawn())
    {
        Player->GetPawn()->Destroy();
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = Player;
    SpawnParams.Instigator = nullptr;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    AC_Character* NewPawn = GetWorld()->SpawnActor<AC_Character>(PawnClass, PlayerStart->GetTransform(), SpawnParams);

    //忍者初始化并玩家控制
    if (NewPawn) {
        NewPawn->SetOwner(Player);
        NewPawn->MyInitialize(PS->Team);
        Player->Possess(NewPawn);
    }
}

void AC_ArenaGM::StartFight()
{
    Player1 = Cast<AC_PlayerController>(Players[0]);
    Player2 = Cast<AC_PlayerController>(Players[1]);
    MyGameState = Cast<AC_ArenaGS>(GameState);
    if (Player1 && PS1)
    {
        Player1->PlayerBeAttacked.AddUObject(PS1, &AC_PlayerState::PlayerGetDamage);//PS绑定受击委托
        Player1->Client_ShowWidget();//玩家UI
    }
    if (Player2 && PS2)
    {
        Player2->PlayerBeAttacked.AddUObject(PS2, &AC_PlayerState::PlayerGetDamage);
        Player2->Client_ShowWidget();
    }

    //3s后开始战斗
    FTimerHandle TimerHandle;
    GetWorldTimerManager().SetTimer(TimerHandle, [this]()
        {
            Cast<AC_ArenaGS>(GameState)->FightStartTime = GameState->GetServerWorldTimeSeconds();//启动回合时钟
            Player1->Client_ChangeInputAbility(true);
            Player2->Client_ChangeInputAbility(true);

        }, 3.0f, false);
}

void AC_ArenaGM::BackToLobby()
{
    if (Player1->IsLocalPlayerController()) {
        Player2->ClientTravel(TEXT("/Game/Game/Map/Lobby"), TRAVEL_Absolute);
        Player1->ClientTravel(TEXT("/Game/Game/Map/Lobby"), TRAVEL_Absolute);
    }
    else {
        Player1->ClientTravel(TEXT("/Game/Game/Map/Lobby"), TRAVEL_Absolute);
        Player2->ClientTravel(TEXT("/Game/Game/Map/Lobby"), TRAVEL_Absolute);
    }
}

