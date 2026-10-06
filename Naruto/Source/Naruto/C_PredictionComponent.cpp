// Fill out your copyright notice in the Description page of Project Settings.


#include "C_PredictionComponent.h"
#include "C_AuthorityValueComponent.h"
#include "C_Character.h"
#include "C_PlayerState.h"
#include "C_PlayerController.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

DEFINE_LOG_CATEGORY(LogPrediction);

namespace
{
	// BindStateLifecycle 支持的 StateName（设计 3.4.2）：带前缀、且落在 AC_PlayerState 上的可预测属性。
	// 这三个名字同时是 TickPredictionTimeout 解析生命周期绑定时要读的属性，增删必须两处同步。
	bool IsSupportedLifecycleStateName(FName StateName)
	{
		static const FName SupportedNames[] =
		{
			FName(TEXT("Self.PS.CharacterState")),
			FName(TEXT("Self.PS.MySkill")),
			FName(TEXT("Self.PS.Attack")),
		};

		for (const FName& Name : SupportedNames)
		{
			if (StateName == Name)
			{
				return true;
			}
		}
		return false;
	}

	// 读生命周期绑定要比较的当前值（设计 3.4.2 / 3.4.5）：三个 StateName 都落在 AC_PlayerState 上。
	// 与 IsSupportedLifecycleStateName 是同一组名字，增删必须两处同步。
	// 返回 false 有两种情形：名字不在这一组里（BindStateLifecycle 本就不会让它进表）、
	// 或 OwnerPS 为空（客户端 PlayerState 尚未同步到达 —— 由调用方下一帧重试，不算异常）
	bool ReadLifecycleStateValue(FName StateName, const AC_PlayerState* OwnerPS, uint8& OutValue)
	{
		if (OwnerPS == nullptr)
		{
			return false;
		}

		// EndStateValue 是 uint8（设计 3.4.2），这里把属性值也折算到 uint8 再比。属性值域都是小整数
		//（CharacterState 0-15、Attack / MySkill 0-4），折算不会产生歧义
		if (StateName == FName(TEXT("Self.PS.CharacterState")))
		{
			OutValue = static_cast<uint8>(OwnerPS->CharacterState);
			return true;
		}
		if (StateName == FName(TEXT("Self.PS.MySkill")))
		{
			OutValue = static_cast<uint8>(OwnerPS->MySkill);
			return true;
		}
		if (StateName == FName(TEXT("Self.PS.Attack")))
		{
			OutValue = static_cast<uint8>(OwnerPS->Attack);
			return true;
		}
		return false;
	}

	// 属性名前缀对应的宿主（设计 2.7.3）：表挂在哪个宿主上，它带来的字段就带哪个前缀
	enum class EAuthorityTableHost : uint8
	{
		None,
		SelfPS,			// Self.PS.*
		SelfCharacter,	// Self.Char.*
		EnemyPS,		// Enemy.PS.*
	};

	// 从带前缀的属性名解析宿主。前缀不认识时返回 None —— 位置类（Self.Move.*）也落在这里，
	// 因为它没有权威值表（设计 2.7.3 / 3.4.3）
	EAuthorityTableHost ParseAttributeHost(const FString& AttributeName)
	{
		if (AttributeName.StartsWith(TEXT("Self.PS.")))
		{
			return EAuthorityTableHost::SelfPS;
		}
		if (AttributeName.StartsWith(TEXT("Self.Char.")))
		{
			return EAuthorityTableHost::SelfCharacter;
		}
		if (AttributeName.StartsWith(TEXT("Enemy.PS.")))
		{
			return EAuthorityTableHost::EnemyPS;
		}
		return EAuthorityTableHost::None;
	}

	// 权威值表内的全部字段名（设计 3.3.1 的字段表 × 设计 2.7.3 的命名前缀）。
	// 只有这里列出的属性名才等得到权威值：标记一个表里没有的名字，这个标记永远不会被表清除，
	// 本地值也会被采用规则第一行永久挡住。表结构增删字段时这里必须同步
	//（另一处需要同步的是 ApplyAuthorityValueTableInternal 的逐字段写回）。
	bool IsTableBackedAttributeName(FName AttributeName)
	{
		static const FName TableBackedNames[] =
		{
			// AC_PlayerState 段（含敌方）
			FName(TEXT("Self.PS.HealthValue")),
			FName(TEXT("Self.PS.Chakra")),
			FName(TEXT("Self.PS.Attack")),
			FName(TEXT("Self.PS.MySkill")),
			FName(TEXT("Self.PS.CharacterState")),

			// AC_Character 段
			FName(TEXT("Self.Char.Toward")),
			FName(TEXT("Self.Char.LastEscapeTime")),
			FName(TEXT("Self.Char.LastFirstSkillTime")),
			FName(TEXT("Self.Char.LastSecondSkillTime")),
			FName(TEXT("Self.Char.LastScrollTime")),
			FName(TEXT("Self.Char.LastSummonTime")),

			// 敌方本地代理（只有 PlayerState 挂表）
			FName(TEXT("Enemy.PS.HealthValue")),
			FName(TEXT("Enemy.PS.Chakra")),
			FName(TEXT("Enemy.PS.Attack")),
			FName(TEXT("Enemy.PS.MySkill")),
			FName(TEXT("Enemy.PS.CharacterState")),
		};

		for (const FName& Name : TableBackedNames)
		{
			if (AttributeName == Name)
			{
				return true;
			}
		}
		return false;
	}

	// 采用规则（设计 2.7.2，表到达时逐字段判断）：
	//   无标记         → 写回本地属性（采用权威值）
	//   有标记、值不等 → 只更新权威值表：本地值不动、标记不清（这一格可能是 RPC 尚在途的旧值）
	//   有标记、值相等 → 只更新权威值表 + 清标记（服务器已追平）
	// 注意"只更新权威值表"那一行不需要写任何东西：表就在权威值表组件上，复制已经把它更新了（设计 3.3），
	// 本组件手上的本地属性才是要被决定的那一份。
	// 命中行号按计划 Prediction.Log 的要求记 Verbose。
	template <typename TAuthorityValue, typename TLocalValue>
	void ApplyAdoptionRule(TMap<FName, uint32>& Markers, const FName& AttributeName,
		TAuthorityValue AuthorityValue, TLocalValue& LocalValue)
	{
		// CharacterState 在表里是 uint8（设计 3.3.1：用 uint8 避免头文件循环包含）、在 PS 上是枚举，
		// 比较与写回都换算到本地类型再做
		const TLocalValue AuthorityAsLocal = static_cast<TLocalValue>(AuthorityValue);

		if (const uint32* OwningKeyID = Markers.Find(AttributeName))
		{
			if (LocalValue == AuthorityAsLocal)
			{
				// 第三行：服务器已追平 —— 清标记，不动本地值
				const uint32 OwningKey = *OwningKeyID;
				Markers.Remove(AttributeName);
				UE_LOG(LogPrediction, Verbose, TEXT("Adoption rule (row 3): %s caught up with key %u; marker cleared"),
					*AttributeName.ToString(), OwningKey);
				return;
			}

			// 第一行：本机正在预测它、键尚未结算 —— 本地值保留，等回执
			UE_LOG(LogPrediction, Verbose, TEXT("Adoption rule (row 1): %s still predicted by key %u; local value kept"),
				*AttributeName.ToString(), *OwningKeyID);
			return;
		}

		// 第二行：无标记 —— 写回本地属性
		LocalValue = AuthorityAsLocal;
		UE_LOG(LogPrediction, Verbose, TEXT("Adoption rule (row 2): %s adopted the authoritative value"), *AttributeName.ToString());
	}
}

