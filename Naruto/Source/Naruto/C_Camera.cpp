// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Camera.h"
#include "Camera/CameraComponent.h"
#include "C_Character.h"

AC_Camera::AC_Camera()
{
	PrimaryActorTick.bCanEverTick = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    RootComponent = Camera;
    Camera->SetRelativeRotation(FRotator(-90, -90, 0));
	bReplicates = true;
}

void AC_Camera::Client_Activate_Implementation(APlayerController* TargetPlayer)
{
    if (TargetPlayer)
    {
        if (!TargetPlayer->IsLocalController())return;

        //设置追踪对象并激活
        AActor* Tracked = TargetPlayer->GetPawn();
        TargetPlayer->SetViewTarget(this);

        //开始追踪
        StartTrack(Tracked);
    }
}

void AC_Camera::StartTrack(AActor* Target)
{
    TargetActor = Target;
    if (bIsTracking)
    {
        bIsTracking = false;
        GetWorldTimerManager().ClearTimer(TrackingTimerHandle);
    }
    bIsTracking = true;
    GetWorldTimerManager().SetTimer(
        TrackingTimerHandle,
        this,
        &AC_Camera::UpdateTracking,
        UpdateInterval,
        true,  
        0.f   
    );
}

void AC_Camera::UpdateTracking()
{
    if (!TargetActor)return;

    AC_Character* Character = Cast<AC_Character>(TargetActor);

    //根据角色朝向设置视角偏移
    FVector TargetOffset = Offset;
    if (Character)
    {
        if (Character->Toward)TargetOffset.X = Offset.X;
        else TargetOffset.X = - Offset.X;
    }
    FVector TargetLocation = TargetActor->GetActorLocation() + TargetOffset;


    //视角范围限定
    if (TargetLocation.X > Max.X)TargetLocation.X = Max.X;
    if (TargetLocation.Y > Max.Y)TargetLocation.Y = Max.Y;
    if (TargetLocation.X < Min.X)TargetLocation.X = Min.X;
    if (TargetLocation.Y < Min.Y)TargetLocation.Y = Min.Y;


    //插值更新摄像头位置
    FVector NewLocation = FMath::VInterpTo(
        GetActorLocation(),
        TargetLocation,
        UpdateInterval, 
        FollowSpeed
    );
    SetActorLocation(NewLocation);
}

