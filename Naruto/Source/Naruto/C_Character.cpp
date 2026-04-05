// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Character.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "PaperFlipbookComponent.h"
#include "PaperZDAnimationComponent.h"
#include "PaperZDAnimInstance.h"
#include "C_PlayerController.h"
#include "C_PlayerState.h"

// Sets default values
AC_Character::AC_Character()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Flipbook = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("FlipbookComponent"));
	PaperZD = CreateDefaultSubobject<UPaperZDAnimationComponent>(TEXT("PaperZDComponent"));
	if (Flipbook)
	{
		Flipbook->SetupAttachment(RootComponent);
		Flipbook->SetRelativeRotation(FRotator(0.f, 0.0f, -90.0f));
		Flipbook->SetIsReplicated(true);
		if (PaperZD) {
			PaperZD->InitRenderComponent(Flipbook);
		}
	}
	bReplicates = true;
	SetReplicateMovement(true);
}

void AC_Character::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (IsLocallyControlled()) {
		Cast<AC_PlayerController>(NewController)->Server_ChangeCharacterState(ECharacterStateType::Normal);
	}
}

void AC_Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AC_Character, Toward);
}

// Called when the game starts or when spawned
void AC_Character::BeginPlay()
{
	Super::BeginPlay();

}

void AC_Character::Move(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		if ((MovementVector.Y > 0 && !Toward) || (MovementVector.Y < 0 && Toward))bTryToChangeToward = true;
		else bTryToChangeToward = false;

		AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
		if (PS && PS->Attack == 0) {
			if ((MovementVector.Y > 0 && !Toward) || (MovementVector.Y < 0 && Toward))Server_ChangeToward();
			// add movement 
			AddMovementInput(ForwardDirection, MovementVector.Y);
			AddMovementInput(RightDirection, MovementVector.X);
		}
	}
}

void AC_Character::Attack(const FInputActionValue& Value)
{
	bPreInputLock = true;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS && bAttackInputLock == false) {
		if (IsLocallyControlled()) {
			if(bTryToChangeToward)Server_ChangeToward();
			bAttackInputLock = true;
			int TargetAttack = PS->Attack + 1;
			PS->Attack = TargetAttack;
			Cast<AC_PlayerController>(Controller)->Server_ChangeAttackState(TargetAttack);
		}
	}
}

void AC_Character::Server_ChangeToward_Implementation()
{
	if(!Toward) Flipbook->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
	else Flipbook->SetRelativeRotation(FRotator(180.f, 0.f, -90.f));
	Toward = !Toward;
}

// Called every frame
void AC_Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void AC_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AC_Character::Move);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AC_Character::Attack);
	}
}

void AC_Character::StartPreInput()
{

}