// Sets default values for this component's properties
UC_PredictionComponent::UC_PredictionComponent()
{
	// 组件不单独开启 Tick（设计 3.2）：超时检查由 AC_Character::Tick 调用 TickPredictionTimeout 完成
	PrimaryComponentTick.bCanEverTick = false;
}

// ---- 生命周期接口（设计 3.4.1）----

bool UC_PredictionComponent::InitializePredictionContext()
{
	// 组件挂在角色上（设计 3.2）——换个宿主就不是预测组件该待的地方
	SelfCharacter = Cast<AC_Character>(GetOwner());
	if (SelfCharacter == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("InitializePredictionContext: the prediction component is not owned by an AC_Character"));
		return false;
	}

	// 角色 / PlayerState / Controller 三者缺一即上下文无效（设计 3.4.1）。
	// 客户端上 PlayerState 与 Controller 都是随后到达的，所以失败是正常中间态而不是错误 ——
	// 调用方在它们到达后再调一次即可（设计 3.4.1 的"可再次调用重试"）
	SelfPlayerState = SelfCharacter->GetPlayerState<AC_PlayerState>();
	SelfController = Cast<AC_PlayerController>(SelfCharacter->GetController());
	if (SelfPlayerState == nullptr || SelfController == nullptr)
	{
		UE_LOG(LogPrediction, Verbose, TEXT("InitializePredictionContext: PlayerState or PlayerController is not available yet; call again once it arrives"));
		return false;
	}

	// 己方两个宿主（设计 2.7.5：PS 上一个、Character 上一个）。重绑即覆盖，可安全重复调用
	BindAuthorityValueHost(SelfPlayerState.Get(), SelfPSAuthority);
	BindAuthorityValueHost(SelfCharacter.Get(), SelfCharacterAuthority);

	// 敌方宿主：按 Team 从 GameState->PlayerArray 里找（与本项目现有的找对手写法一致）。
	// 己方 Team 还没确定时不找 —— 那时"Team != 自己"对所有人都成立，会把同队的人当成敌人
	EnemyPlayerState = nullptr;
	if (SelfPlayerState->Team != ETeamType::None)
	{
		if (AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState<AGameStateBase>() : nullptr)
		{
			for (APlayerState* OtherPS : GameState->PlayerArray)
			{
				AC_PlayerState* OtherMyPS = Cast<AC_PlayerState>(OtherPS);
				if (OtherMyPS != nullptr && OtherMyPS->Team != ETeamType::None && OtherMyPS->Team != SelfPlayerState->Team)
				{
					EnemyPlayerState = OtherMyPS;
					break;
				}
			}
		}
	}
	BindAuthorityValueHost(EnemyPlayerState.Get(), EnemyPSAuthority);

	// 敌方缺失不是失败（设计 3.4.1 的三项里没有它）：对手可能还没生成/还没同步到达，
	// 重试时自然补上。但要区分两种"没有"：对手本身不在（常态，Verbose）vs 对手在、组件却不在（配错了，Warning）
	if (EnemyPSAuthority == nullptr)
	{
		if (EnemyPlayerState == nullptr)
		{
			UE_LOG(LogPrediction, Verbose, TEXT("InitializePredictionContext: no enemy PlayerState yet; Enemy.PS.* attributes cannot be marked until the opponent shows up"));
		}
		else
		{
			UE_LOG(LogPrediction, Warning, TEXT("InitializePredictionContext: the enemy PlayerState has no UC_AuthorityValueComponent; Enemy.PS.* attributes cannot be predicted"));
		}
	}

	UE_LOG(LogPrediction, Verbose, TEXT("Prediction context initialized for %s"), *SelfCharacter->GetName());

	return true;
}

bool UC_PredictionComponent::CanPredict() const
{
	// 只判断上下文是否有效（设计 3.4.1），明确不判断 IsLocallyControlled —— 那个判断在调用方输入函数入口做。
	// 服务器上这三个引用同样齐备，所以服务器上它同样返回 true；服务器不先行靠的是"服务器侧没有建键调用"（设计 3.2）
	return SelfCharacter.Get() != nullptr && SelfPlayerState.Get() != nullptr && SelfController.Get() != nullptr;
}

uint32 UC_PredictionComponent::GetActivePredictionKeyID() const
{
	// 这个成员本身就是"无活跃键时为 0"：只有 CreatePredictionKey 置位，EndPredictionKey 与
	// FinishPredictionKey 都会归零（设计 3.4.1 / W1.2 / W1.7），不需要再查一次键表
	return ActivePredictionKeyID;
}

FPredictionKey UC_PredictionComponent::GetActivePredictionKey() const
{
	// 无活跃键（键 ID 为 0，或键已不在表里）时返回默认键，KeyID = 0 即"无效键"（设计 3.3.1 / 3.4.1）
	if (const FPredictionKey* Key = PredictionKeys.Find(ActivePredictionKeyID))
	{
		return *Key;
	}
	return FPredictionKey();
}

// ---- 预测键接口（设计 3.4.2）----

uint32 UC_PredictionComponent::CreatePredictionKey(uint8 PredictionType)
{
	// 同一时刻只允许一个活跃（未冻结）预测键（设计 2.3.2）：需要开新键时，由状态预测先冻结旧键
	if (ActivePredictionKeyID != 0)
	{
		ensureMsgf(false, TEXT("CreatePredictionKey: an active prediction key %u already exists; freeze it with EndPredictionKey first"), ActivePredictionKeyID);
		return 0;
	}

	// 静态自增 uint32 计数器，保证同一客户端内唯一；0 保留为无效键（设计 2.3）
	static uint32 NextKeyID = 0;
	++NextKeyID;
	if (NextKeyID == 0)
	{
		NextKeyID = 1;
	}
	const uint32 KeyID = NextKeyID;

	const float Now = GetPredictionTimeSeconds();

	FPredictionKey NewKey;
	NewKey.KeyID = KeyID;
	NewKey.PredictionType = PredictionType;
	NewKey.ClientRequestTime = Now;	// 仅用于调试与超时管理，不参与确认/拒绝判定
	PredictionKeys.Add(KeyID, NewKey);

	FPredictionRecord NewRecord;
	NewRecord.KeyID = KeyID;
	NewRecord.StartTime = Now;
	PredictionRecords.Add(KeyID, NewRecord);

	ActivePredictionKeyID = KeyID;

	EvictOldResolvedRecords();

	UE_LOG(LogPrediction, Verbose, TEXT("Created prediction key %u (type %u)"), KeyID, PredictionType);

	return KeyID;
}

