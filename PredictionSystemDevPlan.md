# 预测系统开发计划

**配套文档**：`PredictionSystemDesign.md`（设计，接口与结构体已冻结）、`AICollaborativeNotes.md`（决策记要）。

本计划只回答三件事：**按什么顺序做、每步怎么验、出错怎么退**。设计细节不在此重复，凡有歧义以设计文档为准（章节引用一律指向设计文档）。

---

## 0. 分期依据

1. **新增文件优先**。预测组件与权威值表组件先在新建文件里独立完成，不碰现有代码——编译通过就等于"工程行为零变化"（组件没有实例，任何路径都执行不到）。
2. **降级路径是隔离的地基**。设计 2.12 与第 4 章要求每个接入点都以 `CanPredict()` 为前置，为假时走原有路径。因此"接入"与"不接入"是同一份代码的两个分支，可以逐点打开、逐点关掉。
3. **切片 = 一次输入 = 一个预测键**。键在输入点创建，标记按字段登记，回滚只回滚本键登记过的东西（设计 2.11.2）。不同功能之间不共享键、不共享记录，天然互不干扰。
4. **每个切片一个开关**，功能出问题时就地关掉该切片，其余切片与原有路径不受影响。
5. **无键按机制切（阶段二），有键按输入切（阶段三起）**。阶段二的表 / 锁 / 回执 / 通知拆分，是每次输入都要经过、却没有自己的键的机制，只能按机制整体接入；阶段三起每片都有自己的输入与键，按输入切。切片内部仍按设计 2.1 的三类顺序落笔（先状态、再属性、锁只检查不登记），但三类不各自成片——横着按机制切，会让每一片都依赖并重测前面所有片，隔离性就没了。

**开关能退什么、不能退什么**（先分清，免得误判）：

| 类型 | 例子 | 关掉开关后 |
| --- | --- | --- |
| 预测行为 | 建键、写标记、记录、`RecordMoveBaseline` | 完全回到原有路径（不建键、不标记、直接发原 RPC） |
| 结构性改动 | 段号写入点前移（2.4.4）、动画通知拆分（5.9a）、碰撞框派生（5.9b）、属性摘除（5.2b） | **开关管不到**——它们改的是实现本身，只能靠 `git revert` 那一笔提交 |

所以结构性改动一律单独提交、单独验收，不与预测行为混在一笔里。

## 阶段总览

| 阶段 | 内容 | 改现有代码 | 依赖 | 结束时的状态 | 规模 |
| --- | --- | --- | --- | --- | --- |
| 一 | 预测组件 + 权威值表组件实现 | **否**（纯新增文件，外加两行友元） | 无 | 编译通过；工程行为零变化 | 大 |
| 二 | 接入前置（装上轮子，但一局零预测） | 是（逐项独立提交） | 一 | 表 / 锁 / 回执三条通道就位；对战表现与改造前一致 | 大 |
| 三 | 逐功能切片（第一批） | 是（按切片划分） | 二 | 技能 / 替身 / 普攻 / 位移 / 碰撞框各自可开关、可单独验收 | 大 |
| 四 | 命中预测（第二批） | 是 | 三（碰撞框切片） | 攻击方本地命中判定 + 敌方代理预测 | 中 |
| 五 | 全量验收与收尾 | 文档为主 | 全部 | 开关全开 / 全关两种状态都能跑完整一局 | 小 |

设计 5.9(b) 里的"第一批 / 第二批"即对应本计划的**阶段三 / 阶段四**。

---

## 工具与调试开关（贯穿全程，随用随建）

LAN 上 RTT 约一帧，"本地先行 → 被拒绝 → 回滚"这条路径靠自然操作几乎撞不上（设计 5.1 的注也点明了这一点：拒绝路径不落地，回滚分支就测不出来）。**工具不是可选项，是每个切片验收的前提。**

| 工具 | 侧 | 作用 | 何时建 |
| --- | --- | --- | --- |
| `Prediction.Log` | 客户端 | 打印建键 / 冻结 / 结算 / 回滚，以及采用规则命中了哪一行（分类 `LogPrediction` 已在阶段一 W1.2 建好） | 阶段二 2.1 |
| `Prediction.Draw` | 客户端 | 屏上绘制：活跃键、标记表、三个锁值、表内字段 | 阶段二 2.1 |
| `Prediction.Enabled` | 客户端 | 全局关闭：所有接入点退回原有路径 | 阶段二 2.1 |
| `Prediction.Skill` / `Escape` / `Attack` / `Move` / `Box` / `Hit` | 客户端 | 逐片开关，在各自的输入点读取 | 随各切片 |
| `Prediction.ForceReject` | 服务器 | 对带键请求一律回 `Rejected` 且不写权威值 → 回滚路径 100% 可复现 | 阶段二 2.6 |
| `Prediction.DropResolve` | 服务器 | 不回执 → 验证超时兜底（默认 2.0s） | 阶段二 2.6 |
| `Net PktLag=100` / `Net PktLoss=2` | 引擎自带 | 拉长预测窗口、验证丢包下的收敛 | 阶段二起 |

约定：

- 每个开关的**关**必须是安全态（等价于"未接入"）；`ForceReject` / `DropResolve` 只在非 Shipping 构建里编译。
- 开关用 cvar 而非组件配置属性：不改动设计 3.7 冻结表里的任何签名，且能在 console 里逐片排查。
- 这些 cvar **长期保留**，它们是以后线上排障的唯一手段。
- **2.1 已落地**：三个开关都是文件级 cvar，`extern` 声明在 `C_PredictionComponent.h`（不新增类接口，设计 3.7 不动）。`Prediction.Log 1` 把 `LogPrediction` 抬到 Verbose；`Prediction.Draw` 只画**本地控制**的那个角色，绘制借 `TickPredictionTimeout` 的开头进来（组件自己不 Tick，见设计 3.2），默认关；`Prediction.Enabled` 默认开，**只被接入点读取，不进 `CanPredict()`**——那条判据问的是"上下文是否有效"（已定事项 19），与"这次要不要预测"是两件事。
- **2.6 已落地**：`Prediction.ForceReject` / `Prediction.DropResolve` 是**服务器侧**开关，定义与声明整段包在 `#if !UE_BUILD_SHIPPING` 里（本表的约定），读取走两个恒定义的查询函数（Shipping 里恒 false，见已定事项 39）；`ForceReject` 只作用于带键请求。配套验收命令 `Prediction.DebugKeyedSkill <1|2|4|5>`（本地临时构造键、故意不进键表）。

---

## 源文件编码约定（写代码前先看这条）

工程源文件是 **GBK（本机 ANSI 代码页 936）+ CRLF**，中文只出现在注释里。这条约定在阶段一 W1.2 第一次写中文日志文案时撞墙，结论如下。

**根因**：UE 5.3 的 UBT **无条件**给 cl.exe 传 `/utf-8`（`Engine/Source/Programs/UnrealBuildTool/Platform/Windows/VCToolChain.cs:465`），紧跟着的 `:468` 是 `/wd4819`（屏蔽"字符无法在当前代码页表示"的警告）。于是编译器按 **UTF-8** 解码 GBK 源文件：

- 中文出现在**注释**里 → 只是无效字节序列，警告被 `/wd4819` 压掉，**能编过**（所以 W1.1 一路绿灯）；
- 中文出现在**字符串字面量**里 → 硬错误 `error C2001: 常量中有换行符`（W1.2 首踩，报在第一条中文 `UE_LOG` 文案上）。

**定案（全英文日志）**：

- 新增 / 修改的源文件保持 **GBK + CRLF**，与工程既有文件一致；
- `UE_LOG` / `ensureMsgf` / `TEXT(...)` 等**字面量一律 ASCII 英文**，中文只写在注释里；
- 工程里已有的 `UE_LOG(LogTemp, ...)` 不受影响（本来就没有中文）。

**隔离验证**（同一份字面量，只换源文件编码，用同一套 cl.exe 直编）：

| 源文件编码 | 结果 |
| --- | --- |
| GBK（工程现状） | `warning C4828` ×10 + `error C2001` —— 与工程构建报错完全一致 |
| UTF-8 带 BOM | 通过，宽字符串解码正确 |
| UTF-8 不带 BOM | 通过 |

**被否掉的备选**：新文件转 UTF-8（带 BOM）——中文日志文案能保住，但工程内会并存两种编码；全工程转 UTF-8——编码统一且中文可用，但超出阶段一"除两行友元外不动现有文件"的边界，另找一次单独做。

---

## 阶段一：预测组件（新增文件，零改动现有代码）

### 1.1 交付物

| 文件 | 内容 | 设计 |
| --- | --- | --- |
| `Source/Naruto/C_PredictionComponent.h / .cpp` | `UC_PredictionComponent`、`EPredictionType`、全部结构体与委托类型 | 3.2 / 3.3 |
| `Source/Naruto/C_AuthorityValueComponent.h / .cpp` | `UC_AuthorityValueComponent`、`FAuthorityValueTable` | 2.7.5 / 3.3.1 |

`Naruto.Build.cs` **无需改动**（`FPredictionKey::NetSerialize` 需要的 `NetCore` 已在依赖列表里）。

**硬约束**：除 1.3 的两行友元外，本阶段不修改任何现有文件；组件不挂载、不被调用，因此"编译通过"就等于"工程行为零变化"。

### 1.2 工作包

| 工作包 | 内容 | 设计 |
| --- | --- | --- |
| W1.1 结构体与枚举 | `FPredictionKey`（含 `NetSerialize` 与 `TStructOpsTypeTraits`）、`FAuthorityValueTable`、`FPredictionRecord`、`FStateLifecycleBinding` / `FStateChangeRecord` / `FPresentationRecord`、`FPredictionDelegates`、`EPredictionType` | 3.3.1 / 3.3.2 |
| W1.2 键生命周期 | `CreatePredictionKey`（已有活跃键时断言）、`EndPredictionKey`（只冻结）、`BindStateLifecycle`、`FindPredictionRecord`、`IsPredictionKeyActive`、未结算缓冲池、`MaxPredictionRecords` 淘汰 | 2.3.1 / 2.3.2 / 3.4.2 |
| W1.3 权威值表组件 | `UC_AuthorityValueComponent`：构造函数（`SetIsReplicatedByDefault(true)`）、`GetLifetimeReplicatedProps`（只注册 `AuthorityValueTable`）、`PreReplication` 按宿主刷新表（PS 段 / Character 段）、`OnRep_AuthorityValueTable` + 表到达委托（供预测组件绑定）；同时落地 1.3 的两行友元 | 2.7.5 / 3.3.1 / 3.2 |
| W1.4 标记与采用 | `MarkReplicatedAttribute`、`ApplyAuthorityValueTable`（采用规则三行 + 逐字段清标记）、`IsReplicatedAttributePredicted`、两条防护规则、接管规则 | 2.7.2 / 3.4.3 |
| W1.5 位置 | `RecordMoveBaseline`（一次性基线） | 2.5.1-C / 2.9 / 3.4.3 |
| W1.6 记录与委托 | `RecordStateChange` / `RecordPresentation` / 两个 Get、委托表、按注册顺序执行 | 3.4.4 / 3.5 |
| W1.7 结算与兜底 | `ResolvePrediction` → `ConfirmPrediction` / `RollbackPrediction`、`OnMulticastArrived`、`TickPredictionTimeout`、`GetAuthorityValue` | 2.11 / 3.4.5 |
| W1.8 上下文 | `InitializePredictionContext`、`CanPredict`、`GetActivePredictionKey(ID)`、`FindComponentByClass` 绑定宿主上的权威值表组件（并绑定其表到达委托）；清理向导桩代码（`bCanEverTick = false`、去掉 `TickComponent` / `BeginPlay` 覆写，见设计 3.2） | 3.2 / 3.4.1 |

工作包之间只有编译期依赖，实现顺序可按表从上到下；每个工作包一次提交。

### 1.3 访问边界（本阶段唯一的现有文件改动，两行）

组件需要读写的宿主成员里，有几个是 `private`：

| 宿主 | 成员 | 现状 | 处理 |
| --- | --- | --- | --- |
| `AC_Character` | `LastEscapeTime`、`LastFirstSkillTime`、`LastSecondSkillTime`、`LastScrollTime`、`LastSummonTime` | `private`（`C_Character.h:336` 之后） | 加两行：`friend class UC_PredictionComponent;`（本地先行读写）+ `friend class UC_AuthorityValueComponent;`（`PreReplication` 里从这几个时间戳读值填表，见设计 5.3） |
| `AC_Character` | `bInProtectAnim` | `private`（`C_Character.h:303-304`） | 同上 |
| `AC_PlayerState` | 全部相关字段（`Chakra` / `Attack` / `MySkill` / `CharacterState` / `HealthValue`） | 已是 `public` | 无需处理 |

其余用到的成员（`Toward`、三个锁、`LaunchState`、`MyAttack` / `MyCState` / `MySkill`、四个 `*CDState`）都在 `public`。

替代方案是不加友元、改为在宿主上补一组访问器——改动面更大，且同样要动现有文件，**不推荐**。**结论：加这两行友元**（第二行归权威值表组件：Character 段字段里 `LastEscapeTime` 与四个 CD 时间戳都是 `private`，只有 `Toward` 是 `public`）；提交时机可随阶段一一起（零行为），也可并入 2.1 的挂载提交。

### 1.4 验收

- 编译通过（`Naruto` 模块与编辑器）。
- 接口与设计 3.7 冻结表逐条对照，签名与语义无出入；发现出入时**先改设计文档、再改代码**。
- 组件未被任何地方引用：全局搜索 `UC_PredictionComponent` / `UC_AuthorityValueComponent` 只命中新文件本身。
- 可选自动化测试（新文件，`WITH_DEV_AUTOMATION_TESTS` 包裹）：键生命周期、采用规则三行、两条防护规则、超时判定。**结论：先不引入**——值得测的分支都在"有宿主、有 RPC"之后，从 3.1 起用实机 + `Prediction.Draw` 更省事。

### 1.5 回退

删除 4 个新文件与那两行友元即可，无残留引用。

---

## 阶段二：接入前置（装上轮子，一局零预测）

目标：把预测系统的**全部基础设施**就位，但**不建任何预测键**。因此本阶段结束时，整局对战的行为、表现、UI 必须与改造前一致——这就是后面所有切片赖以隔离的基线。

每项独立提交、独立验收；顺序按"风险从低到高"排。

### 2.0 蓝图侧调用点盘点（已完成）——2.7 / 3.1 / 3.3 / 3.4 的输入

**产出归档**：`BlueprintSideCallSiteInventory.md`（逐资产、逐接口读实现流程）。本节只留三样：首轮三问的结论、调用点清单、由此产生的下游修正。首轮那张按资产名搜出来的挂载表已被 2.0.3 取代。

#### 2.0.1 首轮三问的结论

1. **三个函数的调用点**：`ChangeAttack` / `ChangeState` / `MakeMove` 确无 C++ 调用点，路径都是 `AN_ChangeAttack` / `AN_ChangeState` / `AN_MakeMove` → `BPI_Character` → **实现在 `BP_Character`**（`BP_Menma` 只额外实现了 `I_HitJump`）。
2. **纯表现事件挂在哪**：工程里**不存在** `BP_FirstSkillEffect` / `BP_SecondSkillEffect` / `BP_FinalSkillEffect` / `BP_SummonEffect` 这类资产——首轮按名字搜到的线索落空，不必再找。角色的纯表现只有三条通道，且都写在 `BP_Character` 的接口实现里：`I_SpawnSE`（蓝图内部函数 `SpawnBPSE`）、`I_PlaySound`（`PlaySound2D`）、`I_CameraShake`；生成物（秘卷 / 通灵魔法师、攻击体）另走 `I_Summon` → `I_SpawnAttacker`。
3. **`HasAuthority()` 散落在哪**：**19 个通知资产里一个都没有**。通知的实现全是"直接调用 `BPI_Character` 同名接口"的纯转发，无额外逻辑、无判定；权威判定**全部集中在 `BP_Character` 的接口实现里**（`BPI_Character` 共 20 个函数，分布见 2.0.2）。→ **2.7 的拆分对象从"19 个通知资产"收敛为"一个资产的一组接口函数"**，这是本次盘点对 2.7 最大的简化。

