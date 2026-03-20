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
	// Sets default values for this actor's properties
	AC_Camera();

	UPROPERTY(VisibleAnywhere)
	UCameraComponent* Camera;

	TObjectPtr<AActor> TrackedActor;

	UFUNCTION(Client,Reliable)
	void Client_Activate(APlayerController* TargetPlayer);

	void StartTrack(AActor* Target);

	UPROPERTY(EditAnywhere)
	float UpdateInterval = 0.033f;

	UPROPERTY(EditAnywhere)
	FVector Offset = FVector(0, 0, 200);

	 UPROPERTY(EditAnywhere)
    float FollowSpeed = 10.0f;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	void UpdateTracking();

	FTimerHandle TrackingTimerHandle;

	bool bIsTracking = false;

	AActor* TargetActor;

};
