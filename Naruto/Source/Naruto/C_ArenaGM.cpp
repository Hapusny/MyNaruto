// Fill out your copyright notice in the Description page of Project Settings.


#include "C_ArenaGM.h"
#include "C_PlayerState.h"

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
        UE_LOG(LogTemp, Log, TEXT("Player1:%s"), *(UEnum::GetValueAsString(Player1Team)));
    }
    if (PS2)
    {
        PS2->SetTeam(Player2Team);
        UE_LOG(LogTemp, Log, TEXT("Player2:%s"), *(UEnum::GetValueAsString(Player2Team)));
    }
}
