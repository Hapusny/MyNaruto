// Fill out your copyright notice in the Description page of Project Settings.


#include "C_ArenaGM.h"
#include "C_PlayerController.h"
#include "C_PlayerState.h"
#include "C_Character.h"
#include "C_Camera.h"


void AC_ArenaGM::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (NewPlayer)
    {
        Players.Add(NewPlayer);
    }
    if (Players.Num() == 2) 
    {
        AssignTeams();
    }
}

void AC_ArenaGM::BeginPlay()
{
	Super::BeginPlay();

}

void AC_ArenaGM::AssignTeams()
{
    if (Players.Num() != 2) return;

    bool bPlayer1IsBlue = FMath::RandBool();

    ETeamType Player1Team = bPlayer1IsBlue ? ETeamType::Blue : ETeamType::Red;
    ETeamType Player2Team = bPlayer1IsBlue ? ETeamType::Red : ETeamType::Blue;

    PS1 = Players[0]->GetPlayerState<AC_PlayerState>();
    PS2 = Players[1]->GetPlayerState<AC_PlayerState>();

    if (PS1)
    {
        PS1->ClockTime = 60;
        PS1->SetTeam(Player1Team);  
        SpawnPawnToPlayer(Player1Pawn, Players[0]);
        AC_Camera* NewCamera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
        NewCamera->SetOwner(Players[0]);
        APlayerController* Player = Players[0];
        FTimerHandle TimerHandle;
        GetWorldTimerManager().SetTimer(TimerHandle, [this, NewCamera, Player]()
            {
                NewCamera->Client_Activate(Player);
            }, 0.2f, false);
    }
    if (PS2)
    {
        PS2->ClockTime = 60;
        PS2->SetTeam(Player2Team);
        SpawnPawnToPlayer(Player2Pawn, Players[1]);
        AC_Camera* NewCamera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
        NewCamera->SetOwner(Players[1]);
        APlayerController* Player = Players[1];
        FTimerHandle TimerHandle;
        GetWorldTimerManager().SetTimer(TimerHandle, [this, NewCamera, Player]()
            {
                NewCamera->Client_Activate(Player);
                StartFight();
            }, 0.2f, false);
    }
}

void AC_ArenaGM::SpawnPawnToPlayer(TSubclassOf<AC_Character> PawnClass, APlayerController* Player)
{
    AC_PlayerState* PS = Player->GetPlayerState<AC_PlayerState>();
    AActor* PlayerStart;
    if (PS->Team == ETeamType::Blue)PlayerStart = FindPlayerStart(Player, "Blue");
    else PlayerStart = FindPlayerStart(Player, "Red");
    UWorld* World = GetWorld();
    if (Player->GetPawn())
    {
        Player->GetPawn()->Destroy();
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = Player;
    SpawnParams.Instigator = nullptr;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AC_Character* NewPawn = World->SpawnActor<AC_Character>(PawnClass, PlayerStart->GetTransform(), SpawnParams);

    if (NewPawn) {
        if (PS->Team == ETeamType::Red)NewPawn->Server_ChangeToward_Implementation();
        NewPawn->SetOwner(Player);
        Player->Possess(NewPawn);
    }
}

void AC_ArenaGM::StartFight()
{
    Player1 = Cast<AC_PlayerController>(Players[0]);
    Player2 = Cast<AC_PlayerController>(Players[1]);
    if (Player1 && PS1)
    {
        Player1->Client_ShowWidget();
        Player1->Client_SetWidgetTime(PS1->ClockTime);
        PS1->ClockTime--;
    }
    if (Player2 && PS2)
    {
        Player2->Client_ShowWidget();
        Player2->Client_SetWidgetTime(PS2->ClockTime);
        PS2->ClockTime--;
    }
    GetWorldTimerManager().SetTimer(
        ClockHandle,              // 定时器句柄（输出参数）
        this,                       // 调用对象
        &AC_ArenaGM::ClockChange,   // 回调函数指针
        1.0f,                       // 执行间隔（秒）
        true,                       // 是否循环
        0.0f                        // 首次延迟（可选，默认使用间隔）
    );
}

void AC_ArenaGM::ClockChange()
{
    if (Player1 && PS1)
    {
        Player1->Client_SetWidgetTime(PS1->ClockTime);
        PS1->ClockTime--;
    }
    if (Player2 && PS2)
    {
        Player2->Client_SetWidgetTime(PS2->ClockTime);
        PS2->ClockTime--;
    }
}