void UC_PredictionComponent::EndPredictionKey(uint32 KeyID)
{
	FPredictionRecord* Record = FindPredictionRecord(KeyID);
	if (Record == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("EndPredictionKey: prediction key %u not found; ignored"), KeyID);
		return;
	}

	// 已冻结或已结算：幂等返回，不重复入池
	if (Record->bFrozen || Record->bResolved)
	{
		return;
	}

	// 只冻结，不结算；结算由 ResolvePrediction 执行（设计 2.3.1）
	Record->bFrozen = true;
	Record->EndTime = GetPredictionTimeSeconds();
	PendingKeys.Add(KeyID);

	if (ActivePredictionKeyID == KeyID)
	{
		ActivePredictionKeyID = 0;
	}

	UE_LOG(LogPrediction, Verbose, TEXT("Frozen prediction key %u; added to the unresolved pool"), KeyID);
}

void UC_PredictionComponent::BindStateLifecycle(uint32 KeyID, FName StateName, uint8 EndStateValue)
{
	FPredictionRecord* Record = FindPredictionRecord(KeyID);
	if (Record == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("BindStateLifecycle: prediction key %u not found; ignored"), KeyID);
		return;
	}

	if (Record->bFrozen || Record->bResolved)
	{
		UE_LOG(LogPrediction, Warning, TEXT("BindStateLifecycle: prediction key %u is frozen or resolved; ignored"), KeyID);
		return;
	}

	// StateName 必须落在 AC_PlayerState 的可预测属性上（设计 3.4.2）
	if (!IsSupportedLifecycleStateName(StateName))
	{
		UE_LOG(LogPrediction, Warning, TEXT("BindStateLifecycle: StateName %s is not on AC_PlayerState; ignored"), *StateName.ToString());
		return;
	}

	// EndStateValue 为 uint8，上面三个属性都是整数型，不存在类型不匹配的情形
	FStateLifecycleBinding Binding;
	Binding.StateName = StateName;
	Binding.EndStateValue = EndStateValue;
	Record->LifecycleBindings.Add(Binding);
}

FPredictionRecord* UC_PredictionComponent::FindPredictionRecord(uint32 KeyID)
{
	return PredictionRecords.Find(KeyID);
}

const FPredictionRecord* UC_PredictionComponent::FindPredictionRecord(uint32 KeyID) const
{
	return PredictionRecords.Find(KeyID);
}

bool UC_PredictionComponent::IsPredictionKeyActive(uint32 KeyID) const
{
	const FPredictionRecord* Record = FindPredictionRecord(KeyID);
	return Record != nullptr && !Record->bFrozen && !Record->bResolved;
}

// ---- 预测标记接口（设计 3.4.3 / 2.7.2）----

void UC_PredictionComponent::MarkReplicatedAttribute(FName AttributeName, uint32 KeyID)
{
	if (KeyID == 0 || AttributeName.IsNone())
	{
		UE_LOG(LogPrediction, Warning, TEXT("MarkReplicatedAttribute: invalid argument (KeyID %u, AttributeName %s); ignored"),
			KeyID, *AttributeName.ToString());
		return;
	}

	FPredictionRecord* Record = FindPredictionRecord(KeyID);
	if (Record == nullptr || Record->bResolved)
	{
		// 键已结算：结算不会再走到这个键，标记进了表就没人清除（设计 3.3 的标记只由表到达或结算清除），
		// 而记录本身已可被淘汰——标记会留在表里指向一个不存在的键。故拒绝。
		// 冻结但未结算的键照常接受：它仍在缓冲池里、仍会回滚（设计 2.3.1），
		// 而设计 3.4.3 只给了两个忽略条件（KeyID == 0 / 名字为空），没有"冻结后不接受标记"这一条。
		UE_LOG(LogPrediction, Warning, TEXT("MarkReplicatedAttribute: prediction key %u not found or already resolved; ignored"), KeyID);
		return;
	}

	// 首次标记时确保该属性的权威值表已存在（设计 3.4.3）：名字必须是表内字段、宿主组件必须已绑定。
	// 两者缺一，这个标记都等不到权威值，本地值会被采用规则第一行永久挡住
	if (ResolveAuthorityComponentForAttribute(AttributeName) == nullptr)
	{
		return;	// ResolveAuthorityComponentForAttribute 已记录 Warning
	}

	// 接管规则（设计 2.7.2）：该属性已有标记时，标记易主到新键，并从旧键记录里移除该属性名
	if (const uint32* PreviousKeyID = PredictionMarkers.Find(AttributeName))
	{
		if (*PreviousKeyID != KeyID)
		{
			const uint32 PreviousKey = *PreviousKeyID;
			if (FPredictionRecord* PreviousRecord = FindPredictionRecord(PreviousKey))
			{
				PreviousRecord->ReplicatedAttributes.Remove(AttributeName);
			}
			PredictionMarkers.Add(AttributeName, KeyID);
			Record->ReplicatedAttributes.AddUnique(AttributeName);

			UE_LOG(LogPrediction, Warning, TEXT("MarkReplicatedAttribute: %s was predicted by key %u; taken over by key %u"),
				*AttributeName.ToString(), PreviousKey, KeyID);
			return;
		}

		// 同一个键重复标记同一属性：标记已经是本键的，保证记录里有即可，不算接管
		Record->ReplicatedAttributes.AddUnique(AttributeName);
		return;
	}

	PredictionMarkers.Add(AttributeName, KeyID);
	Record->ReplicatedAttributes.AddUnique(AttributeName);

	UE_LOG(LogPrediction, Verbose, TEXT("Marked %s as predicted by key %u"), *AttributeName.ToString(), KeyID);
}

void UC_PredictionComponent::ApplyAuthorityValueTable(const FAuthorityValueTable& Table)
{
	ApplyAuthorityValueTableInternal(Table);
}

bool UC_PredictionComponent::IsReplicatedAttributePredicted(FName AttributeName) const
{
	return PredictionMarkers.Contains(AttributeName);
}

// ---- 位置类预测：一次性基线（设计 2.5.1-C / 2.9）----

