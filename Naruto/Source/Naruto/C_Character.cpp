
// Fill out your copyright notice in the Description page of Project Settings.


#include "C_Character.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "PaperFlipbookComponent.h"
#include "PaperSpriteComponent.h"
#include "PaperZDAnimationComponent.h"
#include "PaperZDAnimInstance.h"
#include "C_PlayerController.h"
#include "C_PlayerState.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameStateBase.h"

AC_Character::AC_Character()
{
	PrimaryActorTick.bCanEverTick = true;

	//碰撞框
	AttackBox = CreateDefaultSubobject<UBoxComponent>(TEXT("AttackBox"));
	AttackBox->SetupAttachment(RootComponent);
	AttackBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackBox->SetCollisionObjectType(ECC_EngineTraceChannel2);
	AttackBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	AttackBox->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	AttackBox->SetIsReplicated(true);

	PlayerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlayerBox"));
	PlayerBox->SetupAttachment(RootComponent);
	PlayerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PlayerBox->SetCollisionObjectType(ECC_EngineTraceChannel1);
	PlayerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PlayerBox->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Overlap);
	PlayerBox->SetIsReplicated(true);

	//攻击框绑定
	AttackBox->OnComponentBeginOverlap.AddDynamic(this, &AC_Character::OnAttackBoxOverlap);

	//动画表现
	PlaceMark = CreateDefaultSubobject<UPaperSpriteComponent>(TEXT("PaperSpriteComponent"));
	Flipbook = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("FlipbookComponent"));
	PaperZD = CreateDefaultSubobject<UPaperZDAnimationComponent>(TEXT("PaperZDComponent"));
	if (PlaceMark) {
		PlaceMark->SetupAttachment(RootComponent);
		PlaceMark->SetRelativeRotation(FRotator(0.f, 0.0f, -90.0f));
		PlaceMark->SetIsReplicated(true);
	}
	if (Flipbook)
	{
		Flipbook->SetupAttachment(RootComponent);
		Flipbook->SetRelativeRotation(FRotator(0.f, 0.0f, -90.0f));
		Flipbook->SetIsReplicated(true);
		if (PaperZD) {
			PaperZD->InitRenderComponent(Flipbook);
		}
	}

	//网络复制
	bReplicates = true;
	SetReplicateMovement(true);
}

void AC_Character::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	if (HasAuthority())return;
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (!PS)return;

	//Team同步完毕直接初始化，反之绑定委托等待同步
	if (Cast<AC_PlayerState>(GetPlayerState())->Team == ETeamType::None) {
		PS->OnTeamChanged.AddDynamic(this, &AC_Character::OnTeamChanged);
	}
	else MyInitialize(Cast<AC_PlayerState>(GetPlayerState())->Team);
}

void AC_Character::OnTeamChanged()
{
	//同步队伍信息进行初始化
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (PS && PS->Team != ETeamType::None)
	{
		MyInitialize(PS->Team);
	}
}

void AC_Character::MyInitialize(ETeamType team)
{
	//根据队伍信息设置朝向和位置标记
	if (team == ETeamType::Red) {
		if(Toward)Server_ChangeToward_Implementation(false);
		PlaceMark->SetSpriteColor(FColor::Red);
	}
	else {
		if(!Toward)Server_ChangeToward_Implementation(true);
		PlaceMark->SetSpriteColor(FColor::Blue);
	}
}

void AC_Character::BeDameged(float Damage, EAttackType Type, FVector Effect, float Time)
{
	//在PC中处理受击
	Cast<AC_PlayerController>(Controller)->PlayerGetDamage(Damage, Type, Effect, Time);
}

void AC_Character::AddChakra()
{
	AC_PlayerState* PS = Cast<AC_PlayerState>(GetPlayerState());
	if (!PS)return;
	if (PS->Chakra < 4)Cast<AC_PlayerController>(Controller)->Server_ChangeChakra_Implementation(PS->Chakra + 1);
}

void AC_Character::Server_ChangeBox_Implementation(FVector Size, FVector Offset, int32 Box)
{
	//根据标记确认更改的碰撞框
	UBoxComponent* TargetBox;
	if (Box == 0)TargetBox = PlayerBox;
	else TargetBox = AttackBox;

	//同步更改服务器和客户端的碰撞体大小
	Mult_ChangeBoxSize(Size, Box);

	//根据朝向设置碰撞体翻转
	if (!Toward)Offset.X = -Offset.X;
	TargetBox->SetRelativeLocation(Offset);
}