#### 2.0.2 调用点清单（通知 → 现在做了什么 → 应归哪一类）

「现行判定」一列取自调查报告：**权威** = 只在服务器执行；**本地控制** = 只在本地控制端执行；**无** = 两端各自执行。

**时机类**（通知的发生时刻本身有意义，或它写的量会被服务器校验）：

| 通知 | 接口 | 现在做了什么（`BP_Character` 实现） | 现行判定 |
| --- | --- | --- | --- |
| `AN_ChangeAttack` | `I_ChangeAttack` | 权威 → `ChangeAttack(Attack)`：写 `PS->Attack` / `MyAttack`；`attack == 0` 时收招归零（`Attack` / `MySkill` / `CharacterState` / 锁），非 0 时置 `bAttackInputLock` 并按 `TryTargetToward` 直调 `Server_ChangeToward_Implementation` | **权威** |
| `AN_ChangeState` | `I_ChangeState` | 权威 → `ChangeState(State)`，写 `PS->CharacterState`（霸体：`Armor` / `Unbreakable`） | **权威** |
| `AN_MakeMove` | `I_MakeMove` | 权威 → `MakeMove(Offset, 本地 TryTargetToward)`：按朝向与移动意图校正后 `AddActorLocalOffset` | **权威** |
| `AN_PreInput` | `I_StartPreInput` | 权威 → `bPreInputLock = false`、`TryTargetToward = 0` | 权威 |
| `AN_LockTargetToward` | `I_LockTargetToward` | 权威 → `TryTargetToward` 赋值 | 权威 |
| `AN_ChangeDamageValue` | `I_ChangeDamageValue` | 权威 → 写造成伤害的 `Type` / `Effect` / `Value` / `Time` / `State`（非复制，命中判定用） | 权威 |
| `AN_StartHitCheck` | `I_StartHitCheck` | 权威 → `SuccessHit = false` | 权威 |
| `AN_SetGrab` / `AN_StopGrab` | `I_SetGrab` / `I_StopGrab` | 权威 → 设 / 清自身抓取点（决定 `Mult_ChangeGrabLocation` 落在哪） | 权威 |
| `AN_ChangeAttackBox` / `AN_ChangePlayerBox` | `I_ChangeBox` | 本地控制 → `ServerChangeBox(Size, Offset, Type)`，服务器再 `Mult_ChangeBoxSize` 回来 | 本地控制 |
| `AN_HitJump` | `I_HitJump`（`BP_Menma` 实现） | 权威且 `successHit` → `MySkill = 3`（派生技能）+ `I_GiveChakra` | 权威 |
| `AN_ChangeGravity` | `I_ChangeGravity` | 按参数把角色移动组件 `GravityScale` 置 0 / 1——**本地物理量**，5.9a 的注要求与 `Mult_ChangeGravity` 的 `LaunchState` 一起看 | **无** |

**纯权威类**（只产生表现或生成物，不写任何被校验的量）：

| 通知 | 接口 | 现在做了什么 | 现行判定 |
| --- | --- | --- | --- |
| `AN_PlaySound` | `I_PlaySound` | `PlaySound2D` | **无 —— 两端各播一次（2.7 要改的就是它）** |
| `AN_CameraShake` | `I_CameraShake` | 摄像头晃动 | **无 —— 同上** |
| `AN_SpawnSE` | `I_SpawnSE` | 权威 → `SpawnBPSE(Offset, SEName, 本地 Toward)` | 权威（已合规，不动） |
| `AN_SetOtherPauseState` | `I_SetOtherPauseState` | 权威 → `SetOtherPauseState(bool)`（暂停对手输入） | 权威（已合规，不动） |
| `AN_SpawnAttacker` | `I_SpawnAttacker` | 权威 → 按类型 / 坐标 / `Size` 生成攻击体，设 `Owner` 为自身后 `StartUse` | 权威（已合规，不动） |
| `AN_Summon` | `I_Summon` | 按 `SummonIndex` 转调 `I_SpawnAttacker`（0 秘卷查克拉 / 1 通灵魔法师） | 转发层无判定，**被调者权威**（等效已合规） |

**没有通知的两个接口**（属属性预测，接入点在 C++ 侧）：

| 接口 | 现在做了什么 | 现行判定 |
| --- | --- | --- |
| `I_GiveChakra` | `AddChakra()` → **直接调用 `Server_ChangeChakra_Implementation`**（`C_Character.cpp:154-159`，绕过 RPC） | 无判定 → 2.2 修直调、2.3 补服务器校验 |
| `I_MakeDamage` | `BeDamaged(Type, Effect, Value, Time, State, GrabPoint)` | 无判定 → 写的是敌方属性，阶段四 |

> **攻击体侧（`AAN` / `BPI_AttackerBase`）不在 2.7 范围**：调查报告已注明，攻击体只在服务器生成、由复制同步到各端，所以其内部部分函数没有权威判断。命中的客户端判定属阶段四，届时单独盘一遍。

#### 2.0.3 挂载矩阵（取代首轮表）

按序列资产对通知类的引用逐条比对（只读，未改任何资产）：

| 序列 | 挂载的通知 |
| --- | --- |
| `Attack1` / `Attack2` | `AN_ChangeAttack`、`AN_ChangeAttackBox`、`AN_ChangeDamageValue`、`AN_ChangePlayerBox`、`AN_LockTargetToward`、`AN_MakeMove`、`AN_PlaySound`、`AN_PreInput` |
| `Attack3` | 同 `Attack1`，另加 `AN_CameraShake`、`AN_SpawnSE` |
| `Attack4` | 同 `Attack1`，另加 `AN_CameraShake`、`AN_SetGrab`、`AN_StopGrab` |
| `Attack5` | 同 `Attack3`，但**无** `AN_PreInput` |
| `FirstSkill` | 13 个：`AN_ChangeAttack`、`AN_ChangeAttackBox`、`AN_ChangeDamageValue`、`AN_ChangePlayerBox`、`AN_ChangeState`、`AN_HitJump`、`AN_LockTargetToward`、`AN_MakeMove`、`AN_PlaySound`、`AN_SetGrab`、`AN_StartHitCheck`、`AN_StopGrab`、`AN_CameraShake` |
| `FirstSkillb` | `AN_ChangeAttack`、`AN_ChangeAttackBox`、`AN_ChangeDamageValue`、`AN_ChangePlayerBox`、`AN_PlaySound`、`AN_SetGrab`、`AN_SpawnAttacker`、`AN_StopGrab` |
| `SecondSkill` | `AN_ChangeAttack`、`AN_ChangeState`、`AN_PlaySound`、`AN_SpawnAttacker`、`AN_StopGrab` |
| `FinalSkill`（奥义） | `AN_ChangeAttack`、`AN_ChangeGravity`、`AN_ChangePlayerBox`、`AN_ChangeState`、`AN_MakeMove`、`AN_PlaySound`、`AN_SetOtherPauseState`、`AN_SpawnAttacker` |
| `Summon` | `AN_ChangeAttack`、`AN_ChangeState`、`AN_Summon` |
| `Idle` | `AN_ChangeAttackBox`、`AN_ChangeDamageValue`、`AN_ChangePlayerBox`、`AN_PreInput` |
| `Walk` | `AN_ChangeAttackBox`、`AN_ChangePlayerBox` |
| `Grabbed` / `Launched` / `Staggered` | 无 |

**对首轮表的更正**（只有一处，但会误导 3.3 / 3.4 的回归范围）：首轮记 `Idle` / `Walk` 挂 `AN_ChangeAttack`——**两条都没有挂**。也就是 `AN_ChangeAttack` 只出现在 5 段普攻、四个技能与 `FirstSkillb` 上（凡是要推进连段 / 收招的序列），`Idle` / `Walk` / 受击序列上没有任何连段推进。

**顺带落定 `Adamantine`**：全工程只有 `FirstSkillb` 资产带这个取值，而该序列**没有挂** `AN_ChangeState`；`AN_ChangeState` 的挂载点只有四个，取值分别是 `FirstSkill` / `SecondSkill` = `Armor`、`FinalSkill` / `Summon` = `Unbreakable`。→ **`Adamantine` 当前没有任何写入路径**（首轮"来路待查"到此为止）：3.1 的霸体验收只需覆盖 `Armor` / `Unbreakable` 两个值。若仍要保留这个取值，需在编辑器里确认它是否曾被从 `FirstSkillb` 上删掉（该序列里 `AN_ChangeAttackBox` / `AN_SpawnAttacker` 等通知都在，独缺 `AN_ChangeState`）。

#### 2.0.4 三处口径修正（2.7 / 3.1 / 3.3 / 3.4 开工前先读）

① **三个接口的现行端别与设计 / 笔记的记载不一致**。调查报告写 `I_ChangeAttack` / `I_ChangeState` / `I_MakeMove` 都是**权威时**才执行（`C_Character.cpp` 的 `ChangeAttack` / `ChangeState` / `MakeMove` 内部确实没有判定），也就是**客户端现在这三条都不执行**；而设计 2.4.4、5.9a 与协同笔记把这三者记作"两端各自执行"。两处不可能同时成立。影响：

- 2.7 的"时机类 = 两端都执行"对这三条而言，**客户端那一半现在并不存在**——它不是从现有实现里"拆"出来的，而是随预测接入**新加**的（3.1 / 3.3 / 3.4 各自的本地先行）；
- 3.3 的"删掉蓝图里现有的段号写入"要先确认客户端究竟写过没有；
- 2.7 与 3.1 里"本地霸体先被表覆盖一次再由服务器通知恢复"这条已知代价，**只在接入后才成立**。

**动作**：2.7 开工第一步在编辑器里打开 `BP_Character` 的这三个接口函数确认一次（其余 16 个不受影响）。这是 2.0 留下的唯一未闭环项。

**闭环（2026-10-10，开发者口径）**：蓝图侧以 `BlueprintSideCallSiteInventory.md` 的说明为准、且**逐字成立**——写了"权威"就只有权威分支、写了"本地控制"就只有本地控制分支、什么都没写就是没有判定，描述的调用过程就是实际调用过程。→ 本项以**调查报告为准**：这三条今天**客户端都不执行**。推论（同样适用于其余标"权威"的时机类）——`I_StartPreInput` / `I_LockTargetToward` / `I_ChangeDamageValue` / `I_StartHitCheck` / `I_SetGrab` / `I_StopGrab` 的客户端那一半**同样不是从现有实现里"拆"出来的，而是一律随预测接入新加**，各自的落点见 2.7 的判定表。

设计 2.4.4 / 5.9a 与协同笔记里"两端各自执行"的措辞按此**更正**——更正的是**现状描述**，不是目标：时机类的目标仍是"两端都执行"，只是客户端那一半按本节的分期随切片到达。3.3 的"删掉蓝图里现有的段号写入"据此改写为"蓝图里本来就没有客户端那一半的段号写入，客户端那一半是新加的"（见 2.7 变更表）。

② **`PS.CharacterState` 整条按属性预测接入**（裁决，见"已定事项"22）：它的取值里有四个是状态机的状态（`Staggered` / `Launched` / `Grabbed` / `Protected`，GDD 4.6.2），但**没有一个是客户端输入触发的**——霸体族来自 `AN_ChangeState`、受击 / 保护族来自服务器、`Normal` 来自收招。所以它不建键、不绑生命周期，一律"写标记 + 随权威值表采用 / 回滚"。**对 2.7 的直接含义**：`AN_ChangeState` 这一路在客户端接上时只写标记（时机类的通用做法），不要为它建键；本次盘点涉及的正是它写的那一族（霸体）。

③ **`Adamantine` 无写入路径**，见 2.0.3 末段——3.1 的霸体验收范围据此收窄。

### 2.1 组件挂载、上下文初始化、超时接入

- 内容：`AC_Character` 构造函数 `CreateDefaultSubobject`；`BeginPlay` 调 `InitializePredictionContext`（失败可重试）；`Tick` 中调 `TickPredictionTimeout`；建 `Prediction.Log` / `Draw` / `Enabled`。
- 落点：`C_Character` 构造函数、`BeginPlay`、`Tick`（**必须放在 `C_Character.cpp:456-459` 的 PS / GameState 空指针早退之后**，否则数据未就绪期间超时检查静默停摆，见设计 5.7）。
- 验收：屏上看到上下文有效、权威值表组件已绑定（**后半句见已定事项 23：挂载在 2.4，本项跑完时绘制显示 `authority: no`**）；键表与记录表恒为空；跑一局无 Warning 刷屏（"键不存在"一类竞态本就不该记 Warning，见设计 3.4.5）。

**已落地（2026-10-09）**，编译通过（`NarutoEditor Win64 Development`，0 警告），实机验收通过（上下文有效、四张计数表恒空、无 Warning 刷屏）。

对原项目的修改——两个现有文件，共 +43 行，无删除、无签名改动：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_Character.h` | `:17` | 前向声明 `class UC_PredictionComponent;` |
| | `:47-50` | 新增 `virtual void BeginPlay() override;`（原类没有覆写 BeginPlay） |
| | `:76-79` | 新增 `UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UC_PredictionComponent> PredictionComponent;` |
| `C_Character.cpp` | `:22` | `#include "C_PredictionComponent.h"` |
| | `:66-67` | 构造函数末尾 `CreateDefaultSubobject<UC_PredictionComponent>(TEXT("PredictionComponent"))`，不 `SetupAttachment`（设计 3.2） |
| | `:74-85` | 新增 `AC_Character::BeginPlay`：调 `InitializePredictionContext()`，不判返回值（客户端这一次多半失败，属正常中间态） |
| | `:478-492` | `Tick` 中，紧跟 PS / GameState 空指针早退之后：上下文无效则每帧重试一次初始化 → `TickPredictionTimeout(DeltaTime)` |

阶段一新建文件上的扩展（不是原项目内容，一并记录）：`C_PredictionComponent.h` 加 `#include "HAL/IConsoleManager.h"`、三个开关的 `extern` 声明（`:22-24`）、私有 `DrawPredictionDebug()`（`:333`）；`C_PredictionComponent.cpp` 加三个 cvar 定义（`:22 / :29 / :43`）与 `DrawPredictionDebug` 实现（`:721`），并在 `TickPredictionTimeout` 开头调用（`:818`）。设计 3.7 冻结表里的签名与语义未动：开关是文件级 cvar，绘制是私有函数。

### 2.2 既有问题修复（设计 5.8 全表）

- 内容：判空、直接调用改为走 RPC、补权限语义、`bPreInputLock` 的写入改由 C++ 接管（见已定事项 24）、`Tick` 里的 `Mult_ChangeGrabLocation` 明确触发条件。
- 落点：`C_Character.cpp` 的 `AddChakra`（设计 5.8 的旧行号 `:154-159`，2.1 之后下移为 `:171-176`）、`MyInitialize`（`:111`）、`ChangeAttack`（旧 `:185-205` → `:202-222`）、`ChangeState`（旧 `:207-210` → `:224-227`）、`MakeMove`（旧 `:212-228` → `:229-245`）、`Tick` 里的 `Mult_ChangeGrabLocation`（旧 `:510` → `:543`），以及 `ChangeAttack` 里对 `Server_ChangeToward_Implementation` 的直调（`:216-217`）。
- 注意：**只做与预测无关的修复**，不引入闸门、不写标记、不建键（设计 5.8 末注：单独提交、单独验证，不要与预测逻辑混在一起）。`Attack`（旧 `:274-277` → `:291-294`）本阶段只补判空，两道闸门留到切片 3.3。
- **蓝图侧动作（本包唯一一处，由开发者执行）**：`BP_Character` 的 `I_StartPreInput` 权威分支，把直接 `Set bPreInputLock` 的节点换成调用新增的 `AC_Character::StartPreInput()`；`TryTargetToward = 0` 一起挪进该函数，蓝图侧不再保留这两条写。顺序必须是"C++ 函数先落地并编译通过 → 再改蓝图"，否则蓝图找不到函数。`AN_PreInput` 通知与 `I_StartPreInput` 接口都保留。
- 验收：与修复前行为一致（除被修掉的错误路径）；蓝图改完后连段 / 预输入手感与改动前逐段一致——窗口位置一动没动（见已定事项 24）。