bool UC_PredictionComponent::RecordMoveBaseline()
{
	// 位移只能挂在活跃预测键上（设计 2.9 的记录方式表：预测键创建后、执行位移前捕获）
	if (ActivePredictionKeyID == 0)
	{
		// 返回 false：调用方据此决定是否仍执行位移（设计 3.4.3）。设计 2.12 的降级原则同样适用于此——
		// 这里不替调用方决定，只报告"没有基线兜底"
		UE_LOG(LogPrediction, Warning, TEXT("RecordMoveBaseline: no active prediction key; ignored"));
		return false;
	}

	FPredictionRecord* Record = FindPredictionRecord(ActivePredictionKeyID);
	if (Record == nullptr || Record->bFrozen || Record->bResolved)
	{
		// 活跃键按定义既未冻结也未结算（W1.2 的 EndPredictionKey 会清 ActivePredictionKeyID）；
		// 这里是防御——真走到说明有别的路径结算了活跃键
		UE_LOG(LogPrediction, Warning, TEXT("RecordMoveBaseline: prediction key %u is not active; ignored"), ActivePredictionKeyID);
		return false;
	}

	if (Record->bHasMoveBaseline)
	{
		// 一个键只发生一次位移，多段位移应拆成多个键（设计 2.9 / 3.4.3）。已经捕获过时**不改写基线**：
		// 回滚要恢复的是本键位移**之前**的位置，也就是第一次捕获的那一个；改写会让第一段位移留在原地。
		// 仍返回 true 让调用方照常执行位移——这次位移同样被那条基线覆盖，回滚依然精确
		UE_LOG(LogPrediction, Warning, TEXT("RecordMoveBaseline: prediction key %u already captured a baseline; capture ignored (split multi-segment displacement into separate keys)"),
			ActivePredictionKeyID);
		return true;
	}

	// 位置从组件宿主取：预测组件就挂在角色上（设计 3.2）。基线与恢复都是世界坐标
	//（设计 2.9：回滚用 SetActorLocation(MoveBaseline, false, nullptr, ETeleportType::TeleportPhysics)）
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("RecordMoveBaseline: the prediction component has no owner; ignored"));
		return false;
	}

	Record->MoveBaseline = Owner->GetActorLocation();
	Record->bHasMoveBaseline = true;

	UE_LOG(LogPrediction, Verbose, TEXT("Captured move baseline %s for key %u"),
		*Record->MoveBaseline.ToCompactString(), ActivePredictionKeyID);

	return true;
}

// ---- 非复制数据记录接口（设计 3.4.4 / 3.5）----

void UC_PredictionComponent::RecordStateChange(FName StateName, const FString& OldValue, const FString& NewValue,
	float Timestamp, FConfirmDelegate ConfirmDelegate, FRollbackDelegate RollbackDelegate)
{
	if (StateName.IsNone())
	{
		// 与 MarkReplicatedAttribute 的空名字规则一致（设计 3.4.3 给了那一条，这里沿用同一约定）
		UE_LOG(LogPrediction, Warning, TEXT("RecordStateChange: empty StateName; ignored"));
		return;
	}

	if (ActivePredictionKeyID == 0)
	{
		// 所有变化记录必须挂载到当前活跃预测键（设计 3.4.4 约定）
		UE_LOG(LogPrediction, Warning, TEXT("RecordStateChange: no active prediction key; ignored (%s)"), *StateName.ToString());
		return;
	}

	FPredictionRecord* Record = FindPredictionRecord(ActivePredictionKeyID);
	if (Record == nullptr || Record->bFrozen || Record->bResolved)
	{
		// 冻结后忽略并记录 Warning（设计 3.4.4 约定）。冻结键仍在缓冲池里等结算，
		// 但它已经"停止接受新的变化记录"（设计 2.3.1）——记录进去也不会被回滚覆盖
		UE_LOG(LogPrediction, Warning, TEXT("RecordStateChange: prediction key %u is frozen or resolved; ignored (%s)"),
			ActivePredictionKeyID, *StateName.ToString());
		return;
	}

	// 变化记录只针对非复制数据（设计 3.4.4 约定）：可复制属性该走预测标记 + 权威值表。
	// 这里只提醒、不拒绝 —— 记录进来仍会被回滚委托正确恢复，丢掉反而会少一次恢复
	if (IsTableBackedAttributeName(StateName))
	{
		UE_LOG(LogPrediction, Warning, TEXT("RecordStateChange: %s is a replicated attribute backed by the authority value table; it should use MarkReplicatedAttribute instead (recorded anyway)"),
			*StateName.ToString());
	}

	FStateChangeRecord ChangeRecord;
	ChangeRecord.StateName = StateName;
	ChangeRecord.OldValue = OldValue;
	ChangeRecord.NewValue = NewValue;
	ChangeRecord.Timestamp = Timestamp;	// 时间戳由调用方给（世界时间或服务器世界时间估算值，设计 3.4.4）
	Record->StateChanges.Add(ChangeRecord);

	AppendDelegates(Record->KeyID, ConfirmDelegate, RollbackDelegate);

	UE_LOG(LogPrediction, Verbose, TEXT("Recorded state change %s (%s -> %s) on key %u"),
		*StateName.ToString(), *OldValue, *NewValue, Record->KeyID);
}

void UC_PredictionComponent::RecordPresentation(FName PresentationName, FConfirmDelegate ConfirmDelegate, FRollbackDelegate RollbackDelegate)
{
	if (PresentationName.IsNone())
	{
		UE_LOG(LogPrediction, Warning, TEXT("RecordPresentation: empty PresentationName; ignored"));
		return;
	}

	if (ActivePredictionKeyID == 0)
	{
		UE_LOG(LogPrediction, Warning, TEXT("RecordPresentation: no active prediction key; ignored (%s)"), *PresentationName.ToString());
		return;
	}

	FPredictionRecord* Record = FindPredictionRecord(ActivePredictionKeyID);
	if (Record == nullptr || Record->bFrozen || Record->bResolved)
	{
		UE_LOG(LogPrediction, Warning, TEXT("RecordPresentation: prediction key %u is frozen or resolved; ignored (%s)"),
			ActivePredictionKeyID, *PresentationName.ToString());
		return;
	}

	FPresentationRecord PresentationRecord;
	PresentationRecord.PresentationName = PresentationName;
	Record->Presentations.Add(PresentationRecord);

	AppendDelegates(Record->KeyID, ConfirmDelegate, RollbackDelegate);

	UE_LOG(LogPrediction, Verbose, TEXT("Recorded presentation %s on key %u"), *PresentationName.ToString(), Record->KeyID);
}

const TArray<FStateChangeRecord>& UC_PredictionComponent::GetStateChangeRecords(uint32 KeyID) const
{
	// 键不存在时返回空数组，调用方不必判空；已结算的键仍能查到记录（保留供调试，设计 3.4.5）
	static const TArray<FStateChangeRecord> EmptyRecords;
	const FPredictionRecord* Record = FindPredictionRecord(KeyID);
	return Record != nullptr ? Record->StateChanges : EmptyRecords;
}

const TArray<FPresentationRecord>& UC_PredictionComponent::GetPresentationRecords(uint32 KeyID) const
{
	static const TArray<FPresentationRecord> EmptyRecords;
	const FPredictionRecord* Record = FindPredictionRecord(KeyID);
	return Record != nullptr ? Record->Presentations : EmptyRecords;
}

void UC_PredictionComponent::AppendDelegates(uint32 KeyID, const FConfirmDelegate& ConfirmDelegate, const FRollbackDelegate& RollbackDelegate)
{
	// 委托不入变化记录，集中存进委托表（设计 3.5）；空委托不入表（"不需回滚的变化传空委托即可"）
	if (!ConfirmDelegate.IsBound() && !RollbackDelegate.IsBound())
	{
		return;
	}

	FPredictionDelegates& Delegates = PredictionDelegates.FindOrAdd(KeyID);
	if (ConfirmDelegate.IsBound())
	{
		Delegates.ConfirmDelegates.Add(ConfirmDelegate);
	}
	if (RollbackDelegate.IsBound())
	{
		Delegates.RollbackDelegates.Add(RollbackDelegate);
	}
}

