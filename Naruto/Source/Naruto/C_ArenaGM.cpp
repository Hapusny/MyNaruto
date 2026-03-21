// Fill out your copyright notice in the Description page of Project Settings.


#include "C_ArenaGM.h"
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

    AC_PlayerState* PS1 = Players[0]->GetPlayerState<AC_PlayerState>();
    AC_PlayerState* PS2 = Players[1]->GetPlayerState<AC_PlayerState>();

    if (PS1)
    {
        PS1->SetTeam(Player1Team);  
        SpawnPawnToPlayer(Player1Pawn, Players[0]);
        AC_Camera* NewCamera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
        NewCamera->SetOwner(Players[0]);
        APlayerController* Player = Players[0];
        FTimerHandle TimerHandle;
        GetWorldTimerManager().SetTimer(TimerHandle, [this, NewCamera, Player]()
            {
                NewCamera->Client_Activate(Player);
            }, 0.1f, false);
    }
    if (PS2)
    {
        PS2->SetTeam(Player2Team);
        SpawnPawnToPlayer(Player2Pawn, Players[1]);
        AC_Camera* NewCamera = GetWorld()->SpawnActor<AC_Camera>(PlayerCameraClass);
        NewCamera->SetOwner(Players[1]);
        APlayerController* Player = Players[1];
        FTimerHandle TimerHandle;
        GetWorldTimerManager().SetTimer(TimerHandle, [this, NewCamera, Player]()
            {
                NewCamera->Client_Activate(Player);
            }, 0.1f, false);
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
        if (PS->Team == ETeamType::Blue)NewPawn->Toward = true;
        else NewPawn->Toward = false;
        NewPawn->SetOwner(Player);
        Player->Possess(NewPawn);
    }
}