**已落地（2026-10-09）**，编译通过（`NarutoEditor Win64 Development`，0 警告），**实机验收通过**（开局朝向、连段与收招、技能 / 秘卷 / 通灵、抓取跟随、受击 / 击飞 / 保护各一次；预输入窗口位置与改动前一致）。蓝图侧那一处（`I_StartPreInput` 的 `Set bPreInputLock` 换成调用 `StartPreInput()`）由开发者执行完成。

对原项目的修改——两个现有文件，共 +52 行 / −8 行（其中 13 行是新增函数、8 行是头文件声明与注释），无删除函数、无签名改动：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_Character.h` | `:180-186` | 新增 `UFUNCTION(BlueprintCallable) void StartPreInput();`（已定事项 24） |
| `C_Character.cpp` | `:111-124` | `MyInitialize`：两处 `Server_ChangeToward_Implementation` → `Server_ChangeToward`（走 RPC）。本函数在客户端上跑（`OnRep_PlayerState` / `OnTeamChanged`），原来只改本地那一份 `Toward` |
| | `:173-182` | `AddChakra`：`Cast<AC_PlayerController>(Controller)` 判空后改走 `Server_ChangeChakra`。原来 `Controller` 为空会直接解引用空指针，且直调 `_Implementation` |
| | `:208-228` | `ChangeAttack`：加 `PS` 判空（原来第一行就写 `PS->Attack`）；两处 `Server_ChangeToward_Implementation` → `Server_ChangeToward` |
| | `:232-242` | `ChangeState`：加 `if (!HasAuthority())return;` 与 `PS` 判空（原来一行裸写 `GetPlayerState<AC_PlayerState>()->CharacterState = target;`） |
| | `:244-263` | `MakeMove`：加权限语义注释（"两端各自执行"），**无行为改动** |
| | `:266-277` | 新增 `AC_Character::StartPreInput()`：`bPreInputLock = false` + `TryTargetToward` 归零，即原蓝图那两条 Set |
| | `:461-470` | `Server_Attack_Implementation`：`PS` 判空提到解引用之前（原来 `:433` 先读 `PS->CharacterState`、`:434` 才判 `PS`）；随之去掉 `if (PS && …)` 里的冗余 `PS &&` |
| | `:576-581` | `Tick` 里 `Mult_ChangeGrabLocation`：把触发条件与频率写明确，抽出 `GrabLocation` 局部量。**无行为改动** |

三处原本会崩的空指针（`AddChakra` 的 `Controller`、`ChangeAttack` / `ChangeState` 的 `PS`）是 5.8 表"判空"的实际收益。

**5.8 表内本包未落地的行，及理由**：

| 行 | 处理 |
| --- | --- |
| `AC_Character::Attack` 的"本阶段只补判空"（旧 `:274-277` → `:291-294`） | 函数体只有一行 `Server_Attack()`，**没有可判空的对象**；这一行的判空落在它到达的 `Server_Attack_Implementation`（上表 `:461-470`），那里才是真正解引用 `PS` 的地方 |
| `AC_Character::Tick` 里 `Mult_ChangeGrabLocation` 的"建议只在抓取状态变化时调一次" | **未采纳该建议**：被抓角色要靠这条多播连续跟随会随抓取者移动的抓取点，改成"状态变化时一次"会让跟随失效。本包只把触发条件与频率写明确（设计原话是"建议"，不是定案）——**开发者 2026-10-09 裁定：按此执行，见已定事项 25** |
| `ChangeAttack` 的"段号写入点前移 / `attack == 0` 按端区分" | 属切片 3.3（段号写入点前移），本包按"只做与预测无关的修复"不动 |
| `Attack` 的两道闸门 | 属切片 3.3 |
| `MakeMove` 的"客户端先行 + `RecordMoveBaseline`" | 属 2.7 / 3.4（动画通知拆分与位移切片） |
| 服务器校验（P3）那行 | 属 2.3 |

### 2.3 服务器校验（设计 5.1）

- 内容：把客户端的判据原样搬到服务器——`Server_ChangeSkillState` / `Server_ChangeChakra` / `Server_ChangeAttackState` / `Server_ChangeCharacterState`。
- 落点：`C_PlayerController.cpp:67-73`（`Server_ChangeChakra`）与 `:133-155`（`Server_ChangeAttackState` / `Server_ChangeCharacterState` / `Server_ChangeSkillState`）。
- 验收：正常操作行为不变；用调试命令**故意在不可行时机发请求**（如 CD 未好、状态不是 `Normal`/`Protected`）→ 服务器拒绝且不写值。
- 说明：此刻还没有任何键，拒绝表现为"世界状态不变"，客户端看不到任何变化——这正是这一项的验收方式。

**已落地（2026-10-09）**，编译通过（`NarutoEditor Win64 Development`，0 警告）。落点里写的 `:67-73` / `:133-155` 是动手前的位置，改动后见下表。

对原项目的修改——三个现有文件，共 +81 行 / −6 行（其中 `C_PlayerController.cpp` 全部是本包改动，另两个文件里的其余行属 2.1 / 2.2），无删除函数、无签名改动：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_PlayerController.cpp` | `:67-87` | `Server_ChangeChakra`：裸赋值 → 三条判据（助增 +1 / 替身 −1 / 奥义清零），三条之外一律拒绝（已定事项 27） |
| | `:147-157` | `Server_ChangeAttackState`：**保持原样**，只加注释——本函数在工程里没有调用者（见下表） |
| | `:159-168` | `Server_ChangeCharacterState`：同样保持原样，只加注释 |
| | `:170-223` | `Server_ChangeSkillState`：状态判据（`Normal` / `Protected`，五个输入函数共有）+ 按取值判 CD / 查克拉（1 / 2 / 4 / 5），取值不在客户端请求集内一律拒绝；被接受时写下服务器侧的权威 CD 时间戳（已定事项 28） |
| `C_Character.h` | `:41-44` | 新增 `friend class AC_PlayerController;`——上一步要在 `C_Character` 的私有时间戳上落笔 |
| `C_Character.cpp` | `:381-384` | `FinalSkill`：两条 RPC 调序——技能请求排在查克拉清零之前（已定事项 27） |

阶段一新建文件上的扩展：`C_PredictionComponent.cpp:1350-1395` 新增两条调试命令 `Prediction.DebugSkill <1|2|4|5>` / `Prediction.DebugChakra <0..4>`（`FAutoConsoleCommandWithWorldAndArgs`，取本实例的本地 PlayerController，绕过客户端的本地先行判定直接发请求；2.6 的调试工具同放这里）。命令在敲它的那个实例里生效——局域网要测哪个客户端，就在哪个窗口的控制台里敲。

**验收步骤**：

1. 正常打一局：普攻 / 替身 / 一技能 / 二技能 / 秘卷 / 通灵 / 奥义各一次，行为与 2.2 之后一致（新增判据在正常路径上全部通过）。
2. 服务器拒绝：一技能还在 CD 时敲 `Prediction.DebugSkill 1` → 服务器拒绝，`MySkill` 不变、屏上无任何变化（此刻还没有键，拒绝只表现为"世界状态不变"）。
3. 奥义判据：查克拉不满时敲 `Prediction.DebugSkill 5` → 拒绝；再敲 `Prediction.DebugChakra 0` → 也拒绝（`MySkill` 不是 5），查克拉不掉——"被拒的奥义不白扣查克拉"。
4. 非法取值：`Prediction.DebugSkill 3` / `7` → 拒绝（不在客户端请求集内）。

**一处过渡期现象（2.4 落地后消失）**：客户端在输入函数里仍先写自己那一份 CD 时间戳。请求被服务器拒绝时，客户端这一份已经开始计时——冷却条会先亮起来，直到 2.4 的权威值表把服务器的值下发下来才被纠正。这是预期的，也正是下一条要接权威值表的原因。

**设计 5.1 点名的四条 RPC 里，本包只给两条加了判据**：

| RPC | 处理 |
| --- | --- |
| `Server_ChangeAttackState` | 工程里**没有任何调用者**：C++ 侧无调用点、也没有 `BlueprintCallable`，蓝图侧盘点（`BlueprintSideCallSiteInventory.md`）把 `BP_PlayerController` 标为"无关"。没有客户端判据可搬；段号写入点前移属 3.3，它的校验与去向届时一并处理（已定事项 29） |
| `Server_ChangeCharacterState` | 同样没有调用者；且 `CharacterState` 按已定事项 22 是服务器写、不由客户端输入触发，客户端本就无从发起这个请求。保留原样，等 3.x 决定删或改（已定事项 29） |

### 2.4 权威值表接入（设计 5.2 / 5.3）——**本阶段风险最高的一项**

- 内容：新增组件挂到 `AC_PlayerState` 与 `AC_Character`；删除被预测属性的逐属性复制注册；四个 CD 时间戳进表（**保持普通成员**，不加 `UPROPERTY`、不加 `Replicated`，这是权威值表相对逐属性复制省事的地方）；`BeginPlay` 把"最后一次收到的表"初始化为宿主当前值（初始复制"相等即不触发"的兜底，见设计 2.7.5）。
- 落点：`C_PlayerState.cpp:22-31`（删 `DOREPLIFETIME`，保留 `UPROPERTY` 声明）、`C_Character` 的复制注册（`Toward` / `LastEscapeTime`）。
- **必须保留**：`C_PlayerState.cpp:9` 的 `NetUpdateFrequency = 100.f`——它就是表的下发频率上限。
- 不要做：不给这些属性补 `ReplicatedUsing`、不用 `REPNOTIFY_Always`、不为它们写任何 `OnRep_*`（设计 5.2c）。
- 验收：
  1. 客户端打印表，与服务器打印真实属性，在各类操作后逐字段一致（含 `HealthValue` 的敌方那张表）；
  2. UI（血量 / 查克拉 / CD 倒计时）、动画状态机、移动拦截照常；
  3. 跑一局完整对战，无属性不同步。
- 回退：整项一笔提交，`revert` 即回到逐属性复制。

**已落地（2026-10-09）**，编译通过（`NarutoEditor Win64 Development`，0 警告）。落点里 `C_PlayerState.cpp:22-31` 是动手前的位置，改动后见下表。

对原项目的修改——四个现有文件，本包增量共 +56 行 / −14 行（`C_Character.*` 里的其余行属 2.1 / 2.2 / 2.3）；阶段一新建文件 `C_PredictionComponent.cpp` 另加 21 行（验收工具）。无删除函数、无签名改动：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_PlayerState.h` | `:12` | 前向声明 `UC_AuthorityValueComponent` |
| | `:59-63` | 新增组件成员 `UPROPERTY(VisibleAnywhere, BlueprintReadOnly)` |
| | `:79-96` | 五个被预测属性去掉 `Replicated` 说明符：`HealthValue` / `Chakra` / `Attack` / `MySkill` / `CharacterState`（已定事项 30） |
| `C_PlayerState.cpp` | `:10-15` | 构造函数：`NetUpdateFrequency = 100.f` 就地加注"必须保留"，新增 `CreateDefaultSubobject<UC_AuthorityValueComponent>` |
| | `:28-34` | 删五条 `DOREPLIFETIME`（`CharacterState` / `Attack` / `HealthValue` / `Chakra` / `MySkill`），`Team` 保留 |
| `C_Character.h` | `:18` / `:87-91` | 前向声明 + 组件成员 |
| | `:116-119` | `Toward` 去掉 `Replicated`（`BlueprintReadWrite` 保留） |
| | `:376-380` | `LastEscapeTime` 去掉 `Replicated`、改为 `UPROPERTY()`；四个 CD 时间戳**原样不动**（连 `UPROPERTY` 都没有，进表不需要动，设计 5.3） |
| `C_Character.cpp` | `:23` | 加 `#include "C_AuthorityValueComponent.h"` |
| | `:70-71` | 构造函数：紧随预测组件 `CreateDefaultSubobject<UC_AuthorityValueComponent>` |
| | `:205-210` | 删两条 `DOREPLIFETIME`（`Toward` / `LastEscapeTime`），函数体保留 |
| | `:129-138` | `MyInitialize` 末尾补一次 `InitializePredictionContext()`（已定事项 32） |

阶段一新建文件上的扩展：`C_PredictionComponent.cpp:782-832`——`Prediction.Draw` 的每条权威值表下面再加一行宿主的真实属性（`live`，`FColor::Cyan`），供下面验收 1 逐字段对照。**没有新增公开接口**：设计 3.7 冻结了两个组件的接口、扩展原则要求"必须新增接口时追加版本号"，而验收只需要把已有的私有绘制加两行（已定事项 31）。

**蓝图侧：本包不需要改蓝图。** 组件在 C++ 构造函数里创建，蓝图子类自动继承——编辑器里 `BP_Character` 的组件树会多出一个继承来的 `AuthorityValueComponent`（任何玩家状态 / 角色的蓝图子类同理）。

**验收**（对应上面三条）：

1. 分别在服务器窗口与客户端窗口执行 `Prediction.Draw 1`：
   - `refs:` 行的 `authority:` 三项应全为 `yes`（`selfPS` / `selfChar` / `enemyPS`）——2.1 顺延过来的那一条验收到此关闭（已定事项 23），此后不再出现"敌方 PS 没有权威值表组件"的 Warning；
   - 每条表下面那行 `live` 应与表逐字段相等（此刻无预测、无标记，表到达后恒走采用规则第二行）；
   - 把两个窗口的 `live` 行对起来看（含 `enemyPS` 那张表）：客户端 == 服务器。
2. UI（血量 / 查克拉 / CD 倒计时）、动画状态机、移动拦截照常：受击 / 替身 / 一技能 / 二技能 / 秘卷 / 通灵 / 奥义各一次，途中盯血条、查克拉条、五个冷却与朝向。
3. 跑一局完整对战（打到分出胜负或时间耗尽），全程无属性不同步。

**一处语义说明**：摘除之后，这批属性在客户端上的权威写者只剩表的采用规则；但客户端代码今天仍会在本地写其中几个（`ChangeAttack` 写 `PS->Attack` / `PS->MySkill`、`AC_PlayerController::PlayerGetDamage` 写 `CharacterState` 等，两端各自执行）。阶段二没有预测键，采用规则恒走第二行：这些本地写会在下一次表到达时被服务器的值覆盖——与摘除前的逐属性复制同一条件（引擎只在值变化时下发）。到阶段三给它们加上预测标记后，采用规则的第一行 / 第三行才开始起作用（设计 2.7.2）。

**缺陷与修复（2026-10-10，2.8 验收期间由开发者实机发现）——红方客户端不能转向**：