// ---- 结算接口（设计 3.4.5 / 2.11）----

void UC_PredictionComponent::ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked)
{
	// 键不在键表里：迟到回执（超时回滚已把它移出键表）、重复回执，或根本不是本机的键。
	// 三者同属正常竞态，【静默返回】，不记 Warning（设计 3.4.5 约定）
	if (!PredictionKeys.Contains(KeyID))
	{
		return;
	}

	FPredictionRecord* Record = FindPredictionRecord(KeyID);
	if (Record == nullptr || Record->bResolved)
	{
		// 与上一条同源：已结算就不再结算。记录本身结算后仍留在记录表里（供调试），
		// 所以这里不能只看键表 —— 键表的条目在 FinishPredictionKey 里才移除
		return;
	}

	// 只有两种结果，其他值按 Rejected 处理并记录 Warning（设计 3.4.5）
	if (Result != 0 && Result != 1)
	{
		UE_LOG(LogPrediction, Warning, TEXT("ResolvePrediction: key %u got an unknown result %u; treated as rejected"), KeyID, Result);
	}

	const bool bConfirmed = (Result == 0);
	if (bConfirmed)
	{
		ConfirmPrediction(KeyID, ConfirmedStatePacked);
	}
	else
	{
		RollbackPrediction(KeyID);
	}

	// 整体确认 / 整体回滚：不做部分回滚（设计 2.11.3 约定），收尾对两条路径相同
	FinishPredictionKey(KeyID, bConfirmed);

	UE_LOG(LogPrediction, Verbose, TEXT("Resolved prediction key %u as %s"), KeyID, bConfirmed ? TEXT("confirmed") : TEXT("rejected"));
}

void UC_PredictionComponent::OnMulticastArrived(FName MulticastName)
{
	if (MulticastName.IsNone())
	{
		return;
	}

	// 按名字清标记（设计 2.11.3 第 6 条）：语义是"该数据的权威值已到，回滚时跳过它"。
	// 标记表保证一个属性名同一时刻只属于一个键（设计 2.7.2 接管规则），故按名字清除无歧义。
	// 找不到对应标记时【静默返回】—— 多数多播与本地预测无关，属常态（设计 3.4.5 约定）。
	// 注意：配的是标记表里的名字（带前缀的属性名），不是多播自己的名字
	if (const uint32* OwningKeyID = PredictionMarkers.Find(MulticastName))
	{
		const uint32 OwningKey = *OwningKeyID;	// Remove 会让上面的指针失效，先取值
		PredictionMarkers.Remove(MulticastName);

		UE_LOG(LogPrediction, Verbose, TEXT("Multicast %s arrived; the prediction marker of key %u was cleared"),
			*MulticastName.ToString(), OwningKey);
	}
}

void UC_PredictionComponent::TickPredictionTimeout(float DeltaTime)
{
	// 超时判定必须用世界时间与记录 StartTime 比较（设计 2.3.1 实现约束）：玩家被时停时
	// （CustomTimeDilation = 0）DeltaSeconds 为 0，累加式计时会让其预测永不超时。
	// 参数保留只是为了和 Tick 的调用形式一致（设计 3.4.5 给的签名）
	(void)DeltaTime;

	// 一、生命周期绑定（设计 2.3.2 的兜底路径）：绑定的状态变量到达 EndStateValue 即冻结本键。
	// 三个 StateName 都落在本机 PlayerState 上，直接从宿主 Pawn 取引用（APawn::PlayerState 无复制条件，
	// 客户端同样有），不依赖 W1.8 的上下文缓存 —— 计划 1.2 约定包与包之间只有编译期依赖。
	// PlayerState 尚未同步到达时本轮跳过，下一帧再试（设计 3.4.1）
	const AC_Character* OwnerCharacter = Cast<AC_Character>(GetOwner());
	const AC_PlayerState* OwnerPS = OwnerCharacter ? OwnerCharacter->GetPlayerState<AC_PlayerState>() : nullptr;

	// 冻结要写记录、改缓冲池，不能在遍历记录表时进行，先收集要冻结的键
	TArray<uint32> KeysToFreeze;
	for (const TPair<uint32, FPredictionRecord>& Pair : PredictionRecords)
	{
		const FPredictionRecord& Record = Pair.Value;
		if (Record.bResolved)
		{
			continue;
		}

		// 一个键可以绑多个 StateName，任一满足即冻结（设计 3.4.2 多绑定）
		for (const FStateLifecycleBinding& Binding : Record.LifecycleBindings)
		{
			uint8 StateValue = 0;
			if (ReadLifecycleStateValue(Binding.StateName, OwnerPS, StateValue) && StateValue == Binding.EndStateValue)
			{
				KeysToFreeze.Add(Pair.Key);
				break;
			}
		}
	}

	for (uint32 KeyID : KeysToFreeze)
	{
		// 只冻结、不结算（设计 2.3.1）；已冻结的键在这里是幂等空转
		EndPredictionKey(KeyID);
	}

	// 二、超时兜底（设计 2.3.1 / 3.4.5）：覆盖所有 !bResolved 的记录，**含已冻结键**
	const float Now = GetPredictionTimeSeconds();

	TArray<uint32> TimedOutKeys;
	for (const TPair<uint32, FPredictionRecord>& Pair : PredictionRecords)
	{
		const FPredictionRecord& Record = Pair.Value;
		if (!Record.bResolved && (Now - Record.StartTime) > PredictionTimeout)
		{
			TimedOutKeys.Add(Pair.Key);
		}
	}

	for (uint32 KeyID : TimedOutKeys)
	{
		const FPredictionRecord* Record = FindPredictionRecord(KeyID);
		const float Elapsed = Record ? (Now - Record->StartTime) : 0.f;

		// 超时按 Rejected 处理，回滚内容与该路径完全一致（设计 3.4.5）。记 Warning：回执丢失本该是罕见事件
		UE_LOG(LogPrediction, Warning, TEXT("Prediction key %u timed out after %.2fs; rolling back"), KeyID, Elapsed);

		RollbackPrediction(KeyID);
		FinishPredictionKey(KeyID, /*bConfirmed*/ false);
	}
}

