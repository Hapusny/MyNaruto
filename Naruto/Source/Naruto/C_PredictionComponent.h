// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HAL/IConsoleManager.h"		// TAutoConsoleVariable（下面的调试开关）
#include "C_AuthorityValueComponent.h"	// FAuthorityValueTable 定义在此，预测组件按设计 3.2 包含它
#include "C_PredictionComponent.generated.h"

class AC_Character;
class AC_PlayerState;
class AC_PlayerController;

// 预测系统的日志分类：组件与各接入点共用（阶段二 2.1 的 Prediction.Log 开关作用于此分类）
DECLARE_LOG_CATEGORY_EXTERN(LogPrediction, Log, All);

// ---- 调试开关（计划"工具与调试开关"表；阶段二 2.1 建）----
// 用 cvar 而不是组件属性：不改设计 3.7 冻结表里的任何签名，且能在 console 里逐片排查。
// 约定：每个开关的"关"都必须是安全态，等价于"未接入"（计划 0 节的"开关能退什么"）。
// 这三个长期保留，是以后线上排障的手段；逐片开关（Prediction.Skill / Attack / ...）随各切片再建。
extern TAutoConsoleVariable<int32> CVarPredictionEnabled;	// Prediction.Enabled：全局关闭，各接入点退回原有路径
extern TAutoConsoleVariable<int32> CVarPredictionLog;		// Prediction.Log：把 LogPrediction 抬到 Verbose
extern TAutoConsoleVariable<int32> CVarPredictionDraw;		// Prediction.Draw：屏上绘制键 / 标记 / 锁 / 表

// ---- 服务器侧调试开关（计划"工具与调试开关"表，阶段二 2.6 建）----
// 这两个只在【非 Shipping 构建】里编译（工具表的约定：开关的"关"必须是安全态，等价于"未接入"）。
// 读取一律走下面两个查询函数：Shipping 里恒为 false，调用点不必自己写条件编译。
// cvar 定义在 C_PredictionComponent.cpp 的开关区，C_PlayerController.cpp / C_Character.cpp 读
#if !UE_BUILD_SHIPPING
extern TAutoConsoleVariable<int32> CVarPredictionForceReject;	// Prediction.ForceReject：带键请求一律回 Rejected，且不写权威值
extern TAutoConsoleVariable<int32> CVarPredictionDropResolve;	// Prediction.DropResolve：不回执，验证客户端的超时兜底（默认 2.0s）
#endif
bool IsPredictionForceRejectEnabled();
bool IsPredictionDropResolveEnabled();

// 预测类型（设计 3.3.2）
UENUM(BlueprintType)
enum class EPredictionType : uint8
{
	None	UMETA(DisplayName = "None"),	// 无效预测类型
	Attack	UMETA(DisplayName = "Attack"),	// 普攻：对应 Self.PS.Attack 与连段状态；每段一个键（2.4.4）
	Skill	UMETA(DisplayName = "Skill"),	// 技能：对应 Self.PS.MySkill；霸体由动画通知写入、不进技能键（5.9a）
	Escape	UMETA(DisplayName = "Escape"),	// 替身：MySkill / Chakra / LastEscapeTime / 位置瞬移
	Scroll	UMETA(DisplayName = "Scroll"),	// 秘卷：与 Summon 同属 MySkill == 4 这一个状态，键判据相同，不产生两个键
	Summon	UMETA(DisplayName = "Summon")	// 通灵：同上；类型只记录输入来源（SummonIndex），不进键判据
};

// 轻量预测键，跨 RPC 传入服务器；对"非发起连接"自动失效（GAS 式，设计 3.3.1）
// 注意：成员必须带 UPROPERTY，否则不会被序列化，RPC 传过去全是默认值
USTRUCT()
struct FPredictionKey
{
	GENERATED_BODY()

	UPROPERTY() uint32 KeyID = 0;
	UPROPERTY() uint8 PredictionType = 0;		// EPredictionType
	UPROPERTY() float ClientRequestTime = 0.f;	// 仅用于调试与超时管理，不参与确认/拒绝判定

	// ---- GAS 式"只在发起客户端有效"（对照 GameplayPrediction.h / .cpp）----
	// 服务器在某个连接上收到本键时记下该连接的标识；之后把键写回任何连接时，
	// 只有标识匹配的连接能读到真实 KeyID，其他连接读到 KeyID = 0（无效键）。
	// 注意：非 UPROPERTY，由自定义 NetSerialize 手工处理（同 GAS）。
	UPTRINT PredictiveConnectionKey = 0;

