// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "C_BPLNetwork.generated.h"

/**
 * 
 */
UCLASS()
class NARUTO_API UC_BPLNetwork : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	/** 服务器带领所有客户端无缝切换到新地图 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
	static void ServerTravelSeamless(UObject* WorldContextObject, FString MapPath);

};