bool UC_PredictionComponent::GetAuthorityValue(FName AttributeName, FString& OutValue) const
{
	// 名字必须是表内字段、且承载它的权威值表组件已绑定 —— 两道把关都由 ResolveAuthorityComponentForAttribute
	// 完成（它已记 Warning），本函数不再重复判断
	const UC_AuthorityValueComponent* Source = ResolveAuthorityComponentForAttribute(AttributeName);
	if (Source == nullptr)
	{
		return false;
	}

	// 表是带类型字段的结构体（设计 2.7.5），取字符串只为调试展示（设计 3.4.5），不参与任何判定
	const FAuthorityValueTable& Table = Source->AuthorityValueTable;
	const FString Name = AttributeName.ToString();

	// PS 段：己方与敌方的字段完全相同、只有前缀不同（设计 2.7.3 / 2.7.5），而宿主已由名字定死，故两种前缀合判
	if (Name == TEXT("Self.PS.HealthValue") || Name == TEXT("Enemy.PS.HealthValue"))
	{
		OutValue = FString::SanitizeFloat(Table.HealthValue);
		return true;
	}
	if (Name == TEXT("Self.PS.Chakra") || Name == TEXT("Enemy.PS.Chakra"))
	{
		OutValue = FString::FromInt(Table.Chakra);
		return true;
	}
	if (Name == TEXT("Self.PS.Attack") || Name == TEXT("Enemy.PS.Attack"))
	{
		OutValue = FString::FromInt(Table.Attack);
		return true;
	}
	if (Name == TEXT("Self.PS.MySkill") || Name == TEXT("Enemy.PS.MySkill"))
	{
		OutValue = FString::FromInt(Table.MySkill);
		return true;
	}
	if (Name == TEXT("Self.PS.CharacterState") || Name == TEXT("Enemy.PS.CharacterState"))
	{
		OutValue = FString::FromInt(static_cast<int32>(Table.CharacterState));
		return true;
	}

	// Character 段：只有己方角色挂表（设计 2.7.5），不存在敌方前缀
	if (Name == TEXT("Self.Char.Toward"))
	{
		OutValue = Table.Toward ? FString(TEXT("true")) : FString(TEXT("false"));
		return true;
	}
	if (Name == TEXT("Self.Char.LastEscapeTime"))
	{
		OutValue = FString::SanitizeFloat(Table.LastEscapeTime);
		return true;
	}
	if (Name == TEXT("Self.Char.LastFirstSkillTime"))
	{
		OutValue = FString::SanitizeFloat(Table.LastFirstSkillTime);
		return true;
	}
	if (Name == TEXT("Self.Char.LastSecondSkillTime"))
	{
		OutValue = FString::SanitizeFloat(Table.LastSecondSkillTime);
		return true;
	}
	if (Name == TEXT("Self.Char.LastScrollTime"))
	{
		OutValue = FString::SanitizeFloat(Table.LastScrollTime);
		return true;
	}
	if (Name == TEXT("Self.Char.LastSummonTime"))
	{
		OutValue = FString::SanitizeFloat(Table.LastSummonTime);
		return true;
	}

	// 走到这里说明 IsTableBackedAttributeName 认了这个名字、这里却没有分支：表结构与本函数不同步
	UE_LOG(LogPrediction, Warning, TEXT("GetAuthorityValue: %s is listed as a table-backed attribute but has no read branch; the field list and this function are out of sync"),
		*Name);
	return false;
}

// ---- 内部 ----

float UC_PredictionComponent::GetPredictionTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

void UC_PredictionComponent::EvictOldResolvedRecords()
{
	if (MaxPredictionRecords <= 0 || PredictionRecords.Num() <= MaxPredictionRecords)
	{
		return;
	}

	// 只淘汰已结算记录（设计 3.3）；未结算记录不得淘汰
	TArray<uint32> ResolvedIDs;
	ResolvedIDs.Reserve(PredictionRecords.Num());
	for (const TPair<uint32, FPredictionRecord>& Pair : PredictionRecords)
	{
		if (Pair.Value.bResolved)
		{
			ResolvedIDs.Add(Pair.Key);
		}
	}

	// 按时间淘汰最旧
	ResolvedIDs.Sort([this](uint32 A, uint32 B)
	{
		return PredictionRecords.FindChecked(A).StartTime < PredictionRecords.FindChecked(B).StartTime;
	});

	for (uint32 ID : ResolvedIDs)
	{
		if (PredictionRecords.Num() <= MaxPredictionRecords)
		{
			break;
		}
		PredictionRecords.Remove(ID);
	}
}

void UC_PredictionComponent::ApplyAuthorityValueTableInternal(const FAuthorityValueTable& Table)
{
	// 哪张表到达了？表就在权威值表组件里、客户端手上只有那一份（设计 2.7.2 / 3.3），
	// 所以"到达的表是哪个已绑定组件的成员"就是宿主的身份。三个宿主因此共用这一个入口。
	UC_AuthorityValueComponent* Source = nullptr;
	bool bFromEnemyPS = false;

	if (SelfPSAuthority.Get() != nullptr && &Table == &SelfPSAuthority->AuthorityValueTable)
	{
		Source = SelfPSAuthority.Get();
	}
	else if (SelfCharacterAuthority.Get() != nullptr && &Table == &SelfCharacterAuthority->AuthorityValueTable)
	{
		Source = SelfCharacterAuthority.Get();
	}
	else if (EnemyPSAuthority.Get() != nullptr && &Table == &EnemyPSAuthority->AuthorityValueTable)
	{
		Source = EnemyPSAuthority.Get();
		bFromEnemyPS = true;
	}
	else
	{
		UE_LOG(LogPrediction, Warning, TEXT("ApplyAuthorityValueTable: the table belongs to no bound authority value component; ignored"));
		return;
	}

	AActor* HostActor = Source->GetOwner();
	if (HostActor == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("ApplyAuthorityValueTable: the authority value component has no owner; ignored"));
		return;
	}

	// PS 段：己方与敌方的字段完全相同、前缀不同（设计 2.7.3 / 2.7.5），写回目标就是表所属的那个 PS
	if (AC_PlayerState* OwnerPS = Cast<AC_PlayerState>(HostActor))
	{
		if (bFromEnemyPS)
		{
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Enemy.PS.HealthValue")), Table.HealthValue, OwnerPS->HealthValue);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Enemy.PS.Chakra")), Table.Chakra, OwnerPS->Chakra);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Enemy.PS.Attack")), Table.Attack, OwnerPS->Attack);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Enemy.PS.MySkill")), Table.MySkill, OwnerPS->MySkill);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Enemy.PS.CharacterState")), Table.CharacterState, OwnerPS->CharacterState);
		}
		else
		{
			// 血量不在己方预测（设计 2.7.3：没有 Self.PS.HealthValue），这一格永远走第二行——
			// 这正是设计 5.2(b) 摘除血量逐属性复制之后，拥有者客户端拿到自身血量的通道，不能省
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.PS.HealthValue")), Table.HealthValue, OwnerPS->HealthValue);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.PS.Chakra")), Table.Chakra, OwnerPS->Chakra);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.PS.Attack")), Table.Attack, OwnerPS->Attack);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.PS.MySkill")), Table.MySkill, OwnerPS->MySkill);
			ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.PS.CharacterState")), Table.CharacterState, OwnerPS->CharacterState);
		}
		return;
	}

	// Character 段：只有己方角色挂表（设计 2.7.5），因此只可能是 Self.Char.*
	if (AC_Character* OwnerCharacter = Cast<AC_Character>(HostActor))
	{
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.Toward")), Table.Toward, OwnerCharacter->Toward);
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.LastEscapeTime")), Table.LastEscapeTime, OwnerCharacter->LastEscapeTime);
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.LastFirstSkillTime")), Table.LastFirstSkillTime, OwnerCharacter->LastFirstSkillTime);
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.LastSecondSkillTime")), Table.LastSecondSkillTime, OwnerCharacter->LastSecondSkillTime);
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.LastScrollTime")), Table.LastScrollTime, OwnerCharacter->LastScrollTime);
		ApplyAdoptionRule(PredictionMarkers, FName(TEXT("Self.Char.LastSummonTime")), Table.LastSummonTime, OwnerCharacter->LastSummonTime);
		return;
	}

	UE_LOG(LogPrediction, Warning, TEXT("ApplyAuthorityValueTable: the authority value component is owned by an unexpected actor class; ignored"));
}

