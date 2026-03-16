// Fill out your copyright notice in the Description page of Project Settings.


#include "C_BPLNetwork.h"

void UC_BPLNetwork::ServerTravelSeamless(UObject* WorldContextObject, FString MapPath)
{
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
    if (World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_ListenServer)
    {
        World->ServerTravel(MapPath);
    }
}