	bool IsValidKey() const { return KeyID > 0; }
	bool WasReceived() const { return PredictiveConnectionKey != 0; }
	bool WasLocallyGenerated() const { return KeyID > 0 && PredictiveConnectionKey == 0; }

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FPredictionKey> : public TStructOpsTypeTraitsBase2<FPredictionKey>
{
	enum { WithNetSerializer = true };
};

// 实现：对照 GAS FPredictionKey::NetSerialize（GameplayPrediction.cpp:49-100），去掉 Base/ServerInitiated 两档
inline bool FPredictionKey::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	// 仅当"无归属连接"（客户端 → 服务器）或"归属连接就是本连接"（服务器 → 发起客户端）时，才写真实 KeyID
	uint8 bValidForThisConnection = 0;
	if (Ar.IsSaving())
	{
		bValidForThisConnection =
			(PredictiveConnectionKey == 0 || (UPTRINT)Map == PredictiveConnectionKey) && (KeyID > 0);
	}
	Ar.SerializeBits(&bValidForThisConnection, 1);

	if (bValidForThisConnection)
	{
		Ar << KeyID;
		Ar << PredictionType;
		Ar << ClientRequestTime;
	}
	else
	{
		KeyID = 0;	// 非发起连接：键自动变为无效，调用方只需查 IsValidKey()
	}

	if (Ar.IsLoading() && KeyID > 0)
	{
		// 服务器读到了某客户端给的键：记下这个连接的标识，之后只回传给该连接
		PredictiveConnectionKey = (UPTRINT)Map;
	}

	bOutSuccess = true;
	return true;
}

// 以下结构体只存在于客户端，不跨 RPC，使用普通 struct 即可（无需反射）
// 预测键 ID 为 0 表示无效键（不再单独维护 bValid 标志）

// 状态生命周期绑定：状态到达指定值时自动冻结本键
struct FStateLifecycleBinding
{
	FName StateName;
	uint8 EndStateValue = 0;
};

// 非复制状态变量变化记录
struct FStateChangeRecord
{
	FName StateName;
	FString OldValue;	// 仅支持可字符串化的值
	FString NewValue;
	float Timestamp = 0.f;
};

// 表现触发记录
struct FPresentationRecord
{
	FName PresentationName;
};

// 委托列表（设计 3.5）：使用 DECLARE_DELEGATE 而非动态多播，保证性能；绑定对象生命周期由原代码保证
DECLARE_DELEGATE(FConfirmDelegate);
DECLARE_DELEGATE(FRollbackDelegate);

struct FPredictionDelegates
{
	TArray<FConfirmDelegate> ConfirmDelegates;
	TArray<FRollbackDelegate> RollbackDelegates;
};

// 预测记录，存储在预测组件中；只存客户端，不跨 RPC
struct FPredictionRecord
{
	uint32 KeyID = 0;
	TArray<FName> ReplicatedAttributes;					// 本预测键涉及的可复制属性名（含前缀）
	TArray<FStateChangeRecord> StateChanges;			// 非复制状态变化记录
	TArray<FPresentationRecord> Presentations;			// 表现触发记录
	TArray<FStateLifecycleBinding> LifecycleBindings;	// 状态生命周期绑定列表
	FVector MoveBaseline = FVector::ZeroVector;			// 位置类预测的一次性基线（位移执行前的位置）
	bool bHasMoveBaseline = false;						// 本键是否捕获过位置基线
	float StartTime = 0.f;
	float EndTime = 0.f;
	bool bResolved = false;		// 是否已结算
	bool bConfirmed = false;	// 结算结果：true = 确认，false = 回滚
	bool bFrozen = false;		// 是否已冻结（EndPredictionKey 后置位）
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class NARUTO_API UC_PredictionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UC_PredictionComponent();

	// ---- 生命周期接口（设计 3.4.1）----

	// 初始化预测上下文：缓存角色 / PlayerState / Controller 引用，并把三个宿主上的权威值表组件
	// 绑到本组件的 ApplyAuthorityValueTable（设计 3.2 / 3.4.1）
	// 返回 true 表示上下文有效；false 表示角色 / PlayerState / Controller 缺一（设计 3.4.1）
	// 敌方 PlayerState 与三个权威值表组件属于"可能还没同步到达"的部分：拿不到不算失败，本次留空；
	// 绑定成功前，对应宿主的属性名会被标记接口拒绝并记 Warning（W1.4 的守卫，见已定事项 11）
	// 可重复调用：客户端上 PlayerState / Controller 随后才到达，到达后再调一次即可（设计 3.4.1）；
	// 重绑即覆盖（TDelegate 单绑定），不会累积
	bool InitializePredictionContext();