void UC_PredictionComponent::BindAuthorityValueHost(const AActor* HostActor, TObjectPtr<UC_AuthorityValueComponent>& OutAuthority)
{
	OutAuthority = HostActor ? HostActor->FindComponentByClass<UC_AuthorityValueComponent>() : nullptr;
	if (OutAuthority == nullptr)
	{
		// 宿主不在、或组件还没挂上/还没同步到达：保持 nullptr，由 ResolveAuthorityComponentForAttribute
		// 在标记接口处拒绝该宿主的属性名（静默留空会让"标记等不到权威值"变成一个查不出来的死结）
		return;
	}

	// 直接绑本组件的成员函数：ApplyAuthorityValueTable 只有表、没有宿主身份，靠表的地址认宿主（已定事项 10），
	// 所以这里不能换成"接住表再转发"的 lambda —— 那样表的地址就变了。
	// TDelegate 是单绑定，重绑即覆盖（CreateDelegateInstance 会先析构旧的），重复调用不会累积；
	// 且 UObject 版绑定持的是弱引用，权威值表组件被销毁后不会悬空执行
	OutAuthority->OnAuthorityValueTableArrived.BindUObject(this, &UC_PredictionComponent::ApplyAuthorityValueTable);
}

UC_AuthorityValueComponent* UC_PredictionComponent::ResolveAuthorityComponentForAttribute(FName AttributeName) const
{
	const FString AttributeNameString = AttributeName.ToString();

	if (!IsTableBackedAttributeName(AttributeName))
	{
		// 没有权威值可等的两类名字：位置类（Self.Move.*，用 RecordMoveBaseline，设计 3.4.3）与
		// 只存在于本地的变量（如 Self.Char.LaunchState，用 RecordStateChange，设计 3.4.4）
		UE_LOG(LogPrediction, Warning, TEXT("%s is not backed by the authority value table (design 3.3.1 / 2.7.3); ignored"),
			*AttributeNameString);
		return nullptr;
	}

	UC_AuthorityValueComponent* Component = nullptr;
	switch (ParseAttributeHost(AttributeNameString))
	{
	case EAuthorityTableHost::SelfPS:		Component = SelfPSAuthority.Get();			break;
	case EAuthorityTableHost::SelfCharacter:	Component = SelfCharacterAuthority.Get();	break;
	case EAuthorityTableHost::EnemyPS:		Component = EnemyPSAuthority.Get();		break;
	default:					break;
	}

	if (Component == nullptr)
	{
		// 绑定失败时标记必须拒绝：表到达不了，标记就永远不会被清除，本地值会被永久挡住。
		// 绑定在 InitializePredictionContext 里做（可重试），绑定成功后调用方自然恢复
		UE_LOG(LogPrediction, Warning, TEXT("The authority value component carrying %s is not bound yet (see InitializePredictionContext); ignored"),
			*AttributeNameString);
		return nullptr;
	}

	return Component;
}

bool UC_PredictionComponent::ShouldSkipAttributeOnRollback(FName AttributeName, uint32 KeyID) const
{
	// 防护规则一（设计 2.7.2）：该属性已无标记 —— 权威值已到达并在 OnRep 中清除，不要用旧值覆盖权威值
	const uint32* OwningKeyID = PredictionMarkers.Find(AttributeName);
	if (OwningKeyID == nullptr)
	{
		return true;
	}

	// 防护规则二（设计 2.7.2）：标记已易主 —— 由新键负责
	return *OwningKeyID != KeyID;
}

void UC_PredictionComponent::ConfirmPrediction(uint32 KeyID, uint8 ConfirmedStatePacked)
{
	// 跟进委托按注册顺序执行（设计 3.5）："先状态、后表现"的次序由接入方在记录时保证，这里只保序执行
	if (const FPredictionDelegates* Delegates = PredictionDelegates.Find(KeyID))
	{
		for (const FConfirmDelegate& Delegate : Delegates->ConfirmDelegates)
		{
			Delegate.ExecuteIfBound();
		}
	}

	// 清除本键登记的可复制属性标记，**不写回本地值**（设计 2.7.2 结算表）：服务器的"接受"意味着它写下了
	// 与预测相同的值，本地值无需改动；之后到达的表会按无标记路径把它再写一次（幂等）
	if (const FPredictionRecord* Record = FindPredictionRecord(KeyID))
	{
		for (const FName& AttributeName : Record->ReplicatedAttributes)
		{
			// 标记已易主的属性不碰（设计 2.7.2 防护规则二）：它现在归新键
			if (const uint32* OwningKeyID = PredictionMarkers.Find(AttributeName))
			{
				if (*OwningKeyID == KeyID)
				{
					PredictionMarkers.Remove(AttributeName);
				}
			}
		}
	}

	// ConfirmedStatePacked（设计 2.11.1：高 4 位 CharacterState、低 4 位 MySkill）是给接入方校正本地
	// 非复制状态值用的；跟进委托按设计 3.5 是无参的，本组件不对它做任何消费，只记日志供调试
	UE_LOG(LogPrediction, Verbose, TEXT("Confirmed key %u (packed state %d: CharacterState %d, MySkill %d)"),
		KeyID, static_cast<int32>(ConfirmedStatePacked),
		static_cast<int32>(ConfirmedStatePacked >> 4), static_cast<int32>(ConfirmedStatePacked & 0x0F));
}