- **症状**：客户端为红方时角色不能转向（按反方向不发转向请求）；客户端为蓝方正常，服务器两侧都正常。
- **根因是两件事叠在一起，顺序由引擎定死**：
  1. 引擎对新复制过来的 Actor 的顺序是**先应用初始复制、再调 `OnRep`、最后才 `BeginPlay`**（`DataChannel.cpp:3223` `PostReceivedBunch` → `:3237` `PostNetInit`；`Actor.cpp:4124` 在 `PostNetInit` 里才 `DispatchBeginPlay`，该处注释原文是 "After all properties have been initialized, call PostNetInit"）。**设计 2.7.5 的假设反了**——它写"若服务器改过值……`OnRep` 会照常触发并覆盖这次初始化"，实际是那次"初始化"跑在覆盖之**后**。
  2. 于是客户端 `UC_AuthorityValueComponent::BeginPlay` 里"按宿主当前值填一遍"把**刚收到的服务器值覆盖成本地默认值**；而 `OnRep` 只在【变化】时来，那份权威值再也回不来。
  3. **为什么只有红方**：红方服务器侧 `Toward = false`，与类默认 `true` 不同（蓝方恰好等于默认，覆盖了也看不出来）。客户端 `Toward` 卡在 `true` 之后，`Move()`（`C_Character.cpp:334-335`）读本地 `Toward` 判"要不要发转向请求"，条件再也不成立——请求永远发不出去。
- **修法**（两处，都在阶段一 / 二新建的文件里；**不动原项目**）：客户端不写表 + 绑定即同步一次。

| 文件 | 改动 |
| --- | --- |
| `C_AuthorityValueComponent.cpp` | `RefreshTableFromHost` 开头加 `NM_Client` 早退。**"客户端从不写表"本来就是本组件的契约**（头文件原话），实现里只有 `BeginPlay` 这一处绕开了它；`BeginPlay` 的调用保留（服务器侧仍要——单独跑时没有 `PreReplication`，那一次就是表的初值） |
| `C_AuthorityValueComponent.h` | `RefreshTableFromHost` 的注释按上面改写（原注释把"客户端在 `BeginPlay` 里调用一次"当作设计的一部分） |
| `C_PredictionComponent.cpp` | `BindAuthorityValueHost` 绑完委托后补一次采用（仅 `NM_Client` 宿主）：表是 `ReplicatedUsing`、`OnRep` 只在变化时来，而客户端"首次到达"落在**委托还没绑**的空窗里（绑定在 `BeginPlay` 或其后每帧的重试里做）——值已在表里，只是没人把它写回本地属性。等价于把"订阅"补成"订阅 + 立即同步"。**服务器侧不做**：那里的表是从宿主刷出来的副本，反过来写会把宿主刚改过、还没刷进表的值滚回去（`MyInitialize` 紧接着调 `InitializePredictionContext` 就是这种时刻） |

- **不是"客户端也不该读表派生值"**：客户端读表派生值做判定是本设计的既有模式（`Move()` 读的 `PS->Attack` / `CharacterState` 同样是表派生值），通道修好、它们一起恢复；本次只修通道。
- **同一处影响的其余字段**：与 `Toward` 同表下发的一切在客户端上都受这一处影响（`LastEscapeTime`、四个 CD 时间戳、PS 段五个字段的**初值**）。平时看不出来，是因为要么等于类默认值、要么随后被"变化触发的 `OnRep`"纠正；`Toward` 是唯一"与默认值不同、之后又长期不变"的一格。
- **验收**：2.8 收口清单 A1（两端 `live` 行逐字段对齐，含 `enemyPS` 那张表）就是抓这一处的——**2.4 的验收 1 此前没有正式跑过**，这是它的第一次真正暴露。

### 2.5 锁更正通道（设计 5.5 / 5.6）

- 内容：`AC_Character::Client_CorrectLocks(Mask, Values)`；在服务器**每一处写锁的位置**下发（`Server_Attack` 接受时、`ChangeAttack` 连段结束、`PlayerStateReset`、`OnAttackBoxOverlap`）；**`Server_Attack_Implementation` 末尾（含拒绝路径）补发一次**；`bSuccessHit` 的复位点按设计 5.6 定案（复位点与读取点同处）。
- 落点：`C_Character.cpp:412-424`、`:185-205`、`:433-446`；`C_PlayerController.cpp:123-131`。
- 验收：客户端打印三个锁，在服务器写锁的每个场景（普攻被接受 / 被拒绝 / 连段结束 / 受击打断 / 命中）与服务器一致；**拒绝场景**（此时服务器没写锁）验证补发那一次确实让客户端锁复位——这是 2.6.1 的核心验收点，切片 3.3（普攻段）的闸门 1 直接依赖它。

**已落地（2026-10-10）**，编译通过（`NarutoEditor Win64 Development`，0 警告）。落点里写的 `:412-424` / `:185-205` / `:433-446` / `C_PlayerController.cpp:123-131` 是动手前的位置，改动后见下表。

对原项目的修改——三个现有文件，本包增量共 +100 行 / −4 行（`C_Character.h` / `C_Character.cpp` 里的其余行属 2.1–2.4）。无删除函数、无签名改动；四个删除行分别是 `ChangeAttack` 的提前 `return` 与 `Server_Attack_Implementation` 的两条早退，都改成了带补发/收口的写法：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_Character.h` | `:42-47` | 友元注释补一句：`PlayerStateReset` 写完锁之后也要走 `SendLockCorrection`（private） |
| | `:204-210` | 新增 `UFUNCTION(BlueprintCallable) ResetSuccessHit()`——设计 5.6 定案的复位点（已定事项 34） |
| | `:227-237` | 新增 `UFUNCTION(Client, Reliable) Client_CorrectLocks(uint8 LockMask, uint8 LockValues)`，位域按设计 2.8 |
| | `:415-432` | 私有：三个锁位常量（bit0 / bit1 / bit2）+ `LockBitsAttackRequest`（`Server_Attack` 三个出口统一发的两位，已定事项 33）+ `SendLockCorrection(uint8 LockMask)` |
| `C_Character.cpp` | `:223-249` | `ChangeAttack`：`attack == 0` 的提前 `return` 收口到函数末尾（语义不变），三个分支共用末尾那一次 `SendLockCorrection(LockBitAttackInputLock)`——与设计 2.8 调用点表的"连段结束 / 预输入被消费"两行对应 |
| | `:482-507` | `Server_Attack_Implementation`：两条早退改成"先补发再返回"的分支，函数末尾再加一次；三个出口统一发 `LockBitsAttackRequest`（已定事项 33） |
| | `:516-555` | 新增三函数：`SendLockCorrection`（按掩码读当前值打包下发，Verbose 日志带角色名）、`Client_CorrectLocks_Implementation`（只覆盖掩码覆盖的锁 + Verbose 日志带角色名）、`ResetSuccessHit`（`HasAuthority()` 守卫 → 置假 → 下发 bit2） |
| | `:556-569` | `OnAttackBoxOverlap`：`bSuccessHit = true` 之后下发 bit2（设计 2.8 的命中行） |
| `C_PlayerController.cpp` | `:142-146` | `PlayerStateReset`：`bAttackInputLock = false` 之后 `SendLockCorrection(LockBitAttackInputLock)`（设计 2.8 的受击打断行） |

阶段一新建文件上的扩展：`C_PredictionComponent.cpp:1412-1443` 新增验收工具 `Prediction.DebugLock [<AttackInputLock>] [<PreInputLock>] [<SuccessHit>]`（本地直接写锁、不发服务器，缺席的参数保持原值）。**为什么必须有它**：下面的验收 6 要求"拒绝场景验证补发那一次确实让客户端锁复位"，而客户端要到切片 3.3 才在输入瞬间本地置位（设计 2.6.1）——在那之前两端的锁恒等，补发的值恒等于客户端已有的值，无从观察。这条命令制造"客户端本地值 ≠ 服务器值"，是 2.5 验收的前提，3.3 之前也是唯一的办法（已定事项 35）。没有新增公开接口（设计 3.7 冻结的接口表不受影响）。

**验收期间发现并顺手修掉的一处日志噪声**（已定事项 36）：`Prediction.Log 1` 之下，`InitializePredictionContext: PlayerState or PlayerController is not available yet; call again once it arrives` 每秒刷满屏。机制本身是设计内的——客户端上 PlayerState / Controller 逐帧到达，`AC_Character::Tick` 在 `CanPredict()` 为假时每帧重试一次初始化（2.1 落地；这条日志是 Verbose，只有开了 `Prediction.Log 1` 才可见），**但有一个组件永远就绪不了**：客户端上的敌方角色是模拟代理，引擎不复制 `Controller`（远端 `PlayerController` 只发往拥有者连接），它的 `CanPredict()` 恒为假。重试无害（`InitializePredictionContext` 在绑定前就返回）且是设计 3.4.1 的形状，照旧保留；改的是日志——`C_PredictionComponent.cpp` 两处"还没就绪"改成**每个组件只记第一次**（新增成员 `bContextPendingLogged`，成功那一次重新武装），并带上角色名，下一次真出问题时一行就能定位是谁。**纯日志改动，重试行为与组件状态一字未动**。

**蓝图侧：本包需要一处蓝图改动**（设计 5.6 的定案）：`bSuccessHit` 的**写入者全工程只有两处**——`C_Character::OnAttackBoxOverlap`（服务器置真，本包已加下发）与蓝图 `BP_Character` 的 `I_StartHitCheck`（权威分支置假）。两处都得走更正通道，客户端那一份才不会漂：

| 蓝图 | 位置 | 改动 |
| --- | --- | --- |
| `BP_Character` | `I_StartHitCheck`：权威分支里那条 `Set SuccsessHit = false`（盘点 `BlueprintSideCallSiteInventory.md:31`，语义是"下一次攻击的命中检测开始时清掉上一次的命中"） | 换成调用 `ResetSuccessHit`（本包新增的 `BlueprintCallable`）；分支结构不动 |

`BP_Menma` 的 `I_HitJump`（盘点 `:54`：权威且 `successHit` 为真时把 `MySkill` 置 3、并调 `I_GiveChakra`）**只读不写**，本包不动它。

**设计 5.6 的一处前提需要更正**（已定事项 34）：5.6 写"复位点与读取点同处……那门技能的服务器判定逻辑消费掉它之后随即复位"，这句话的前提是"读取点会把它消费掉"，而开发者核对蓝图后的结论是读取点（`I_HitJump`）只判断、不写入。本包按 5.6 同一节里"**不引入新的复位时机**"这一条执行：保留工程既有的那个复位点（`I_StartHitCheck`），只把它接进更正通道——既不新增复位时机，也不让任何一个写者绕开通道。

**验收步骤**（局域网两窗口，其中一个为主机；下面对照的是"客户端窗口 `Prediction.Draw` 的 `locks:` 行 ←→ 服务器窗口日志里的 `SendLockCorrection: <角色名> mask ... values ...`"，日志带角色名，两个角色不会混）：

1. 两端都开 `Prediction.Log 1`（`LogPrediction` 抬到 Verbose）与 `Prediction.Draw 1`。
2. **普攻被接受**：客户端按一次普攻 → 服务器日志 `mask 3 values 3`（bit0 + bit1 都置真）→ 客户端 `locks:` 行 `bPreInputLock=1 bAttackInputLock=1`。
3. **连段结束**：让一段普攻走完（`AN_ChangeAttack` → `ChangeAttack(0)`）→ 服务器 `mask 1 values 0` → 客户端 `bAttackInputLock=0`。
4. **受击打断**：被对方打中（平推 / 击飞 / 抓取任一）→ `PlayerStateReset` 的 `mask 1 values 0` → 客户端 `bAttackInputLock=0`。
5. **命中**：己方攻击框碰到对方 → `OnAttackBoxOverlap` 的 `mask 4 values 4` → 客户端 `bSuccessHit=1`；蓝图那处改完之后，**下一次攻击开始**（`I_StartHitCheck` 的命中检测开始）→ `mask 4 values 0` → 客户端 `bSuccessHit=0`。这一段里 `BP_Menma` 的 `I_HitJump` 只读该标志、不写也不下发，别把它当成复位点。
6. **拒绝场景（2.6.1 的核心验收）**：
   1. 客户端敲 `Prediction.DebugLock 1`——只写本地：客户端 `bAttackInputLock=1`，服务器那一份还是假；
   2. 在服务器会拒绝的时机按普攻：最省事的办法是让对方把自己打成僵直 / 击飞（状态不是 `Normal` / `Protected`）再按普攻；
   3. 服务器日志出现 `mask 1 values 0`（拒绝路径的补发，客户端本地那一次置位被解除）→ 客户端 `locks:` 行 `bAttackInputLock` 回到 0；
   4. 随后立刻再按一次普攻：能正常出手（本地闸门 1 没有自锁）——这正是 3.3 依赖的那条性质。

**一处按设计保留的缺口**：`StartPreInput()`（`AN_PreInput` → `C_Character`，2.2 落地）也写 `bPreInputLock`（置假），而设计 2.8 的服务器调用点表里**没有它**。因此"打开预输入窗口"之后到下一次 `Server_Attack` 之间，客户端的 `bPreInputLock` 与服务器不一致（客户端停在真）。阶段二无影响：客户端在 2.7 之前不读这个锁（`ChangeAttack` 与两道闸门都在服务器侧跑）。2.7 里 `AN_PreInput` 两端执行、客户端自己把它置假，缺口自然消失。**按冻结的设计保持原样，不加下发点**——设计 2.8 的表是穷举，多一处就得多一条已定事项；留给 2.7 复核。

### 2.6 回执通道与调试工具（设计 2.11.1）

- 内容：`AC_PlayerController::Client_ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked)`（此刻暂无调用点）；`Prediction.ForceReject` / `Prediction.DropResolve`。
- 落点：`C_PlayerController.h / .cpp`，服务器侧拒绝开关放在 2.3 建好的校验分支上。
- 验收：用一条调试命令发送带键请求（键由本地临时构造），观察回执到达与 `ResolvePrediction` 的日志（此时键表为空，应走"静默返回"路径——顺带验证设计 3.4.5 的竞态约定）。

**已落地（2026-10-10）**，编译通过（`NarutoEditor Win64 Development`，0 警告）。UHT 接受了 `FPredictionKey` 作为 RPC 参数（重新生成了 4 个 `.gen.cpp`，其中含 `C_PlayerController.gen.cpp`）——这是本包动手前唯一的技术风险，已关闭。

对原项目的修改——三个现有文件，本包增量共 +98 行 / −14 行（这三个文件里其余行属 2.1–2.5）：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `C_PlayerController.h` | `:9` | 新增 `#include "C_PredictionComponent.h"`——`FPredictionKey` 按值传，UHT 生成的代码需要完整类型（该头只前向声明本类，不成环） |
| | `:60-67` | `Server_ChangeSkillState` 加键参数 `FPredictionKey PredictionKey`（设计 3.4.2 的挂载约束：全工程只有三个 RPC 可带键，这是其中之一） |
| | `:70-76` | 新增 `UFUNCTION(Client, Reliable) Client_ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked)`（设计 2.11.1，宿主是 PlayerController） |
| | `:78-81` | 新增 `SendPredictionResolve(const FPredictionKey& Key, uint8 Result)`——服务器侧唯一的回执出口（设计 2.11.4） |
| `C_PlayerController.cpp` | `:174-248` | `Server_ChangeSkillState_Implementation`：七条拒绝出口改成"先回执再返回"、接受出口在写完权威值之后回执；`Prediction.ForceReject` 落在校验分支上（带键且开关打开 → 记日志 + 回执 Rejected + 不写任何权威值） |
| | `:250-292` | 新增 `SendPredictionResolve`（两道闸门：`KeyID == 0` 与 `DropResolve`；打包 `(CharacterState << 4) \| (MySkill & 0x0F)`）与 `Client_ResolvePrediction_Implementation`（Verbose 日志带角色名 + 转交组件的 `ResolvePrediction`） |
| `C_Character.cpp` | `:368` / `:386` / `:404` / `:423` / `:441` | 五个技能输入函数的请求改为传空键 `FPredictionKey()`（每个调用点原一行被替换） |

阶段一新建文件上的扩展（三块，共 +81 行 / −2 行）：

