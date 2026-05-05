// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "C_Camera.generated.h"

class APlayerController;
class UCameraComponent;

UCLASS()
class NARUTO_API AC_Camera : public AActor
{
	GENERATED_BODY()
	
public:	
	AC_Camera();

	UPROPERTY(VisibleAnywhere)
	UCameraComponent* Camera;

	//激活摄像头
	UFUNCTION(Client,Reliable)
	void Client_Activate(APlayerController* TargetPlayer);

	//启动追踪
	void StartTrack(AActor* Target);


	//追踪设定
	UPROPERTY(EditAnywhere)
	float UpdateInterval = 0.017f;//更新频率

	UPROPERTY(EditAnywhere)
	FVector Offset = FVector(0, 0, 200);//视角偏移

	UPROPERTY(EditAnywhere)
    float FollowSpeed = 10.0f;//速率


	//视角范围
	UPROPERTY(EditAnywhere)
	FVector2D Max = FVector2D(100,100);

	UPROPERTY(EditAnywhere)
	FVector2D Min = FVector2D(-100, -100);

private:
	//追踪
	void UpdateTracking();

	//追踪定时器
	FTimerHandle TrackingTimerHandle;

	//是否启用追踪
	bool bIsTracking = false;

	//视角追踪的对象
	AActor* TargetActor;

};