	// 判断预测上下文是否有效（组件已初始化、角色有效、PlayerState 有效、Controller 有效）
	// 【不判断 IsLocallyControlled】—— 本地控制的判断由调用方在输入函数入口处完成（设计 3.4.1）。
	// 服务器上这四个引用同样齐备，故它同样返回 true；服务器不先行靠的是"服务器侧没有建键调用"（设计 3.2）
	// 返回 false 时调用方必须回退到原有路径：直接发原 Server RPC，不建键、不标记、不记录（设计 2.12）
	bool CanPredict() const;

	// 获取当前活跃预测键 ID，无活跃键时返回 0（设计 3.4.1）
	uint32 GetActivePredictionKeyID() const;

	// 获取当前活跃预测键的轻量结构，用于填充 Server RPC 参数（设计 3.4.1）
	// 无活跃键时返回 KeyID = 0 的默认键，调用方只需查 IsValidKey()。
	// 本机生成的键 PredictiveConnectionKey 恒为 0 —— 这正是"发起连接"的标记（设计 3.3.1）
	FPredictionKey GetActivePredictionKey() const;

	// ---- 预测键接口（设计 3.4.2）----

	// 创建预测键，返回预测键 ID；由状态预测调用
	// 生成自增 uint32 ID，同时创建轻量预测键与预测记录（设计 2.3 / 3.4.2）
	// 已有活跃键时断言（Debug）并返回 0（Shipping）
	uint32 CreatePredictionKey(uint8 PredictionType);

	// 冻结预测键：只把键标记为已冻结，停止接受新的变化记录，不执行结算（设计 2.3.1）
	// 冻结时自动进入未结算缓冲池；KeyID 不存在时忽略并记录 Warning
	void EndPredictionKey(uint32 KeyID);

	// 绑定状态生命周期：状态到达指定值时自动冻结本键（触发点见 TickPredictionTimeout）
	// StateName 取值（设计 3.4.2）："Self.PS.CharacterState" / "Self.PS.MySkill" / "Self.PS.Attack"
	// 支持多绑定：一个预测键可绑定多个 StateName；任一满足即冻结
	void BindStateLifecycle(uint32 KeyID, FName StateName, uint8 EndStateValue);

	// 根据预测键 ID 查找预测记录，找不到返回 nullptr
	FPredictionRecord* FindPredictionRecord(uint32 KeyID);
	const FPredictionRecord* FindPredictionRecord(uint32 KeyID) const;

	// 判断预测键是否仍然活跃（未冻结、未结算）
	bool IsPredictionKeyActive(uint32 KeyID) const;

	// ---- 预测标记接口（设计 3.4.3 / 2.7.2）----

	// 标记某个可复制属性正在被预测：写入预测标记表，并把属性名加入本键记录的 ReplicatedAttributes
	// 首次标记时确保该属性的权威值表已存在（设计 3.4.3）：属性名必须是权威值表内的字段（设计 3.3.1），
	// 且承载它的权威值表组件已绑定；两者缺一，标记永远等不到权威值，故忽略并记录 Warning
	// 该属性已有标记时：标记易主到新键，并从旧键记录的 ReplicatedAttributes 中移除该属性名，记录 Warning（接管规则，设计 2.7.2）
	// KeyID == 0、属性名为空、键已结算时忽略并记录 Warning
	// 注意：位置类属性**不得**调用本函数（只调 RecordMoveBaseline，设计 2.7.3 / 3.4.3）
	void MarkReplicatedAttribute(FName AttributeName, uint32 KeyID);

	// 权威值表到达（OnRep_AuthorityValueTable）时调用：逐字段套用采用规则（设计 2.7.2 三行）
	// 表本身就是权威值：本组件不另存副本（设计 3.3），因此"只更新权威值表"那一行不需要写任何东西
	void ApplyAuthorityValueTable(const FAuthorityValueTable& Table);

	// 查询某个可复制属性是否正在被预测（标记表只保留"尚未收到权威值"的属性，设计 3.3）
	bool IsReplicatedAttributePredicted(FName AttributeName) const;

	// ---- 位置类预测：一次性基线（设计 2.5.1-C / 2.9 / 3.4.3）----