- `C_PredictionComponent.h:27-35` + `C_PredictionComponent.cpp:50-86`：两条服务器侧开关 `Prediction.ForceReject` / `Prediction.DropResolve`，定义与声明整段包在 `#if !UE_BUILD_SHIPPING` 里（工具表的约定：这两个只在非 Shipping 构建里编译）；读取走两个**恒定义**的查询函数 `IsPredictionForceRejectEnabled` / `IsPredictionDropResolveEnabled`（Shipping 里恒 false），所以各调用点不必自己写条件编译。
- `C_PredictionComponent.cpp:1465-1489`（`DebugRequestKeyedSkill`）：新增调试命令 `Prediction.DebugKeyedSkill <1|2|4|5>`——本地临时构造一个键（ID 从 900001 起数，与 `CreatePredictionKey` 的计数器互不干扰），**故意不建在键表里**，随 `Server_ChangeSkillState` 发出去。
- `C_PredictionComponent.cpp:1517-1521`：上面那条命令的注册；调试命令区的头注释同步改写。

**三处需要记下来的判断**（已定事项 37–39）：

1. **带键请求的调试路径复用 `Server_ChangeSkillState`，不新增调试专用 RPC**（已定事项 37）。设计 3.4.2 的挂载约束写着"只有三个 RPC 可以带键……**不为任何其他 RPC 新增预测键参数**"——为验收造一条调试 RPC 会同时违反这一条与设计 2.11.1 的接口面。于是把设计 3.4.2 指定的键参数**提前**落到这条 RPC 上（切片 3.1 本来就要做这一步），五个生产调用点一律传空键：服务器按设计 3.4.2 末行"`KeyID == 0` 按非预测请求处理"照常校验执行、不回执，行为与改造前逐位一致。原计划里"（此刻暂无调用点）"一句据此修正为"暂无**带键的**调用点"——回执接线的第一条真实路径就是这条技能分支；切片 3.1 只需把输入点的实参从空键换成 `GetActivePredictionKey()`。
2. **回执打包收在 `SendPredictionResolve` 内部**（已定事项 38）。设计 2.11.1 把打包方式写死了（高 4 位 `CharacterState`、低 4 位 `MySkill`），切片 3.1 / 3.2 / 3.3 三个调用点若各写一遍就是三处重复；收在一处后，调用点只剩"拒绝 / 确认"两态。函数契约写在头文件：必须在**写权威值之后**调用（确认时打包的就是被接受的那个状态）。
3. **两个开关只在非 Shipping 里编译，且 `ForceReject` 只作用于带键请求**（已定事项 39）。不带键的请求没有本地预测可回滚，拒掉它只会让开关打开时玩不了（工具表的原文就是"对**带键**请求一律回 `Rejected`"）；`DropResolve` 只挡回执，不挡任何写入。

**蓝图侧：本包不需要改蓝图。** `Server_ChangeSkillState` 不是 `BlueprintCallable`，盘点文档里没有任何蓝图调用它；改的是 C++ 签名，蓝图侧看不见。

**验收步骤**（客户端窗口 + 主机窗口，两端都 `Prediction.Log 1`）：

1. **通道通着**：客户端在 CD 已好、状态常态时敲 `Prediction.DebugKeyedSkill 1` → 客户端 `Prediction.DebugKeyedSkill: locally built key 900001 ...`；服务器 `SendPredictionResolve: ... key 900001 result 0 packed ...`；客户端 `Client_ResolvePrediction: ... key 900001 result 0 packed ...`，**之后没有任何结算 / 回滚日志**——键表为空，走的就是设计 3.4.5 的静默返回。`packed` 高 4 位应等于服务器的 `CharacterState`、低 4 位等于 `MySkill`（技能 1 被接受后 `MySkill = 1`）。
2. **拒绝也回执**：让一技能处于 CD 中再敲一次 → `result 1`，客户端同样收到、同样静默返回。
3. **`Prediction.ForceReject 1`**（在**服务器**窗口敲）→ 再敲 `Prediction.DebugKeyedSkill 1`（CD 已好也一样）→ 服务器日志 `Prediction.ForceReject: ... rejects the keyed request (skill 1) without writing anything`，`MySkill` 不变、世界状态不变，客户端仍收到 `result 1`。验证完 `Prediction.ForceReject 0`。
4. **`Prediction.DropResolve 1`**（服务器窗口）→ 敲一次 → 服务器 `Prediction.DropResolve: ... drops the resolve for key ...`，客户端**没有** `Client_ResolvePrediction`。验证完 `Prediction.DropResolve 0`。（超时兜底本身在切片 3.x 才有可观察对象：此刻没有键在键表里，`TickPredictionTimeout` 无事可做。）
5. **不带键的请求不受影响**：敲 `Prediction.DebugSkill 1` 或正常按键 → 服务器没有任何 `SendPredictionResolve` 日志（设计 2.11.4 第 2 行）。

**验收通过（2026-10-10，开发者实机）**。补一条操作提醒（不改代码）：`Prediction.DebugKeyedSkill` 那一行是命令自己打的 **Log 级**（默认可见），而 `SendPredictionResolve` 与 `Client_ResolvePrediction` 两条都是 **Verbose**——与 2.5 的 `SendLockCorrection` / `Client_CorrectLocks` 同级。只看到命令那一行、后面没有，最常见的原因是那一端的日志级别没抬起来：**两端窗口都要 `Prediction.Log 1`**（上面的验收步骤第 1 步就是这么写的，实机上容易只开一端）。

### 2.7 动画通知拆分（设计 5.9a）

- 内容：按 2.0 的清单，把通知切成**时机类**（连段推进 `AN_ChangeAttack`、位移 `AN_MakeMove`、碰撞框变更、**状态授予 `AN_ChangeState`**——目标是两端都执行）与**纯权威类**（特效、音效、纯表现开关——改 `HasAuthority()` 门控，服务器触发后多播分发）。`AN_ChangeState` 的写入**不进技能键**（设计 5.9a 的定案），且客户端那一半按 2.0.4② 只写标记；本地霸体被表覆盖一次再由服务器通知恢复，是已知代价（约一帧，`Net PktLag` 下可见），不要当成本项失败。
- **先读 2.0.4①**：时机类里 `AN_ChangeAttack` / `AN_ChangeState` / `AN_MakeMove` 这三条**现行是权威门控**（客户端那一半今天并不存在）。所以本项对这三位不是"把客户端那一半拆出来"，而是**补上**它——补上之后 3.1 / 3.3 / 3.4 的本地先行才有落点。开工第一步：在编辑器里打开 `BP_Character` 的这三个接口函数确认端别，确认结果回填 2.0.4①。
- 落点：**蓝图**——不是 19 个通知资产（2.0.1 已确认它们只把 `AN_*` 转发给 `BPI_Character`），而是 `BP_Character` 里的接口实现（`BPI_Character` 共 20 个函数，其中 **18 个有通知资产**、是本项的对象；另 2 个 `I_GiveChakra` / `I_MakeDamage` 没有通知、属属性预测，见 2.0.2 末表。例外：`I_HitJump` 在 `BP_Menma`）：`HasAuthority()` 门控加在这里，时机类的客户端先行也加在这里，一次改一处、18 条都在同一个图集里。顺带处理 `Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 这类"兼具表现与状态"的多播——表现部分按权威门控，状态部分必须保持两端一致。
- 验收：特效 / 音效不再"两端各播一次"；时机类在客户端提前执行后与服务器结果一致（位移、连段推进、霸体三方都对齐，客户端那一半由本项补齐）；判定标准只有一条——**"这个通知在客户端提前执行了，会不会让某个量进入服务器可能不同意的值？"**（设计 5.9a）。
- 说明：这一项改的是现有实现，开关管不到，必须单独提交、单独回归。

**已落地（2026-10-10）**——**全部是蓝图改动，由开发者执行完成**；无 C++ 改动、无编译、无新增 RPC。实机确认运行正常。

**一条口径更正（先看）**：本节计划原文有两处与本次实际执行不符，按 2.0.4① 的闭环（2026-10-10 开发者口径）与已定事项 40 更正：

1. "客户端那一半由本项补齐"**不成立**——除 `AN_PreInput` 外，时机类的客户端那一半都不是本项补的，而是随预测接入**新加**（2.0.4① 的闭环结论：现行全部是权威门控，客户端那一半一律是新加的）；
2. 验收原文里"时机类在客户端提前执行后与服务器结果一致（位移、连段推进、霸体三方都对齐）"因此**顺延到 3.1 / 3.3 / 3.4**——本项的验收是下面那四条。

**判定表**（本项的产物：18 个有通知的接口逐条定类，`I_GiveChakra` / `I_MakeDamage` 无通知、不在此表）：

| 接口 | 类别 | 现行 | 2.7 | 客户端那一半落在哪 |
| --- | --- | --- | --- | --- |
| `I_StartPreInput` | 时机类 | 权威 | **改** | **2.7 本项**（只补 `bPreInputLock`） |
| `I_ChangeAttack` | 时机类 | 权威 | 不动 | 3.3 |
| `I_ChangeState` | 时机类 | 权威 | 不动 | 3.1（只写标记、不建键，已定事项 22） |
| `I_MakeMove` / `I_LockTargetToward` | 时机类 | 权威 | 不动 | 3.4（`TargetToward` 是"锁瞬间的意图快照"，与位移同片） |
| `I_StartHitCheck` | 时机类 | 权威 | 不动 | 3.3 / 阶段四（`bSuccessHit` 的本地复位） |
| `I_ChangeDamageValue` | 时机类 | 权威 | 不动 | 阶段四（本地命中判定要读它写的伤害参数） |
| `I_SetGrab` / `I_StopGrab` | 时机类 | 权威 | 不动 | 阶段四（本地命中判定要拿 `MyGrabPoint` 传给受害方） |
| `I_HitJump`（`BP_Menma`） | 时机类 | 权威且 `successHit` | 不动 | 阶段四（写的是 `MySkill`，随命中预测） |
| `I_ChangeBox` | 时机类 | 本地控制（→ `ServerChangeBox` → `Mult_ChangeBoxSize`） | 不动 | 3.5（设计 5.9b 的碰撞框本地先行） |
| `I_ChangeGravity` | 时机类 | 无判定（本地物理量） | 不动 | 已在两端 ✓ |
| `I_PlaySound` | 纯权威类 | 无判定（两端各播一次） | **改** | —— |
| `I_CameraShake` | 纯权威类 | 无判定（两端各晃一次） | **改** | —— |
| `I_SpawnSE` / `I_SetOtherPauseState` / `I_SpawnAttacker` / `I_Summon` | 纯权威类 | 权威 | 不动（已合规） | —— |

`Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 本就是"服务器 Tick 触发 + `NetMulticast` 到各端"，设计 5.9a 那条"兼具表现与状态、不能当纯表现处理"的注**已满足**，本项不动。

**蓝图改动表**（三处，都在 `BP_Character` 的接口实现里）：

| 蓝图 | 位置 | 改动 | 为什么 |
| --- | --- | --- | --- |
| `BP_Character` | `I_PlaySound` | 新建一个 **Multicast 自定义事件**（`Replicates` = Multicast、`Reliable` 默认勾选），把原来的 `PlaySound2D` 连引脚设置一起搬进去；接口实现改成两分支——Authority → 调该事件（入参透传）、**Remote → 空着不接节点** | 纯权威类：客户端不再自行执行，由服务器触发、多播到各端本地播放（设计 5.9a / 已定事项 42） |
| | `I_CameraShake` | 同上：现有晃动节点搬进一个新的 Multicast 事件，Authority 分支调它、**Remote 分支空着** | 同上 |
| | `I_StartPreInput` | Authority 分支（调 `StartPreInput`）不动；**新增 Remote 分支**：`Set bPreInputLock = false` | 时机类的客户端那一半（本项唯一一处）。**远端不调 `StartPreInput()`**——它连 `TryTargetToward` 一起清零，而那个量在客户端是本地输入意图（`Move()` 每帧写它并上报服务器，`C_Character.cpp:320-322`；`C_Character.h:199-200` 的注释就是为此把门留给调用方） |

盘点文档对应三行（`BlueprintSideCallSiteInventory.md` 的 `I_PlaySound` / `I_CameraShake` / `I_StartPreInput`）已按改后流程回填。

**三处判断**（已定事项 40–42）：客户端那一半的分期与 2.0.4① 闭环（40）、分发机制用蓝图 Multicast 事件而非 C++ `Mult_*`（41）、音效/晃屏服从设计的权威门控口径（42——含"播放本来就是本地的、改的只是触发点"与那份"自己角色晚一个 RTT"的代价）。

**验收**：

1. **主判据（实机）**：一局对战，双方角色的音效与晃屏**各端各出一次、时机与改造前一致**——重点看客户端窗口：敌方角色的音效/晃屏照旧 ⇒ 多播那一跳通了（不通则客户端会直接少掉敌方角色的音效）。**已通过（2026-10-10，开发者实机运行正常）**。
2. **结构判据**：阶段二里"两端各播一次"与"服务器分发"在观感上不可区分（预测未接入时，自己的动画本来也是等复制的状态到了才跑），真正可观察的差异要等 3.1 的本地先行；本项在阶段二的取证靠第 1 条加"改后图里 Remote 分支为空"。
3. **`I_StartPreInput`**：客户端 `Prediction.Draw 1` 的 `locks:` 行——`AN_PreInput` 一过 `bPreInputLock` 就变 0 且与服务器一致（不再等下一次普攻 / 受击被 2.5 的更正通道拉回）。**阶段二无影响项**：客户端此时不读这个锁——2.5 那条"按设计保留的缺口"（预输入窗口打开后客户端锁与服务器不一致）就此消失。此项可在后续任意一局里顺带看一眼。
4. 本项无开关（改的是现有实现），`Prediction.Enabled` 0 / 1 都不参与。

### 2.8 阶段验收

- 一局完整对战：无预测、无回执，表 / 锁两条通道工作，行为与表现与改造前一致。
- `Prediction.Enabled` 0 / 1 两种状态行为一致（本阶段两者都等于"无预测"）。
- 2.0 的蓝图清单归档在 `BlueprintSideCallSiteInventory.md`（盘点已完成，见 2.0）。

**收口清单（2026-10-10 汇总）**——2.1–2.7 各自验收项的并集，照此跑一局（A / B 可同一局穿插，C 里 C1–C3 在同一局内做、C4 / C5 各另起一轮）。任何一项不对就先停下报告，本清单之外不做新改动。

准备：两窗口、其中一个监听服务器开局；**两端各敲** `Prediction.Log 1` 与 `Prediction.Draw 1`（2.5 / 2.6 的多数日志是 Verbose，只开一端会看成"没有后续"，见 2.6 末尾那条操作提醒）；`Prediction.Enabled` 保持默认 1。

**A. 三条通道**（表 = 2.1 + 2.4，锁 = 2.5，回执 = 2.6）。"服务器"指服务器窗口日志（`SendLockCorrection` / `SendPredictionResolve`），"客户端"指客户端窗口的日志与 `Prediction.Draw` 的 `locks:` 行。

