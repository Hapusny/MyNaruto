
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
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"

// Sets default values
AC_Character::AC_Character()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AttackBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AttackBox"));
	AttackBox->SetupAttachment(RootComponent);
	AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackBox->SetCollisionObjectType(ECC_EngineTraceChannel2);
	AttackBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	AttackBox->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	AttackBox->SetIsReplicated(true);

	AttackBox->OnComponentBeginOverlap.AddDynamic(this, &AC_Character::OnAttackBoxOverlap);

	PlayerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlayerBox"));
	PlayerBox->SetupAttachment(RootComponent);
	PlayerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PlayerBox->SetCollisionObjectType(ECC_EngineTraceChannel1);
	PlayerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PlayerBox->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Overlap);
	PlayerBox->SetIsReplicated(true);

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

void AC_Character::Server_ChangeBox_Implementation(FVector Size, FVector Offset, int32 Box)
{
	UBoxComponent* TargetBox;
	if (Box == 0)TargetBox = PlayerBox;
	else TargetBox = AttackBox;
	if (Size.IsNearlyZero())TargetBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	else TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetBox->SetBoxExtent(Size);
	Mult_ChangeBoxSize(Size, Box);
	if (!Toward)Offset.X = -Offset.X;
	TargetBox->SetRelativeLocation(Offset);
}


void AC_Character::Mult_ChangeBoxSize_Implementation(FVector Size, int32 Box)
{
	UBoxComponent* TargetBox;
	if (Box == 0)TargetBox = PlayerBox;
	else TargetBox = AttackBox;
	if (Size.IsNearlyZero())TargetBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	else TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TargetBox->SetBoxExtent(Size);
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
		if (PS && PS->Attack == 0  && PS->CharacterState != ECharacterStateType::Staggered && PS->CharacterState != ECharacterStateType::Launched) {
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
			Cast<AC_PlayerController>(Controller)->Server_ChangeAttackState(TargetAttack);
		}
	}
}

void AC_Character::Escape(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!GameState)return;
	if (!PS)return;
	if (PS->Chakra == 0)return;
	if (LastEscapeTime == 0.f || GameState->GetServerWorldTimeSeconds() - LastEscapeTime >= EscapeCD) {
		LastEscapeTime = GameState->GetServerWorldTimeSeconds();
		FVector TargetPlace = GetActorLocation();
		if (PS->CharacterState == ECharacterStateType::Staggered || PS->CharacterState == ECharacterStateType::Launched || PS->CharacterState == ECharacterStateType::Grabbed) {
			for (APlayerState* OtherPS : GameState->PlayerArray) {
				if (Cast<AC_PlayerState>(OtherPS)->Team != PS->Team) {
					float Distance = FVector::Distance(OtherPS->GetPawn()->GetActorLocation(), GetActorLocation());
					if (Distance <= EscapeRange)TargetPlace = OtherPS->GetPawn()->GetActorLocation();
				}
			}
		}
		SetActorLocation(TargetPlace);
		Cast<AC_PlayerController>(Controller)->Server_ChangeCharacterState(ECharacterStateType::Normal);
		Cast<AC_PlayerController>(Controller)->Server_EscapeEffect();
	}
}

void AC_Character::FirstSkill(const FInputActionValue& Value)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		if (IsLocallyControlled()) {
			Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(1);
		}
	}
}

void AC_Character::SecondSkill(const FInputActionValue& Value)
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS) {
		if (IsLocallyControlled()) {
			Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(2);
		}
	}
}

void AC_Character::FinalSkill(const FInputActionValue& Value)
{
}

void AC_Character::Server_ChangeToward_Implementation()
{
	if(!Toward) Flipbook->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
	else Flipbook->SetRelativeRotation(FRotator(180.f, 0.f, -90.f));
	Toward = !Toward;
}

void AC_Character::OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor == this)return;
	if (HasAuthority()) {
		FVector Effect = DamageEffect;
		if (Toward == false)Effect.X = -Effect.X;
		if (DamageType == EAttackType::Grab)Effect = Effect + GetActorLocation();
		Cast<AC_PlayerController>(Cast<AC_Character>(OtherActor)->Controller)->PlayerGetDamage(DamageValue,DamageType,Effect,EffectTime);
	}
}

// Called every frame
void AC_Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (GetActorLocation().Z > 0) {
		Flipbook->SetRelativeLocation(FVector(0, -GetActorLocation().Z,0));
	}
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	if (PS->CharacterState == ECharacterStateType::Launched) {
		if (LaunchState == 0 && GetActorLocation().Z > 5.f)LaunchState = 1;
		if (LaunchState == 1 && GetActorLocation().Z < 5.f) {
			PS->CharacterState = ECharacterStateType::Normal;
			LaunchState = 0;
		}
	}
	if (PS->CharacterState == ECharacterStateType::Grabbed) {
		GetCharacterMovement()->GravityScale = 0.f;
	}
	else {
		GetCharacterMovement()->GravityScale = 1.f;
	}
	GEngine->AddOnScreenDebugMessage(-1, 0.06f, FColor::Blue, FString::Printf(TEXT("Chakra: %d"), PS->Chakra));
	if (LastEscapeTime != 0.f) {
		GEngine->AddOnScreenDebugMessage(-1, 0.06f, FColor::Green,FString::Printf(TEXT("Time: %.2f"), GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastEscapeTime));
	}
}

// Called to bind functionality to input
void AC_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AC_Character::Move);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AC_Character::Attack);
		EnhancedInputComponent->BindAction(EscapeAction, ETriggerEvent::Triggered, this, &AC_Character::Escape);
		EnhancedInputComponent->BindAction(FirstSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FirstSkill);
		EnhancedInputComponent->BindAction(SecondSkillAction, ETriggerEvent::Triggered, this, &AC_Character::SecondSkill);
		EnhancedInputComponent->BindAction(FinalSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FinalSkill);
	}
}