void AC_Character::Mult_ChangeBoxSize_Implementation(FVector Size, int32 Box)
{
	//根据标记确认更改的碰撞框
	UBoxComponent* TargetBox;
	if (Box == 0)TargetBox = PlayerBox;
	else TargetBox = AttackBox;

	//根据碰撞体尺寸设置碰撞性
	if (Size.IsNearlyZero())TargetBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	else TargetBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	//更改碰撞体大小
	TargetBox->SetBoxExtent(Size);
}

void AC_Character::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AC_Character, Toward);
}

void AC_Character::ChangeAttack(int32 attack)
{
	if (attack == 0) {
		Cast<AC_PlayerController>(Controller)->Server_ChangeAttackState(0);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(0);

		//定时器控制普攻输入锁解锁时机
		if (AttackCheckTimerHandle.IsValid())GetWorldTimerManager().ClearTimer(AttackCheckTimerHandle);
		GetWorldTimerManager().SetTimer(
			AttackCheckTimerHandle,
			[this]()
			{
				if (GetPlayerState<AC_PlayerState>()->Attack == 0) {
					bAttackInputLock = false;
					GetWorldTimerManager().ClearTimer(AttackCheckTimerHandle);
				}
			},
			0.017f,
			true
		);
		return;
	}
	if (bPreInputLock) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeAttackState(attack);
	}
	else bAttackInputLock = false;
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

		//获取移动意图
		TryTargetToward.X = MovementVector.Y;
		TryTargetToward.Y = MovementVector.X;

		AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
		if(!PS)return;

		//移动可行性判断
		if(PS->Attack != 0 || PS->MySkill != 0)return;
		if (PS->CharacterState == ECharacterStateType::Staggered)return;
		if (PS->CharacterState == ECharacterStateType::Launched)return;
		if (PS->CharacterState == ECharacterStateType::Grabbed)return;

		//转向处理
		if (MovementVector.Y > 0 && !Toward)Server_ChangeToward(true);
		if (MovementVector.Y < 0 && Toward)Server_ChangeToward(false);
		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AC_Character::Attack(const FInputActionValue& Value)
{
	bPreInputLock = true;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (PS && bAttackInputLock == false) {
		if (IsLocallyControlled()) {
			if(TryTargetToward.X > 0)Server_ChangeToward(true);
			if(TryTargetToward.X < 0)Server_ChangeToward(false);
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
	if (EscapeCDState == 0.f) {
		if (PS->CharacterState == ECharacterStateType::Staggered || PS->CharacterState == ECharacterStateType::Launched || PS->CharacterState == ECharacterStateType::Grabbed) {
			LastEscapeTime = GameState->GetServerWorldTimeSeconds();
			FVector TargetPlace = GetActorLocation();
			for (APlayerState* OtherPS : GameState->PlayerArray) {
				if (Cast<AC_PlayerState>(OtherPS)->Team != PS->Team) {
					float Distance = FVector::Distance(OtherPS->GetPawn()->GetActorLocation(), GetActorLocation());
					if (Distance <= EscapeRange)TargetPlace = OtherPS->GetPawn()->GetActorLocation();
				}
			}
			SetActorLocation(TargetPlace);
			Cast<AC_PlayerController>(Controller)->Server_ChangeCharacterState(ECharacterStateType::Normal);
			Cast<AC_PlayerController>(Controller)->Server_EscapeEffect();
		}
	}
}

void AC_Character::FirstSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!GameState)return;
	if (FirstSkillCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(1);
		LastFirstSkillTime = GameState->GetServerWorldTimeSeconds();
		BP_FirstSkillEffect();
	}
}

void AC_Character::SecondSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!GameState)return;
	if (SecondSkillCDState == 0.f) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(2);
		LastSecondSkillTime = GameState->GetServerWorldTimeSeconds();
		BP_SecondSkillEffect();
	}
}

void AC_Character::FinalSkill(const FInputActionValue& Value)
{
	if (!IsLocallyControlled())return;
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>();
	if (!PS)return;
	if (!GameState)return;
	if (PS->Chakra == 4) {
		if (TryTargetToward.X > 0)Server_ChangeToward(true);
		if (TryTargetToward.X < 0)Server_ChangeToward(false);
		Cast<AC_PlayerController>(Controller)->Server_ChangeChakra(0);
		Cast<AC_PlayerController>(Controller)->Server_ChangeSkillState(3);
		BP_FinalSkillEffect();
	}
}

