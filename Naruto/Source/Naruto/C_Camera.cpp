// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Camera.h"
#include "Camera/CameraComponent.h"
#include "C_Character.h"

// Sets default values
AC_Camera::AC_Camera()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
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
        AActor* Tracked = TargetPlayer->GetPawn();
        TargetPlayer->SetViewTarget(this);
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

// Called when the game starts or when spawned
void AC_Camera::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void AC_Camera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AC_Camera::UpdateTracking()
{
    if (!TargetActor)return;

    AC_Character* Character = Cast<AC_Character>(TargetActor);
    FVector TargetOffset = Offset;
    if (Character)
    {
        if (Character->Toward)TargetOffset.X = Offset.X;
        else TargetOffset.X = - Offset.X;
    }
    FVector TargetLocation = TargetActor->GetActorLocation() + TargetOffset;

    if (TargetLocation.X > Max.X)TargetLocation.X = Max.X;
    if (TargetLocation.Y > Max.Y)TargetLocation.Y = Max.Y;
    if (TargetLocation.X < Min.X)TargetLocation.X = Min.X;
    if (TargetLocation.Y < Min.Y)TargetLocation.Y = Min.Y;

    FVector NewLocation = FMath::VInterpTo(
        GetActorLocation(),
        TargetLocation,
        UpdateInterval, 
        FollowSpeed
    );
    SetActorLocation(NewLocation);
}