| # | 操作 | 断言 | 出自 |
| --- | --- | --- | --- |
| A1 | 各类操作各来一次：受击 / 替身 / 一技能 / 二技能 / 秘卷 / 通灵 / 奥义、抓取、击飞 | `refs:` 行 `authority:` 三项全 `yes`；每条表下面那行 `live` 与表逐字段相等；**客户端 `live` 行 == 服务器 `live` 行**（含 `enemyPS` 那张表） | 2.1 / 2.4 验收 1（已定事项 23 在此关闭；**2026-10-10 那个红方不能转向的缺陷就是靠这一对照抓出来的**，见 2.4 末尾"缺陷与修复"） |
| A2 | 全程盯 `Prediction.Draw` 的键表 / 记录表 | 恒空（本阶段无预测） | 2.1 |
| A3 | 客户端按一次普攻 | 服务器 `mask 3 values 3`；客户端 `bPreInputLock=1 bAttackInputLock=1` | 2.5 步骤 2 |
| A4 | 让一段普攻走完（`AN_ChangeAttack` → `ChangeAttack(0)`） | 服务器 `mask 1 values 0`；客户端 `bAttackInputLock=0` | 2.5 步骤 3 |
| A5 | 被对方打断（平推 / 击飞 / 抓取任一） | 服务器 `mask 1 values 0`（`PlayerStateReset`） | 2.5 步骤 4 |
| A6 | 己方攻击框碰到对方，随后开始下一次攻击 | 先 `mask 4 values 4` → 客户端 `bSuccessHit=1`；下一次攻击开始（`I_StartHitCheck`）→ `mask 4 values 0` | 2.5 步骤 5 |
| A7 | **拒绝场景**：客户端敲 `Prediction.DebugLock 1`（只写本地）→ 让对方把自己打成僵直 / 击飞 → 按普攻 → 再立刻按一次 | 服务器 `mask 1 values 0`（拒绝路径的补发）→ 客户端 `bAttackInputLock=0`；**随后那次普攻能正常出手** | 2.5 步骤 6（3.3 闸门 1 直接依赖这条） |
| A8 | 客户端 CD 已好、状态常态时敲 `Prediction.DebugKeyedSkill 1` | 客户端 `locally built key 900001 ...`；服务器 `SendPredictionResolve: ... result 0 packed ...`；客户端 `Client_ResolvePrediction: ...`，**之后无任何结算 / 回滚日志**（键表为空，走 3.4.5 静默返回）；`packed` 高 4 位 = 服务器 `CharacterState`、低 4 位 = `MySkill` | 2.6 步骤 1 |
| A9 | 让一技能处于 CD 中再敲一次 | 两端同样两条日志，`result 1` | 2.6 步骤 2 |

**B. 行为与表现**（2.1 / 2.2 / 2.4 / 2.7）

| # | 操作 | 断言 | 出自 |
| --- | --- | --- | --- |
| B1 | 一整局内所有动作来一遍（普攻连段 / 替身 / 技能 / 秘卷 / 通灵 / 奥义 / 抓取 / 受击 / 击飞 / 保护） | 手感与改造前逐段一致（预输入窗口位置一动没动）；血条 / 查克拉条 / 五个冷却、动画状态机、移动拦截照常 | 2.2 / 2.4 验收 2 |
| B2 | 打到分出胜负或时间耗尽 | 无属性不同步；无 Warning 刷屏 | 2.4 验收 3 / 2.1 |
| B3 | 客户端窗口里听自己与敌方角色的音效、看晃屏 | 照旧。**本阶段与改造前不可区分**——两侧动画本来都随服务器状态走，多播只把触发点挪到服务器；判别点在 3.1 之后（已定事项 42） | 2.7 验收 1 |
| B4 | 顺带盯 `locks:` 行的 `bPreInputLock` | `AN_PreInput` 一过就变 0，与服务器一致（2.5 那条"按设计保留的缺口"已消失） | 2.7 验收 3 |

**C. 开关与压测**

| # | 操作 | 断言 | 出自 |
| --- | --- | --- | --- |
| C1 | 服务器敲 `Prediction.ForceReject 1` → 客户端敲 `Prediction.DebugKeyedSkill 1` → 归 0 | 服务器 `... rejects the keyed request (skill 1) without writing anything`；`MySkill` 与世界状态不变；客户端仍收到 `result 1` | 2.6 步骤 3 |
| C2 | 服务器敲 `Prediction.DropResolve 1` → 敲一次 → 归 0 | 服务器 `... drops the resolve for key ...`；客户端**没有** `Client_ResolvePrediction` | 2.6 步骤 4 |
| C3 | 不带键：`Prediction.DebugSkill 1`（或正常按键） | 服务器没有任何 `SendPredictionResolve` | 2.6 步骤 5 |
| C4 | `Prediction.Enabled 0` 再跑一轮（默认 1 跑完之后的第二轮） | 行为逐位一致。开关只管预测接入点（本阶段还没有），**表 / 锁 / 回执三条通道与 2.7 的表现改动都不受它管**——0 / 1 的差别要等 3.1 才出现 | 2.8 第二条 |
| C5 | 两端各敲 `Net PktLag=100`，再跑一轮完整对战 | 表 / 锁 / 回执照旧工作；无卡输入、无残留；音效 / 晃屏的时点与改造前一致。测完 `Net PktLag 0`（想更狠可叠 `Net PktLoss=2`） | 工具表"阶段二起" |

**验收通过（2026-10-10，开发者实机）**——清单跑完，含 2.4 缺陷修复后的红方复跑。**阶段二就此收口**：表 / 锁 / 回执三条通道就位，行为与表现与改造前一致，`Prediction.Enabled` 0 / 1 行为一致——与"阶段总览"里阶段二那一行"结束时的状态"相符。

两条记录上的交代：**2.3 与 2.4 此前只有"已落地、编译通过"，没有单独的验收记录**（2.4 的验收 1 甚至没正式跑过——红方那一处缺陷就是它的第一次真正暴露）；两项的验收项都由本清单关闭。阶段二期间落在工作区的改动没有别的未验项：2.4 末尾那处修复（已定事项 43）是 2.8 清单之后唯一的阶段二改动，已在红方复跑中确认。

**下一项**：阶段三切片 **3.1（技能一）**（见 3.1 章节与已定事项 40 的落点表）。

---

## 阶段三：逐功能切片（第一批）

### 3.0 每个切片的固定动作

1. **输入点**：`IsLocallyControlled()` + `CanPredict()` + 本片开关 → 建键 → 本地先行写值 / 置锁 → 写标记或记录 → 发**带键** RPC（设计 3.4.2 只允许三个 RPC 带键）。
2. **服务器**：按 2.3 搬好的判据校验 → 执行 → `Client_ResolvePrediction(KeyID, Result, ConfirmedStatePacked)`；`Prediction.ForceReject` 打开时按拒绝走。
3. **客户端**：回执 → `ResolvePrediction`；表到达 → `ApplyAuthorityValueTable`；多播到达 → `OnMulticastArrived`。
4. **降级**：`CanPredict() == false` 或本片开关关闭 → 完全走原路径（不建键、不标记、直接发原 RPC）。
5. **验收三连**（每片都要跑）：**正常**（预测生效、无回滚）→ **强制拒绝**（本地先行后弹回、无残留标记、无卡输入）→ **丢回执**（2.0s 后超时回滚）。再叠一次 `Net PktLag=100` 重跑。
6. **一笔提交 = 一个切片**，提交信息写清开关名与三连验收结论。

### 3.1 切片 Skill（技能）——先做"技能一"

**为什么先做它**：一次输入一个键，无段、无锁、无位置（技能位移留到 3.4），是最薄的一条全链路——用它验证"建键 → 标记 → 带键 RPC → 回执 → 结算 → 表采用"整条管线。

- **范围**：先只做技能一（`MySkill = 1`），跑通后横向铺到技能二 / 奥义 / 秘卷与通灵（`4`，秘卷与通灵同为 `4`，按同一状态处理，`SummonIndex` 不进键判据，见设计 2.4.4）。
- **落点**：`FirstSkill`（`C_Character.cpp:286-302`）、`Server_ChangeSkillState`（`C_PlayerController.cpp:149-155`）加 `FPredictionKey` 参数 + 回执。
- **本地先行**：`Self.PS.MySkill`、`Self.PS.Chakra`（奥义）、`Self.Char.LastFirstSkillTime`（用 `GameState->GetServerWorldTimeSeconds()` 估算）；表现记录（技能动画、特效）。**不含 `Self.PS.CharacterState`**——技能输入不写它（写它的只有 `AN_ChangeState`，归 2.7），整条按属性预测接入，见"已定事项"22 与 2.0.4②。
- **冻结**：`BindStateLifecycle(PK, "Self.PS.MySkill", 0)`——技能结束时自动冻结。
- **已知差异**：CD 时间戳的本地值是"服务器世界时间估算值"，与服务器精确值不等，要等下一张表才写回（设计 2.7.2 竞态 1）。验收以"CD 状态（能否再次释放）服从服务器权威"为准，**不要求时间戳逐位相等**。
- **奥义**：这次输入**和别的技能一样会创建预测键**（本地先行里已含 `MySkill` / `Chakra`，键由本次输入创建）。要注意的不是"奥义没键"，而是 **`Server_ChangeChakra(0)` 这条 RPC 不带键**——设计 3.4.2 只允许三个 RPC 带键，且禁止同一次输入发两条带键 RPC。所以本地预扣的 `Chakra` 标记登记在**本次输入的那个键**下，由该键的回执结算清除（Confirmed 清标记、不写回；Rejected 由下一张表写回）。服务器若没扣 Chakra，下一张表按"无标记 → 写回本地"把它拉回权威值。验收里专门加一条"奥义被拒后 Chakra 回到服务器值"。
- **不进本键的东西**：技能霸体（`AN_ChangeState` 写的 `Armor` / `Unbreakable`）**不挂在技能键下**——归 2.7 的通知拆分（设计 5.9a 的定案，客户端那一半按 2.0.4② 只写标记、不建键）。本地霸体被表覆盖一次、再由服务器自己的通知恢复，是已知代价（约一帧）——注意这条**接入后才成立**（见 2.0.4①），验收时不要当成本切片的失败。

### 3.2 切片 Escape（替身）

- **前置：本片的第一步就是在客户端确认落点计算所需量齐备**（设计 5.4 的本地判定 + 本地瞬移：`LastEscapeTime` 在表里、落点用本机 `PlaceMark` 计算）。若某个量最终不可得，本切片**降级为不预测瞬移**，只保留服务器权威路径，不要为预测临时新增数据通道（设计 5.4 注）。
- **落点**：`Escape`（`C_Character.cpp:279-284`）、`Server_Escape`（`:374-408`）加 `FPredictionKey` 参数 + 回执；`Server_Escape_Implementation` 里的瞬移逻辑要在客户端有对应先行路径。
- **本地先行**：`MySkill`、`Chakra`、`LastEscapeTime`、位置（`RecordMoveBaseline()` + `SetActorLocation`）、表现。
- **验收**：三连之外，重点看**位置回滚**——强制拒绝时位置必须回到基线，且用 `TeleportPhysics` 不被地形卡住（设计 3.4.5 末条）。

### 3.3 切片 Attack（普攻段）——最复杂，放在最后

- **前置**：2.5 锁通道（闸门 1 的实际依据，以及拒绝路径的补发）。
- **内容**：
  - 输入点加两道与服务器同源的闸门（`bAttackInputLock == false`；`CharacterState ∈ {Normal, Protected}`，对标 `C_Character.cpp:416-417`），通过后：本地写段号 → 置锁 → 建键 → `Server_Attack(PK)`；
  - **段号写入点从动画推进点前移到输入点**（设计 2.4.4 的核心改动）；
  - `AN_ChangeAttack` 改为**只切表现**：按本地 `PS->Attack` 驱动动画，**删掉蓝图里现有的段号写入**（否则输入点写一次、推进点又写一次）；
  - `AN_ChangeAttack` 的 `attack == 0`（收招）分支按端区分：服务器写 0；拥有者客户端写 0 **并置预测标记**（与输入点写段号同构）；其它端不写、等表；
  - `BindStateLifecycle(PK, "Self.PS.Attack", 0)` 作兜底（只覆盖收招、被打断这类没有下一次预测的结束路径）；
  - "被拒绝的一段"按设计 2.4.4 的既定代价处理，不额外加机制。
- **落点**：`Attack`（`C_Character.cpp:274-277`）、`Server_Attack`（`:412-424`）、`AN_ChangeAttack`（**蓝图**）、`Server_Attack_Implementation` 末尾的补发（2.5 已建）。
- **验收**：
  - 连段正常（本地先行、段号与服务器同语义）；
  - `Prediction.ForceReject 1` 下："多挥一下随即弹回"，`Attack` 回到权威值，**且本地锁被补发的更正解除——拒绝之后必须能立刻再按**（这是 2.6.1 的唯一验收点，也是本切片最容易挂的地方）；
  - 丢回执：2.0s 回滚，锁同样被下一次写锁 / 补发解除；
  - 收招：本地写 0 后不被"服务器还停在最后一段"的表拽回连段（采用规则第三行覆盖，设计 2.4.4）。

### 3.4 切片 Move（位移 / 位置）

- **内容**：`AN_MakeMove` 内本地先行 + `RecordMoveBaseline()`（设计 5.8 的 `MakeMove` 一行：明确"两端各自执行"的语义，客户端路径改由动画通知内先行）；技能位移与替身瞬移共用同一机制。
- **落点**：`MakeMove`（`C_Character.cpp:212-228`）、`AN_MakeMove`（**蓝图**）。
- **验收**：位移后位置正常；强制拒绝 → 位置回到基线；**连续两段位移分别成键、各自回滚**（一个键只允许一次位移，多段位移应拆成多个键，设计 3.4.3 的 `RecordMoveBaseline` 约定）。
- **说明**：本切片让"位置预测"独立于技能 / 替身切片存在——3.1 / 3.2 可以先不接位移，出问题时只关这一片。

### 3.5 切片 Box（碰撞框本地先行，设计 5.9b）

- **内容**：优先"派生"方案——碰撞框的尺寸 / 偏移 / 翻转由已复制状态派生（在 `Tick` 里按 `PS->CharacterState` / `Toward` 计算，或另立一组 `Replicated` 的碰撞框属性 + `OnRep` 应用）。做对了就有机会**整体删除** `Server_ChangeBox` / `Mult_ChangeBoxSize` 这对 RPC。
- **落点**：`Server_ChangeBox`（`C_Character.cpp:161-171`）、`Mult_ChangeBoxSize`（`:127-152`）、各技能输入点里的 `Server_ChangeBox` 调用（`:294 / :312 / :328 / :345 / :363`）、`Tick`。
- **验收**（设计给定的标准）：客户端在任意动画帧打印 `AttackBox` 的 `GetUnscaledBoxExtent()` 与 `GetRelativeLocation()`，与服务器同一帧一致（容差 0）。
- **说明**：本切片不依赖任何预测功能，可任意插队；但它是阶段四的前置。

### 3.6 切片间的隔离与组合

- **键不共享**：每个切片的键在各自输入点创建；同一时刻只允许一个活跃键（设计 2.3.2），换切片时先冻结旧键。
- **标记不共享**：同名属性被新键接管时按设计 2.7.2 的接管规则处理。真正的共享面只有 `MySkill` 一个——技能输入、替身输入、收招的 `AN_ChangeAttack(0)` 都会写它（收招同时也写 `CharacterState`，但按 2.0.4② 它不建键、只写标记，接管规则随属性预测走）——验收时要专门覆盖"技能进行中按普攻"这类交叉输入。
- **开关独立**：任一开关关闭后该切片完全退回原路径。
- **组合验收**：三个切片同时开，打一局，确认回滚只影响本键登记项、无交叉回滚。

---

## 阶段四：命中预测（第二批）

### 4.1 命中预测（设计 2.5.2）

- **前置**：3.5 完成——"只有本项完成后，才能启用本地命中判定"，在此之前命中整体降级为服务器判定。
- **内容**：`OnAttackBoxOverlap`（`C_Character.cpp:433-446`）加客户端本地判定分支；攻击方本地预判 `Enemy.PS.HealthValue` / `Enemy.PS.CharacterState`（`Enemy.` 前缀，回滚依据是敌方 PlayerState 上的那张表）；未命中的回滚表现。
- **范围界定**：敌方代理**只预测血量与状态，不预测位移**——敌方是模拟代理，位移由服务器权威下发，本地预测会与多播写入互相打架（设计 2.5.2）。
- **验收**：本地判到命中 → 敌方立刻掉血 / 僵直且无回滚；本地判到、服务器没判到（`ForceReject`）→ 敌方血量与状态弹回权威值、命中表现撤销；受击方（被预测方）不预测自身，表现仍等服务器复制。

### 4.2 命中表现的本地先行