	// 记录位置类预测的【一次性基线】：把当前位置捕获进活跃预测键的 MoveBaseline，每键只调用一次
	// 位移执行前调用（动画通知内）。位置**不写预测标记**：拥有者收不到自身位置的复制，等不到权威值，
	// 标记没有清除时机（设计 3.4.3 约定）——回滚位置只看 bHasMoveBaseline
	// 无活跃键时忽略、记录 Warning，并返回 false —— 调用方据此决定是否仍执行位移
	// 已捕获过基线时**不改写基线**、记录 Warning，仍返回 true：一个键只发生一次位移，多段位移应拆成
	// 多个键（设计 2.9 / 3.4.3）；这次位移仍被第一次捕获的基线覆盖，回滚依然精确
	bool RecordMoveBaseline();

	// ---- 非复制数据记录接口（设计 3.4.4 / 3.5）----

	// 记录非复制状态变量的旧值、新值、时间戳，并接收本条的跟进/回滚委托
	// 记录挂到**当前活跃**预测键；无活跃键、或键已冻结/结算时忽略并记录 Warning（设计 3.4.4 约定）
	// 委托由组件集中存入委托表、按注册顺序排列，**不**存在变化记录里（设计 3.5）
	// 接入规范：先注册状态委托、后注册表现委托，保证表现基于已恢复的状态（设计 3.5）
	// 变化记录只针对非复制数据；StateName 必须带来源前缀（设计 3.4.4）。若传进来的名字是权威值表
	// 内的字段（该走 MarkReplicatedAttribute）会额外记 Warning，但仍照常记录
	void RecordStateChange(FName StateName, const FString& OldValue, const FString& NewValue,
		float Timestamp, FConfirmDelegate ConfirmDelegate, FRollbackDelegate RollbackDelegate);

	// 记录表现触发（动画、特效、音效、UI）的跟进/回滚回调，同样挂到当前活跃预测键
	// 不需回滚的表现传空委托即可；委托参与结算（设计 3.4.4 / 3.5）
	void RecordPresentation(FName PresentationName, FConfirmDelegate ConfirmDelegate, FRollbackDelegate RollbackDelegate);

	// 获取某个预测键下的全部非复制变化记录 / 表现记录；键不存在时返回空数组
	// 已结算的键仍可查（记录保留供调试与历史查询，设计 3.4.5）
	const TArray<FStateChangeRecord>& GetStateChangeRecords(uint32 KeyID) const;
	const TArray<FPresentationRecord>& GetPresentationRecords(uint32 KeyID) const;

	// ---- 结算接口（设计 3.4.5 / 2.11）----

	// 统一入口：服务器回执到达时调用，按 Result 自动选择跟进或回滚（设计 2.11.3）
	// Result：0 = Confirmed，1 = Rejected；其他值视为 Rejected 并记录 Warning
	// KeyID 在本地键表中找不到时【静默返回】—— 迟到回执（超时已把键移出键表）与重复回执同属正常竞态，不记 Warning
	// 结算后：键移出预测键表与缓冲池、委托表清掉本键条目；预测记录保留（bResolved = true），供调试与历史查询
	void ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked);

	// 多播 / 客户端 RPC 到达时调用：按名字从预测标记表中移除对应标记
	// 语义：该数据的权威值已到，回滚时跳过它（设计 2.11.3 第 6 条）；不触发跟进/回滚委托
	// 名字必须与 MarkReplicatedAttribute 用过的名字一致（带前缀的属性名）才配得上，配不上时【静默返回】——
	// 多数多播与本地预测无关，属常态，记 Warning 会刷屏（设计 3.4.5 约定）
	void OnMulticastArrived(FName MulticastName);

	// 预测键超时检查与生命周期兜底：由 AC_Character::Tick 调用，组件不单独开启 Tick（设计 3.2）
	// 一、生命周期绑定（设计 3.4.2）：绑定的状态变量已到达 EndStateValue 时自动 EndPredictionKey
	// 二、超时（设计 2.3.1 / 3.4.5）：所有 !bResolved 的记录（含已冻结键）里，世界时间与 StartTime 之差
	//     超过 PredictionTimeout 的强制回滚，回滚内容与 Rejected 完全一致
	// 超时判定必须用世界时间，不能累加 DeltaTime（玩家被时停时 DeltaSeconds 为 0，见 2.3.1 实现约束）
	void TickPredictionTimeout(float DeltaTime);

	// 只读查询权威值表（设计 3.4.5，调试用）：把表内对应字段的值字符串化
	// 属性名必须是表内字段（设计 3.3.1 / 2.7.3）且宿主已绑定，否则返回 false 并记录 Warning
	bool GetAuthorityValue(FName AttributeName, FString& OutValue) const;