void AC_Character::Scroll(const FInputActionValue& Value)
{
	
}

void AC_Character::Summon(const FInputActionValue& Value)
{
	
}

void AC_Character::Server_ChangeToward_Implementation(bool TargetToward)
{
	if(TargetToward) Flipbook->SetRelativeRotation(FRotator(0.f, 0.f, -90.f));
	else Flipbook->SetRelativeRotation(FRotator(180.f, 0.f, -90.f));
	Toward = TargetToward;
}

void AC_Character::OnAttackBoxOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (OtherActor == this)return;
	if (HasAuthority()) {
		FVector Effect = DamageEffect;
		if (Toward == false)Effect.X = -Effect.X;
		if (DamageType == EAttackType::Grab)Effect = Effect + GetActorLocation();

		//被攻击的对象受到伤害
		Cast<AC_Character>(OtherActor)->BeDameged(DamageValue, DamageType, Effect, EffectTime);
	}
}

// Called every frame
void AC_Character::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	//每帧获取角色信息
	GetInformation();

	//根据角色高度同步动画高度
	if (GetActorLocation().Z > 0) {
		Flipbook->SetRelativeLocation(FVector(0, -GetActorLocation().Z,0));
	}

	//角色移动范围限制
	if (GetActorLocation().X > MaxLocation.X)SetActorLocation(FVector(MaxLocation.X, GetActorLocation().Y, GetActorLocation().Z));
	if (GetActorLocation().Y > MaxLocation.Y)SetActorLocation(FVector(GetActorLocation().X, MaxLocation.Y, GetActorLocation().Z));
	if (GetActorLocation().X < MinLocation.X)SetActorLocation(FVector(MinLocation.X, GetActorLocation().Y, GetActorLocation().Z));
	if (GetActorLocation().Y < MinLocation.Y)SetActorLocation(FVector(GetActorLocation().X, MinLocation.Y, GetActorLocation().Z));

	//受击处理
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

	//CD处理
	//替身
	if (LastEscapeTime != 0.f) {
		EscapeCDState = EscapeCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastEscapeTime);
		if (EscapeCDState <= 0.f)EscapeCDState = 0.f;
	}
	else EscapeCDState = 0.f;

	//一技能
	if (LastFirstSkillTime != 0.f) {
		FirstSkillCDState = FirstSkillCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastFirstSkillTime);
		if (FirstSkillCDState <= 0.f)FirstSkillCDState = 0.f;
	}
	else FirstSkillCDState = 0.f;


	//二技能
	if (LastSecondSkillTime != 0.f) {
		SecondSkillCDState = SecondSkillCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastSecondSkillTime);
		if (SecondSkillCDState <= 0.f)SecondSkillCDState = 0.f;
	}
	else SecondSkillCDState = 0.f;

	//秘卷
	if (LastScrollTime != 0.f) {
		ScrollCDState = ScrollCD - (GetWorld()->GetGameState()->GetServerWorldTimeSeconds() - LastScrollTime);
		if (ScrollCDState <= 0.f)ScrollCDState = 0.f;
	}
	else ScrollCDState = 0.f;
}

void AC_Character::GetInformation()
{
	AC_PlayerState* PS = GetPlayerState<AC_PlayerState>();
	if (!PS)return;
	MySpeed = GetVelocity().Length();
	MyAttack = PS->Attack;
	MyCState = PS->CharacterState;
	MySkill = PS->MySkill;
}

// Called to bind functionality to input
void AC_Character::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	//输入绑定
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AC_Character::Move);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &AC_Character::Attack);
		EnhancedInputComponent->BindAction(EscapeAction, ETriggerEvent::Triggered, this, &AC_Character::Escape);
		EnhancedInputComponent->BindAction(FirstSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FirstSkill);
		EnhancedInputComponent->BindAction(SecondSkillAction, ETriggerEvent::Triggered, this, &AC_Character::SecondSkill);
		EnhancedInputComponent->BindAction(FinalSkillAction, ETriggerEvent::Triggered, this, &AC_Character::FinalSkill);
		EnhancedInputComponent->BindAction(ScrollAction, ETriggerEvent::Triggered, this, &AC_Character::Scroll);
		EnhancedInputComponent->BindAction(SummonAction, ETriggerEvent::Triggered, this, &AC_Character::Summon);
	}
}