- 命中派生动画 / 特效的本地先行 + 委托回滚（设计 2.5.3 的"本地表现"与"本地生成物"）。
- 依赖 4.1；验收同三连。

---

## 阶段五：全量验收与收尾

- **降级回归**：`Prediction.Enabled 0` 跑一局 → 必须与改造前一致（这是"预测系统可整体关闭"的最终验收，设计 2.12）。
- **全量**：全部开关打开，叠 `Net PktLag=100` / `Net PktLoss=2` 跑一局 → 无残留标记、无卡输入、无重复表现、超时兜底生效。
- **双端对照**：同一操作序列下，客户端与服务器的 `Attack` / `MySkill` / `CharacterState` / `Chakra` / 三个锁逐帧比对（借 `Prediction.Draw`）。
- **回填**：设计与实现的差异改回设计文档；`AICollaborativeNotes.md` 只补有裁决价值的条目（按既有约定）。
- **保留**：调试 cvar 留在非 Shipping 构建里，不删。

---

## 已定事项（原待定项，已裁决）

| # | 事项 | 定案 | 落在哪 |
| --- | --- | --- | --- |
| 1 | 逐片开关用 cvar 还是组件上的 `UPROPERTY` | **cvar**：零接口改动（不碰设计 3.7 冻结表）、可在 console 里逐片排查 | 工具与调试开关 |
| 2 | 组件访问宿主私有时戳：友元还是补访问器 | **加两行友元**（`UC_PredictionComponent` + `UC_AuthorityValueComponent`，零行为）；提交时机可并入 2.1 | 1.3 |
| 3 | 阶段一是否引入自动化测试 | **先不引入**：值得测的分支都在"有宿主、有 RPC"之后 | 1.4 |
| 4 | ~~奥义预扣 `Chakra` 不带键~~ | **撤销**：措辞有误——奥义输入照常创建预测键；"不带键"说的是 `Server_ChangeChakra` 这条 RPC。机制已定，不再是待定项 | 3.1 的"奥义"条 |
| 5 | 替身客户端本地瞬移所需数据是否齐备（设计 5.4） | **在 3.2 开工第一步就先确认**；不齐则该片降级为不预测瞬移 | 3.2 |
| 6 | 蓝图侧调用点清单（2.0） | **已盘完**：清单与分类进 2.0.2，产出归档 `BlueprintSideCallSiteInventory.md`。遗留一项待编辑器确认（三个接口的现行端别，见 2.0.4①） | 2.0 |
| 7 | `FPredictionRecord::EndTime` 记哪个时刻 | **冻结时刻**（`EndPredictionKey` 里写入）；设计 3.3.1 只给了字段、未说语义，该字段仅用于调试 | 3.3.1 / W1.2 |
| 8 | 日志用哪个分类 | **新建 `LogPrediction`**（`C_PredictionComponent.h` 声明、`.cpp` 定义），阶段二的 `Prediction.Log` 沿用它 | 工具与调试开关 |
| 9 | 汉字能不能进字符串字面量 | **不能**：字面量一律 ASCII 英文，汉字只进注释 —— 根因是 UBT 的 `/utf-8`，见"源文件编码约定" | 源文件编码约定 |
| 10 | `ApplyAuthorityValueTable(const FAuthorityValueTable&)` 只有表、没有宿主身份（3.7 冻结签名），而预测组件要绑三个宿主的表到达委托 | **靠表的地址认宿主**：表就在已绑定组件的 `AuthorityValueTable` 成员上、客户端只有那一份（设计 3.3），"到达的表是哪个组件的成员"与"宿主是谁"是同一件事。三个宿主共用同一入口，W1.8 直接 `BindUObject`（**必须直接绑**，不能换成会拷贝表的 lambda）；认不出来时记 Warning | 3.2 / 3.3 / 3.4.3 / W1.4 |
| 11 | `MarkReplicatedAttribute` 的忽略条件（设计 3.4.3 只写了 `KeyID == 0` / 名字为空） | **补三道守卫**：名字必须在表内字段集（设计 3.3.1 的字段 × 2.7.3 的前缀 = 16 个全名）、承载它的权威值表组件已绑定、键未结算 —— 三者缺一，标记都等不到权威值、进表就出不来（标记只由表到达或结算清除），还会用采用规则第一行把本地值永久挡住。**冻结但未结算的键照常接受标记**（设计没给这条限制，且冻结键仍在缓冲池里、仍会回滚） | 3.4.3 / 3.3 / W1.4 |
| 12 | `Self.PS.HealthValue` 在表里、但设计 2.7.3 说"没有 `Self.PS.HealthValue`预测" | **两件事不冲突**：字段照常在表里、照常走采用规则（永远命中第二行，因为没人标记它）。它是设计 5.2(b) 摘除血量逐属性复制之后，拥有者客户端拿到自身血量的唯一通道 —— "不预测"说的是不写标记，不是不采用 | 2.7.3 / 5.2 / W1.4 |
| 13 | `RecordMoveBaseline` 在"已捕获过基线"时返回什么（设计 3.4.3 只写了"无活跃键 → false"） | **返回 true，且不改写基线**：回滚要恢复的是本键位移**之前**的位置（第一次捕获的那个），改写会让第一段位移留在原地；返回 false 则等于让调用方跳过位移，而设计 2.9 的位移语句两端各自执行，跳过会造成本地动画与位置脱节再被校正拽回。Warning 照记 —— 多段位移本就该拆键，属于要暴露的接入错误 | 2.9 / 3.4.3 / W1.5 |
| 14 | 两个记录接口的边界（设计 3.4.4 只写了"无活跃键忽略"与"冻结后忽略"） | **空名字忽略 + Warning**（沿用 3.4.3 给 `MarkReplicatedAttribute` 的同一条规则）；**`StateName` 是权威值表内字段时只记 Warning、仍照常记录** —— 该走标记却在记录是非复制数据的用法错误，但记录进来回滚委托照常恢复，丢掉反而少一次恢复；**空委托不入委托表**（3.5 的"传空委托即可"），入表的两条列表都只收已绑定的 | 3.4.4 / 3.5 / W1.6 |
| 15 | `ConfirmedStatePacked`（2.11.1 说"用于客户端跟进时校正本地非复制状态变量"）在组件里没有出口 | **照 3.4.5 的步骤表执行，本组件不消费它**：Confirm 只做三件事（执行跟进委托、清非复制预测标记、清本键的可复制属性标记）。它出不去有两层原因：跟进委托按 3.5 是无参 `DECLARE_DELEGATE`（3.7 冻结），组件也无权写宿主的任意非复制变量。本包只把它记进 `LogPrediction` 的 Verbose 日志（调试可见）；真要消费得新增接口，按 3.7 的"扩展原则"追加版本号后再定 | 2.11.1 / 3.4.5 / W1.7 |
| 16 | `OnMulticastArrived(MulticastName)` 按名字清标记，但 3.4.5 给的示例是 `"GrabLocation" / "ProtectedAnim" / "Gravity" / "BoxSize"`（多播自己的数据名），而标记表的键是带前缀的属性名（2.7.3） | **按字面实现：拿传进来的名字直接在标记表里查/删，查不到静默返回**（设计明写"找不到对应标记时静默返回"）。因此接入时传的必须是 `MarkReplicatedAttribute` 用过的那个名字；示例里的数据名只是"多播叫什么"的说明。理由：组件手上没有也不该有"多播名 → 属性名"的映射 —— 5.10 明确不给多播加键，多播与预测的配对本来就只有名字这一条线 | 3.4.5 / 2.11.3 / 5.10 / W1.7 |
| 17 | 结算的收尾动作（设计 3.4.5 只说"键移出键表与缓冲池、记录保留"） | **收尾统一走内部 `FinishPredictionKey(KeyID, bConfirmed)`**，两条结算路径与超时路径共用。它多做两件设计没写但必须做的事：①**清掉该键的委托表条目** —— 3.5 只说"结算时取出执行"、没说保留，留着会让委托表随预测次数无限增长且存的都是失效绑定；②**`ActivePredictionKeyID` 归零** —— 超时可能落在**活跃**键上（回执丢失且没绑生命周期），那条路径不经过 `EndPredictionKey` | 3.5 / 3.4.5 / 2.3.2 / W1.7 |
| 18 | `TickPredictionTimeout` 读生命周期绑定的状态值要 `AC_PlayerState*`，而缓存它的 `InitializePredictionContext` 排在 W1.8 | **本包不依赖 W1.8：直接从宿主 Pawn 取**（`Cast<AC_Character>(GetOwner())->GetPlayerState<AC_PlayerState>()`，`APawn::PlayerState` 无复制条件，客户端同样有），拿不到就本轮跳过、下一帧再试。计划 1.2 约定包与包之间只有**编译期**依赖，这条依赖不是；顺带让 W1.7 的代码自足 —— 不接上下文也能跑。W1.8 落地后可改用它缓存的引用，语义相同 | 3.4.2 / 3.4.5 / W1.7 |
| 19 | `CanPredict` 的判据边界（3.4.1 写"组件已初始化、角色有效、PlayerState 有效"，3.2 的引用表里还有 Controller 与三个权威值表组件） | **判据 = 角色 + PlayerState + Controller 三者齐备，不含"权威值表组件是否已绑定"**。Controller 必须算进去：2.11.1 的回执走它、三个带键 RPC 之一也在它上面，缺了它连键都发不出去。权威值表组件**不算**：它没绑只影响该宿主的属性名，已由 3.4.3 的守卫按属性名拒绝并 Warning（已定事项 11）——把整片预测降级掉，比"少预测一个属性"的损失大得多 | 3.4.1 / 3.2 / W1.8 |
| 20 | `InitializePredictionContext` 的失败边界（3.4.1 只说"false 表示角色 / PlayerState / Controller 缺失"，没提敌方） | **那三项缺一即返回 false；敌方 PlayerState 与其权威值表组件缺失不算失败**（本次留空、仍返回 true）。敌方按定义就是"可能还没生成 / 还没同步"的一方，把它算进有效性会让客户端在对手进场前整片降级。日志分两档：**对手本身不在 → Verbose**（常态），**对手在、但它没挂权威值表组件 → Warning**（配置错误，混在一起会看不见） | 3.4.1 / 2.7.5 / W1.8 |
| 21 | `InitializePredictionContext` 可重复调用的保证（3.4.1 要求"PlayerState 后续到达可再次调用重试"） | **重绑即覆盖，重复调用安全** —— 已对照引擎源码核实：`TDelegate` 是单绑定（`DelegateBase.h:310-321` 的 `CreateDelegateInstance` 先析构旧实例、再就地构造新实例），所以重试不会累积绑定；UObject 版绑定持**弱引用**（`DelegateSignatureImpl.inl:500-503` 的注释明说），权威值表组件被销毁后不会悬空执行 | 3.4.1 / 3.2 / W1.8 |
| 22 | `PS.CharacterState` 算状态预测还是属性预测（设计 2.4.1 把它列进"可复制状态变量"，设计 2.5.1 / 2.5.2 又把它列进属性预测走权威值表的那两行） | **整条按属性预测**。它的取值里有四个确实是状态机的状态（GDD 4.6.2 的 BeAttacked / Protected 读 `MyCState = Staggered` / `Launched` / `Grabbed` / `Protected`），但**没有一个取值由客户端输入触发**——霸体族（`Armor` / `Unbreakable` / `Adamantine`，GDD 的硬体 / 金刚体 / 霸体）由动画通知 `AN_ChangeState` 写、受击 / 保护族由服务器写（受击路径 / `Server_Escape`）、`Normal` 由收招写。既然不由输入触发，它就不建键、不 `BindStateLifecycle`，一律"写标记 + 随权威值表采用 / 回滚"（与 `Chakra` 同路）。与状态预测的差别只剩回滚的观感：受击 / 保护那四个取值回滚时要跟着把动画状态机切回去（设计 2.4.3） | 2.4.1 / 2.5.1 / 2.5.2 / 2.0.4② / 3.1 / 3.3 / 阶段四 |
| 23 | 2.1 验收写"屏上看到…权威值表组件已绑定"，而挂载动作写在 2.4 与设计 5.2(a)——两处不可能同时成立 | **按计划字面：2.1 只挂预测组件，三个权威值表组件留到 2.4 挂**（计划自己定了"凡有歧义以设计文档为准"，设计 5.2(a) 把挂载与"属性改走表"放在同一节）。2.1 验收该条顺延到 2.4 一起走；2.4 之前 `Prediction.Draw` 显示 `authority: no`，且每次上下文初始化会报一条"敌方 PS 没有权威值表组件"的 Warning（真实配置缺失，2.4 挂上后消失）。这样 2.4 仍是"挂载 + 摘除逐属性复制"一整笔，`revert` 一笔回到原状 | 2.1 / 2.4 |
| 24 | `bPreInputLock` 的复位点：设计 5.8 的定案写"在 `AN_ChangeAttack` 的连段推进点置位/复位"，但蓝图 `I_StartPreInput` 置 `false` 的语义是**打开预输入窗口**（开窗后窗口内的一次输入由 `Server_Attack_Implementation` 置回 `true`，推进点 `AN_ChangeAttack` → `ChangeAttack` 消费它决定连段变换），它不是残留的复位 | **位置与语义都不动，只把写入者从蓝图搬进 C++**（开发者 2026-10-09 口径）：新增 `AC_Character::StartPreInput()`（`UFUNCTION(BlueprintCallable)`），函数体就是今天蓝图那两条（`bPreInputLock = false`、`TryTargetToward = 0`）；`BP_Character` 的 `I_StartPreInput` 权威分支改为调用它。**不在推进点加复位**——5.8 那句按"C++ 里必须有一个置 false 的位置、蓝图不能是唯一写者"落地：2.7 之后拥有者客户端也跑 `ChangeAttack`，而蓝图那条 authority 分支在客户端不执行，标志会永远卡在初值 `true`（`C_Character.h:185`）。`TryTargetToward` 非复制、不进预测系统（设计 1.1.2-06 / 2.9），随同搬入只是换实现位置，行为不变 | 5.8 / 2.2 / 2.7 |
| 25 | 设计 5.8 对 `Tick` 里 `Mult_ChangeGrabLocation` 的处理栏写"（建议只在抓取状态变化时调一次）"，而现行代码是**连续跟随**：被抓期间每帧比对"自己 vs 抓取点"，不一致就发一次多播把两端一起挪过去 | **不采纳那条建议，保持连续跟随**（开发者 2026-10-09 裁定"按给出的建议执行"，即沿用本包给出的建议）：抓取点随抓取者的动画移动，改成"状态变化时一次"会让被抓角色不再跟随、抓取表现直接失效。该多播是 `Reliable`，发送频率由"抓取点是否移动"决定——2.2 只把这个条件与频率写明确（`C_Character.cpp:576-581`），行为不变。要限频是另一个决定，暂不做 | 5.8 / 2.2 |
| 26 | 秘卷与通灵共用请求值 4（两者的 `MySkill` 都是 4、动画相同），服务器侧怎么分辨是哪一个 | **读 `Char->SummonIndex`**：`C_Character.cpp` 的两个输入函数在发请求前先发 `Server_SetSummonIndex(0 秘卷 / 1 通灵)`。这两条请求跨 Actor（角色 / PlayerController），设计 2.11.4 已说明跨 Actor 不保证保序——但服务器侧的 `I_Summon` 本来就要读 `SummonIndex` 决定生成哪一个，这个"先到"要求是原代码就有的，本包没有新增假设。CD 判据与时间戳写入按同一条件分支，两者不会分叉 | 5.1 / 2.11.4 / 2.3 |
| 27 | 奥义的服务器判据是"满查克拉"（`Chakra == 4`），而客户端 `FinalSkill` 原来把 `Server_ChangeChakra(0)` 排在 `Server_ChangeSkillState(5)` **之前**——照搬判据会让每一次奥义都被拒 | **客户端两条 RPC 调序**（技能请求在前、清零在后；同一 Actor 上的可靠 RPC，UE 保证按序到达，设计 2.11.4），**并在 `Server_ChangeChakra` 的清零路径上加一条 `PS->MySkill == 5`**：奥义请求本身被拒（状态不合法、或服务器侧查克拉已变）时，紧随其后的清零也一并拒绝——被拒的奥义不白扣查克拉。此刻还没有回滚（2.5 / 2.6），这条是拒绝路径唯一的兜底 | 5.1 / 2.11.4 / 2.3 |
| 28 | 四个 CD 时间戳的服务器侧写入算 2.3 还是 2.4（权威值表在 2.4 才建） | **放 2.3，与判据同包**：服务器自己的 CD 判据读的就是这四个时间戳，不写它们恒为 0、判据永远是"没在冷却"，刚加上的拒绝路径形同虚设；而且 2.4 的采用规则会把服务器那一份下发给客户端，服务器侧若是 0 就会**把客户端正在跑的冷却抹掉**。客户端在输入函数里也记一份，那是本地先行值，2.4 起被权威值纠正 | 5.1 / 5.3 / 2.3 / 2.4 |
| 29 | 设计 5.1 点名的四条 RPC 里，`Server_ChangeAttackState` / `Server_ChangeCharacterState` 在工程里没有任何调用者，没有客户端判据可搬 | **本包不动，只加注释**（写明为什么没有判据）。段号写入点前移属 3.3、`CharacterState` 按已定事项 22 由服务器写，两者的校验与去向都等到那时处理；现在凭空给它们编判据会锁死 3.3 的选择 | 5.1 / 2.3 / 3.3 |
| 30 | 摘除逐属性复制时，`Replicated` 说明符去不去掉——设计 5.2b 只对 `AC_Character` 写了"可一并去掉"，`AC_PlayerState` 那一栏只说"UPROPERTY 声明保留" | **两处都去掉**（共七个属性）。说明符留着而 `DOREPLIFETIME` 已删，等于"名义上复制、实际不注册"：读代码的人无从判断哪个是真的，也没法靠声明看出"这批属性只走表"。`UPROPERTY` 一律保留（反射 / GC / 蓝图读写照旧）；`Team` 的 `ReplicatedUsing = OnRep_Team` 与四个 CD 时间戳都不动 | 5.2b / 2.4 |
| 31 | 2.4 的验收 1 要"客户端打印表，与服务器打印真实属性"逐字段比，验收工具放哪 | **扩展已有的私有 `Prediction.Draw`**（`DrawPredictionDebug`，2.1 建的），每条表下面加一行宿主的 `live` 值。理由：设计 3.7 冻结了两个组件的接口、扩展原则写明"必须新增接口时追加版本号"，而 `Prediction.Draw` 是私有成员、加行不改任何签名；且"表 vs 真实属性"在同一屏、同一实例上直读，不用跨窗口手抄。也替 2.6 的调试工具定了去向——同放这里 | 2.4 / 3.7 / 2.1 |
| 32 | 敌方 PS 的绑定只在 `InitializePredictionContext` 里做，而 Tick 的重试门槛是 `CanPredict()`（只看自身三项）——自身到齐后不再重试，首次初始化若早于 `Team` 或早于对手 PS 出现，`EnemyPSAuthority` 会一直空着 | **在 `AC_Character::MyInitialize` 末尾补一次 `InitializePredictionContext()`**：该函数正是"PS 与 Team 都就绪"的时刻（服务器在 `SpawnPawnToPlayer` 里、客户端在 `OnRep_PlayerState` / `OnTeamChanged` 里），重复调用安全（重绑即覆盖，已定事项 21），不需要新接口。本工程的生成顺序（`AssignTeams` 先 `SetTeam` 再 `SpawnPawnToPlayer`，且要求两名玩家都在场）让两端都能在这一刻找齐对手；不补这一手的话，`Prediction.Draw` 的 `enemyPS` 会一直显示 `no`，阶段三的 `Enemy.PS.*` 预测也无从落地 | 3.4.1 / 2.1 / 2.4 |
| 33 | `Server_Attack` 拒绝路径的补发，掩码里给不给 `bit0`——服务器在拒绝路径上**并没有写** `bAttackInputLock` | **给**，三个出口统一 `bit0｜bit1`（`LockBitsAttackRequest`）。掩码的语义定为"本次更正**声明**了哪几个锁"，而不是"服务器刚写过哪几位"：拒绝路径没写锁，但那一次下发的正是"你本地那次置位无效"这个结论，不声明 bit0 就送不出去，本地闸门 1 会自锁（设计 2.6.1）。接受路径的 bit0 也由这一次一并覆盖——设计 2.8 把"接受时"与"末尾补发"列成两行，落点是同一个调用点 | 2.8 / 2.6.1 / 2.5 |
| 34 | `bSuccessHit` 的复位怎么落——C++ 里没有任何复位路径（设计 5.6 点明），而 5.6 又说"复位点与读取点同处" | **按开发者核对后的蓝图事实执行：写它的全工程只有两处——`OnAttackBoxOverlap` 置真（C++，本包已加下发）与 `BP_Character` 的 `I_StartHitCheck` 置假（权威分支）；读取点 `BP_Menma` 的 `I_HitJump` 只读不写**。所以 5.6 那句"读取点消费掉它之后随即复位"的前提不成立；本包按同节"**不引入新的复位时机**"保留既有复位点（`I_StartHitCheck`），新增 `AC_Character::ResetSuccessHit()`（`BlueprintCallable`，函数体自带 `HasAuthority()` 守卫）作为它的下发形态，蓝图把那条 `Set SuccsessHit = false` 换成它。让蓝图直接写 `bSuccessHit` 会绕开更正通道，客户端那一份永远停在真（5.6：客户端只经 `Client_CorrectLocks` 接收）；在 `I_HitJump` 之后另加复位等于新增复位时机，与 5.6 冲突，故不做。函数体不做"已经是假就跳过"的提前返回——重复下发幂等 | 5.6 / 2.8 / 2.5 |
| 35 | 2.5 的验收要求"拒绝场景验证补发让客户端锁复位"，但客户端要到 3.3 才在输入瞬间本地置位——此刻两端锁值恒等，补发不可观察（发下去的就是客户端已有的值） | **新增调试命令 `Prediction.DebugLock`**（本地写、不发服务器）制造"本地值 ≠ 服务器值"。它是 3.3 之前唯一能观察补发的办法；放 `C_PredictionComponent.cpp` 的调试命令区（已定事项 31 定下的去向），不新增公开接口，`Prediction.Log 1` 下与 `SendLockCorrection` / `Client_CorrectLocks` 两条 Verbose 日志（都带角色名）成对对照 | 2.5 / 2.6.1 / 3.3 |
| 36 | 客户端上的敌方角色（模拟代理）的预测组件永远拿不到 `Controller`，`CanPredict()` 恒为假，于是每帧重试一次初始化、每帧记一行 Verbose，`Prediction.Log 1` 之下刷满屏（开发者实机验收时提出） | **保留重试，把日志改成一次性**。引擎事实：`Controller` 不复制，远端 `PlayerController` 只发往拥有者连接（`bOnlyRelevantToOwner`）——所以客户端上非己方角色没有 Controller，这不是配置错。重试本身无害（`InitializePredictionContext` 在绑定前就返回）且"就绪后再调一次"正是设计 3.4.1 的形状，故不动；逐帧记 Verbose 会把真正要看的锁 / 回执行淹掉，故两处"还没就绪"的日志改成每个组件只记第一次（新增 `bContextPendingLogged`，成功时重新武装）并带上角色名。**给阶段三的一条提醒**：客户端上敌方角色的预测组件是惰性的，不要往它上面接预测（`Prediction.Draw` 也只画本地控制角色） | 3.4.1 / 3.2 / 2.1 / 2.5 |
| 37 | 2.6 的验收要"用一条调试命令发送**带键**请求"，但此刻没有任何 RPC 能带键（三个可带键的 RPC 都要到切片 3.x 才拿到键参数）——是造一条调试专用 Server RPC，还是提前落地设计里的键参数 | **复用 `Server_ChangeSkillState`，不新增调试 RPC**：设计 3.4.2 的挂载约束明写"只有三个 RPC 可以带键……**不为任何其他 RPC 新增预测键参数**"，造调试 RPC 同时违反它和设计 2.11.1 的接口面。把设计指定的键参数**提前**落到这条 RPC 上（3.1 本来就要做），五个生产调用点传空键 → 服务器按设计 3.4.2 末行"`KeyID == 0` 按非预测请求处理"照常执行、不回执，行为与改造前逐位一致。切片 3.1 只需把实参换成 `GetActivePredictionKey()` | 2.11.1 / 3.4.2 / 2.11.4 / 2.6 |
| 38 | `ConfirmedStatePacked` 的打包由谁做——设计写了公式（高 4 位 `CharacterState`、低 4 位 `MySkill`），但接受 / 拒绝两条路径与三个调用点各写一遍就是重复 | **收在 `SendPredictionResolve` 内部**（服务器侧唯一出口）。调用点只剩"拒绝 / 确认"两态；`KeyID == 0`（没有键可回执）与 `DropResolve` 两道闸门也在这里。契约写进头文件：必须在**写权威值之后**调用，确认时打包的才是被接受的那个状态 | 2.11.1 / 2.11.4 / 2.6 |
| 39 | `Prediction.ForceReject` / `DropResolve` 的编译范围与作用域（工具表的约定是"只在非 Shipping 构建里编译"） | **两条 cvar 的定义与声明整段包在 `#if !UE_BUILD_SHIPPING` 里**，读取走两个恒定义的查询函数（Shipping 里恒 false），调用点不写条件编译。`ForceReject` **只作用于带键请求**——不带键的请求没有本地预测可回滚，拒掉只会让开关打开时玩不了（工具表原文即"对**带键**请求一律回 `Rejected`"）；`DropResolve` 只挡回执、不挡任何写入 | 2.6 / 2.10.1 / 3.4.5 |
| 40 | 2.0.4① 留下的悬案（盘点文档说三个接口"权威时"才执行 vs 设计 2.4.4 / 5.9a 与笔记记作"两端各自执行"） | **以盘点文档为准**（开发者 2026-10-10 口径：写了"权威"就只有权威分支、写了"本地控制"就只有本地控制分支、什么都没写就是没有判定，描述的调用过程就是实际调用过程）。这三条**今天客户端都不执行**；推论同样适用于其余标"权威"的时机类——**客户端那一半一律是随预测接入新加，不是从现有实现里"拆"出来的**：`I_ChangeAttack` → 3.3、`I_ChangeState` → 3.1（只写标记、不建键，已定事项 22）、`I_MakeMove` / `I_LockTargetToward` → 3.4、`I_StartHitCheck` → 3.3 / 阶段四、`I_ChangeDamageValue` / `I_SetGrab` / `I_StopGrab` / `I_HitJump` → 阶段四、`I_ChangeBox` → 3.5。**2.7 自己补的只有 `AN_PreInput`（`I_StartPreInput`）的客户端那一半，且只补 `bPreInputLock`**——`TryTargetToward` 是服务器侧量（`Move()` 里它还是客户端的本地输入意图并逐次上报，`C_Character.cpp:320-322`），落到客户端会与本地意图打架。设计与笔记里"两端各自执行"的措辞按此更正为**现状描述错误、目标仍是两端都执行** | 2.0.4① / 2.4.4 / 5.9a / 2.7 |
| 41 | 纯权威表现（`I_PlaySound` / `I_CameraShake`）的分发机制：蓝图 Multicast 自定义事件，还是新增 C++ `Mult_*` | **蓝图 Multicast 自定义事件**（`Replicates` = Multicast，`Reliable` 默认勾选——引擎事实，开发者确认）：把现有播放 / 晃动节点**原样搬进**新事件（引脚设置不动，表现逐位不变）、改动落在同一个图集、一笔提交一笔回退、不引入编译。走 C++ `Mult_*` 则必须把那两个节点在 C++ 里重写一遍，容易与蓝图当前设置分叉。设计 5.10 的"4 条 `Mult_*` 不加键"不受影响（新事件不带键、也不属于那 4 条）。**代价**：蓝图事件不能从 C++ 调——将来若需要从 C++ 触发这两条分发，得改走 C++ `Mult_*` | 5.9a / 5.10 / 2.7 |
| 42 | 音效与摄像头晃动的触发口径：服从设计 5.9a 的"纯权威类"（服务器触发 + 多播分发），还是维持"各端随本地动画自行执行" | **服从设计**（开发者 2026-10-10 在被告知代价后维持原判）。澄清一句：**播放本来就是本地的**——`PlaySound2D` 与摄像头晃动只作用于本机，多播传的是**触发**不是声音本身；改的只是触发点从"各端自己的动画"挪到"服务器"。**代价**：触发要等一个网络来回——对**自己**角色的音效 / 晃屏，3.1 本地先行落地后从"零延迟"变成"一个 RTT"（阶段二两侧无差别：此时自己的动画本来也是等复制的 `MySkill` 到了才跑）；对**敌方**角色的音效基本无差（今天也是等复制的动画帧跑到通知才响）。局域网下单帧级、看不见；将来联网若觉得明显，升级路径是把它当"可预测表现"走 `RecordPresentation` 确认 / 回滚（设计 3.4.4 / 3.5），属阶段四。**不加门控的代价记在明处**：3.1 起，被拒绝 / 回滚的动作会留下一次假音效、假晃屏 | 5.9a / 3.4.4 / 3.5 / 2.7 |
| 43 | 客户端要不要在 `BeginPlay` 里给权威值表填初值（设计 2.7.5 的"初始复制'相等即不触发 OnRep'"兜底） | **不填**（2026-10-10 缺陷修复，症状见 2.4 末尾"缺陷与修复"）。引擎对新复制 Actor 的顺序是"初始复制 → `OnRep` → `BeginPlay`"，客户端在 `BeginPlay` 里填 = 把刚收到的权威值覆盖成本地默认值，而 `OnRep` 只在变化时来——权威值再也回不来（红方客户端 `Toward` 卡在 `true`，`Move()` 发出的转向请求因此永远不成立）。**代码侧两条**：`RefreshTableFromHost` 只允许权威侧写表（"客户端从不写表"是本组件头文件原有的话，实现只差这一处）；客户端那份表的初值改由 `UC_PredictionComponent::BindAuthorityValueHost` 的**绑定即同步一次**负责（表首次到达时委托还没绑，值在表里、没人写回本地属性）。另外，`OnRep` 被跳过本来也只发生在"收到的值 == 本地当前值"，那时表里已经是服务器的值——兜底从一开始就不必要 | 2.4 / 2.7.5 / 2.8 |

---

## 风险与回退总表

| 风险点 | 影响面 | 手段 |
| --- | --- | --- |
| 权威值表接入（2.4） | **全局**：表坏 = 属性不同步 | 单独一笔提交、逐字段比对验收、随时 `revert` |
| 段号写入点前移（3.3） | 普攻语义 | 开关管不到 → 单独提交；回归项就是连段与收招 |
| 动画通知拆分（2.7） | 表现触发 | 开关管不到 → 单独提交；回归项是"特效不再双播"与"位移 / 连段推进 / 霸体在两端的执行一致"（客户端那一半是**新加**的，见 2.0.4①） |
| 本地闸门与服务器判据不同源 | 出现"被拒绝的一段"、观感抖动 | 判据逐行对照（设计 2.4.4）；`ForceReject` 下逐片验收 |
| 拒绝路径下本地锁不复位 | **输入卡死** | 2.5 的补发验收；3.3 的三连验收必须覆盖"拒绝后能立刻再按" |
| 超时兜底失效 | 标记残留、本地值永久偏离 | `DropResolve` 验证 2.0s 回滚 |
| 结构改动与预测行为混在一笔提交 | 无法回退、无法定位 | 每个工作包 / 切片单独提交（第 0 节的"开关能退什么"表） |
