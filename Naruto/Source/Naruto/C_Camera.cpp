// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Camera.h"

// Sets default values
AC_Camera::AC_Camera()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

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