private:
	// ---- 宿主引用（设计 3.2 / 3.4.1）----
	// 在 InitializePredictionContext 里缓存；未初始化、或宿主尚未同步到达时保持 nullptr，
	// CanPredict 据此返回 false，调用方降级到原有路径（设计 2.12）
	UPROPERTY(Transient) TObjectPtr<AC_Character> SelfCharacter;
	UPROPERTY(Transient) TObjectPtr<AC_PlayerState> SelfPlayerState;
	UPROPERTY(Transient) TObjectPtr<AC_PlayerController> SelfController;
	UPROPERTY(Transient) TObjectPtr<AC_PlayerState> EnemyPlayerState;	// 按 Team 从 GameState->PlayerArray 定位

	// "上下文还没就绪"这条日志只记一次的标记（2.5 验收时发现）：每帧重试是设计（3.4.1），
	// 但缺 PlayerState / Controller 是正常中间态，逐帧记一行 Verbose 会把日志淹掉。
	// 只在第一次失败时记一行（带角色名），成功那一次重新武装 —— 下次真的失效时还看得到
	bool bContextPendingLogged = false;

	// ---- 容器（设计 3.3）----

	// 存储活跃与未结算的预测键；结算后移除
	TMap<uint32, FPredictionKey> PredictionKeys;

	// 每个预测键对应的完整本地记录；结算后保留（bResolved = true），供调试
	TMap<uint32, FPredictionRecord> PredictionRecords;

	// 未结算缓冲池：已冻结、待结算的预测键 ID；结算或超时回滚后出池
	TArray<uint32> PendingKeys;

	// 当前活跃（未冻结）预测键；0 = 无。同一时刻只允许一个（设计 2.3.2）
	uint32 ActivePredictionKeyID = 0;

	// 预测记录保留上限（设计 3.3）：只淘汰已结算记录（按时间淘汰最旧）；未结算记录不得淘汰
	UPROPERTY(EditAnywhere, Category = "Prediction")
	int32 MaxPredictionRecords = 64;

	// 预测键超时上限（秒，设计 2.3.1 / 3.4.5）：世界时间与记录 StartTime 之差超过它就强制回滚
	UPROPERTY(EditAnywhere, Category = "Prediction")
	float PredictionTimeout = 2.0f;

	// 预测标记表（设计 3.3 / 2.7.2）：键为带前缀的属性名（设计 2.7.3），值为预测键 ID。
	// 只保留"尚未收到权威值"的属性；权威值表带来该字段的权威值时清除。位置类属性从不进入本表。
	TMap<FName, uint32> PredictionMarkers;

	// 委托表（设计 3.3 / 3.5）：每个预测键的跟进/回滚委托列表，按注册顺序排列。
	// 委托**不**存在变化记录里；结算时取出该键的两条列表依次执行（W1.7），执行完随结算清掉该键的条目
	TMap<uint32, FPredictionDelegates> PredictionDelegates;

	// ---- 权威值表组件引用（设计 3.2 / 3.3）----
	// 表不在本组件里：正文在权威值表组件上，本组件只持引用——客户端手上的那一份同时就是权威值。
	// InitializePredictionContext 里用 FindComponentByClass 绑定，并把它的表到达委托绑到
	// ApplyAuthorityValueTable。绑定失败（如敌方 PlayerState 尚未同步）时保持 nullptr，标记接口会拒绝该宿主的属性名。
	UPROPERTY(Transient) TObjectPtr<UC_AuthorityValueComponent> SelfPSAuthority;		// 己方 PlayerState 上的（Self.PS.*）
	UPROPERTY(Transient) TObjectPtr<UC_AuthorityValueComponent> SelfCharacterAuthority;	// 己方 Character 上的（Self.Char.*）
	UPROPERTY(Transient) TObjectPtr<UC_AuthorityValueComponent> EnemyPSAuthority;		// 敌方 PlayerState 上的（Enemy.PS.*）

	// 内部：Prediction.Draw 开着时把预测状态画到屏上（阶段二 2.1）。
	// 组件自己不 Tick（设计 3.2），绘制借用每帧唯一的那次调用 —— TickPredictionTimeout 的开头。
	// 开关关着时本函数第一行就返回，对 TickPredictionTimeout 的语义零影响。
	// 只画本地控制的那个角色：一场对战里两个角色各有一个预测组件，都画会互相盖住
	void DrawPredictionDebug() const;

	// 内部：统一取世界时间。超时判定必须用世界时间，不能累加 DeltaTime（设计 2.3.1 实现约束）
	float GetPredictionTimeSeconds() const;

	// 内部：记录数超过上限时淘汰最旧的已结算记录
	void EvictOldResolvedRecords();

	// 内部：套用权威值表的采用规则（逐字段，设计 2.7.2 / 2.4.4）
	// 表来自哪个宿主由表的地址确定：表就在权威值表组件里、客户端只有那一份（设计 3.3），
	// 所以"到达的表 == 某个已绑定组件的 AuthorityValueTable"就是宿主的身份。
	// 三个宿主因此共用同一个入口，设计 3.2 的"在 OnRep_AuthorityValueTable 的委托上调用 ApplyAuthorityValueTable"不需要按宿主各写一个包装
	void ApplyAuthorityValueTableInternal(const FAuthorityValueTable& Table);

	// 内部：解析宿主 Actor 上的权威值表组件，并把它的表到达委托绑到本组件的 ApplyAuthorityValueTable
	//（设计 3.2）。宿主为空、或它还没挂/还没同步到达该组件时，OutAuthority 保持 nullptr —— 该宿主的属性名
	// 会被标记接口拒绝并记 Warning（见已定事项 11 的第二道守卫）。
	// 绑定必须是本组件的成员函数直绑：ApplyAuthorityValueTable 只有表、没有宿主身份，靠表的地址认宿主
	//（已定事项 10）——换成会拷贝表的 lambda 会让"表的地址"失效
	void BindAuthorityValueHost(const AActor* HostActor, TObjectPtr<UC_AuthorityValueComponent>& OutAuthority);

	// 内部：把带前缀的属性名解析到承载它的权威值表组件（设计 2.7.3 / 3.2）。
	// 属性名不在权威值表的字段集内（设计 3.3.1）、或对应组件尚未绑定时，记录 Warning 并返回 nullptr。
	// W1.4 的 MarkReplicatedAttribute 与 W1.7 的回滚恢复都用它定位宿主。
	UC_AuthorityValueComponent* ResolveAuthorityComponentForAttribute(FName AttributeName) const;

	// 内部：回滚时判断某个属性是否应跳过（设计 2.7.2 两条防护规则）：
	// 该属性已无标记（权威值已到达并在 OnRep 中清除）→ 跳过，不要用旧值覆盖权威值；
	// 标记已易主（Mark[Name] != 本键 ID）→ 跳过，由新键负责。
	// W1.7 的 RollbackPrediction 使用。
	bool ShouldSkipAttributeOnRollback(FName AttributeName, uint32 KeyID) const;

	// 内部：把本条变化带来的跟进/回滚委托追加进该键的委托表（设计 3.5：委托不入变化记录、集中存表、按注册顺序）
	// 空委托不入表 —— 设计 3.5 的"不需回滚的变化传空委托即可"
	void AppendDelegates(uint32 KeyID, const FConfirmDelegate& ConfirmDelegate, const FRollbackDelegate& RollbackDelegate);

	// 内部：跟进结算（设计 3.4.5）—— 按注册顺序执行跟进委托；清除本键登记的可复制属性标记，**不写回本地值**：
	// 服务器的"接受"意味着它写下了与预测相同的值（设计 2.7.2 结算表），之后到达的表按无标记路径再写一次，幂等
	void ConfirmPrediction(uint32 KeyID, uint8 ConfirmedStatePacked);

	// 内部：回滚结算（设计 3.4.5）—— 按注册顺序执行回滚委托（接入方据此恢复非复制数据旧值）→
	// 本键登记的可复制属性从权威值表恢复（逐项过两条防护规则）→ 位置按 MoveBaseline 恢复（仅当 bHasMoveBaseline）
	void RollbackPrediction(uint32 KeyID);

	// 内部：从权威值表恢复某个属性的本地值（设计 3.4.5）。名字不在表内或宿主未绑定时记 Warning 并返回
	void RestoreFromAuthorityValue(FName AttributeName);

	// 内部：结算收尾（设计 3.4.5）—— 记录置 bResolved / bConfirmed（记录保留供调试）、键出键表、出缓冲池、
	// 委托表清掉本键条目（委托本轮已执行完，留着会让表随预测次数无限增长）、活跃键被结算时归零
	void FinishPredictionKey(uint32 KeyID, bool bConfirmed);
};