void UC_PredictionComponent::RollbackPrediction(uint32 KeyID)
{
	// 一、非复制数据（设计 3.4.5 / 3.5）：按注册顺序执行回滚委托，接入方据此按变化记录恢复旧值
	if (const FPredictionDelegates* Delegates = PredictionDelegates.Find(KeyID))
	{
		for (const FRollbackDelegate& Delegate : Delegates->RollbackDelegates)
		{
			Delegate.ExecuteIfBound();
		}
	}

	const FPredictionRecord* Record = FindPredictionRecord(KeyID);
	if (Record == nullptr)
	{
		return;
	}

	// 二、可复制属性（设计 2.7.2 结算表：Rejected / 超时 → 从权威值表写回本地并清标记）。
	// 逐项过两条防护规则：已无标记（权威值已到达并在 OnRep 里清除）→ 跳过，不要用旧值覆盖权威值；
	// 标记已易主 → 跳过，由新键负责
	for (const FName& AttributeName : Record->ReplicatedAttributes)
	{
		if (ShouldSkipAttributeOnRollback(AttributeName, KeyID))
		{
			continue;
		}

		RestoreFromAuthorityValue(AttributeName);
		PredictionMarkers.Remove(AttributeName);	// 上面已保证标记属于本键，清的就是本键这一格
	}

	// 三、位置（设计 3.4.5 / 2.11.2）：仅当本键捕获过基线。恢复用 TeleportPhysics、不用 sweep ——
	// 带 sweep 时会被地形或另一个角色挡住，导致恢复不到位；基线取自同一个宿主 Actor，坐标系一致
	if (Record->bHasMoveBaseline)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorLocation(Record->MoveBaseline, false, nullptr, ETeleportType::TeleportPhysics);

			UE_LOG(LogPrediction, Verbose, TEXT("Restored the position of key %u to %s"),
				KeyID, *Record->MoveBaseline.ToCompactString());
		}
		else
		{
			UE_LOG(LogPrediction, Warning, TEXT("RollbackPrediction: the prediction component has no owner; the move baseline of key %u was not restored"), KeyID);
		}
	}
}

void UC_PredictionComponent::RestoreFromAuthorityValue(FName AttributeName)
{
	UC_AuthorityValueComponent* Source = ResolveAuthorityComponentForAttribute(AttributeName);
	if (Source == nullptr)
	{
		return;	// 名字不在表内、或宿主未绑定 —— ResolveAuthorityComponentForAttribute 已记 Warning
	}

	AActor* HostActor = Source->GetOwner();
	if (HostActor == nullptr)
	{
		UE_LOG(LogPrediction, Warning, TEXT("RestoreFromAuthorityValue: the authority value component has no owner; %s was not restored"),
			*AttributeName.ToString());
		return;
	}

	// 与 ApplyAuthorityValueTableInternal 同理：写回目标由表的宿主决定，PS 段与 Character 段分开。
	// 这里不套采用规则 —— 调用方（RollbackPrediction）已用两条防护规则筛过，剩下的就是"无条件写回"
	const FAuthorityValueTable& Table = Source->AuthorityValueTable;

	if (AC_PlayerState* OwnerPS = Cast<AC_PlayerState>(HostActor))
	{
		// 己方与敌方 PS 的字段相同、只有前缀不同（设计 2.7.3 / 2.7.5），而宿主已由名字定死，故两种前缀合判
		if (AttributeName == FName(TEXT("Self.PS.HealthValue")) || AttributeName == FName(TEXT("Enemy.PS.HealthValue")))
		{
			OwnerPS->HealthValue = Table.HealthValue;
		}
		else if (AttributeName == FName(TEXT("Self.PS.Chakra")) || AttributeName == FName(TEXT("Enemy.PS.Chakra")))
		{
			OwnerPS->Chakra = Table.Chakra;
		}
		else if (AttributeName == FName(TEXT("Self.PS.Attack")) || AttributeName == FName(TEXT("Enemy.PS.Attack")))
		{
			OwnerPS->Attack = Table.Attack;
		}
		else if (AttributeName == FName(TEXT("Self.PS.MySkill")) || AttributeName == FName(TEXT("Enemy.PS.MySkill")))
		{
			OwnerPS->MySkill = Table.MySkill;
		}
		else if (AttributeName == FName(TEXT("Self.PS.CharacterState")) || AttributeName == FName(TEXT("Enemy.PS.CharacterState")))
		{
			OwnerPS->CharacterState = static_cast<ECharacterStateType>(Table.CharacterState);
		}
		else
		{
			UE_LOG(LogPrediction, Warning, TEXT("RestoreFromAuthorityValue: %s is a PlayerState-hosted attribute but has no restore branch; not restored"),
				*AttributeName.ToString());
			return;
		}

		UE_LOG(LogPrediction, Verbose, TEXT("Restored %s from the authority value table"), *AttributeName.ToString());
		return;
	}

	// Character 段：只有己方角色挂表（设计 2.7.5）。LastEscapeTime 与四个 CD 时间戳是 private，
	// 靠计划 1.3 的两行友元访问
	if (AC_Character* OwnerCharacter = Cast<AC_Character>(HostActor))
	{
		if (AttributeName == FName(TEXT("Self.Char.Toward")))
		{
			OwnerCharacter->Toward = Table.Toward;
		}
		else if (AttributeName == FName(TEXT("Self.Char.LastEscapeTime")))
		{
			OwnerCharacter->LastEscapeTime = Table.LastEscapeTime;
		}
		else if (AttributeName == FName(TEXT("Self.Char.LastFirstSkillTime")))
		{
			OwnerCharacter->LastFirstSkillTime = Table.LastFirstSkillTime;
		}
		else if (AttributeName == FName(TEXT("Self.Char.LastSecondSkillTime")))
		{
			OwnerCharacter->LastSecondSkillTime = Table.LastSecondSkillTime;
		}
		else if (AttributeName == FName(TEXT("Self.Char.LastScrollTime")))
		{
			OwnerCharacter->LastScrollTime = Table.LastScrollTime;
		}
		else if (AttributeName == FName(TEXT("Self.Char.LastSummonTime")))
		{
			OwnerCharacter->LastSummonTime = Table.LastSummonTime;
		}
		else
		{
			UE_LOG(LogPrediction, Warning, TEXT("RestoreFromAuthorityValue: %s is a Character-hosted attribute but has no restore branch; not restored"),
				*AttributeName.ToString());
			return;
		}

		UE_LOG(LogPrediction, Verbose, TEXT("Restored %s from the authority value table"), *AttributeName.ToString());
		return;
	}

	UE_LOG(LogPrediction, Warning, TEXT("RestoreFromAuthorityValue: the authority value component is owned by an unexpected actor class; %s was not restored"),
		*AttributeName.ToString());
}

void UC_PredictionComponent::FinishPredictionKey(uint32 KeyID, bool bConfirmed)
{
	// 记录保留（设计 3.4.5）：只标成已结算，不删 —— 供调试与历史查询。
	// 淘汰交给 EvictOldResolvedRecords，它只动已结算记录（设计 3.3）
	if (FPredictionRecord* Record = FindPredictionRecord(KeyID))
	{
		Record->bResolved = true;
		Record->bConfirmed = bConfirmed;
	}

	// 键出键表、出缓冲池（设计 2.3.2 的出池规则：结算后出池）
	PredictionKeys.Remove(KeyID);
	PendingKeys.Remove(KeyID);

	// 委托表清掉本键的条目：委托本轮已执行完。设计 3.5 只规定"结算时取出执行"，
	// 不留条目是必要的 —— 否则委托表会随预测次数无限增长，且里面存的是已失效的绑定
	PredictionDelegates.Remove(KeyID);

	// 活跃键被结算时归零：超时可能落在**活跃**键上（回执丢失、又没绑生命周期），
	// 那条路径不经过 EndPredictionKey，只能靠这里归零
	if (ActivePredictionKeyID == KeyID)
	{
		ActivePredictionKeyID = 0;
	}
}
