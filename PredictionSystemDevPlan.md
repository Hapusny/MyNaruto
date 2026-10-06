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

### 2.0 蓝图侧调用点盘点（调查项，先做）

`ChangeAttack` / `ChangeState` / `MakeMove` 三个 `BlueprintCallable` 函数在 C++ 里**没有任何调用点**（已核对：只有声明与实现），说明调用全部来自动画通知蓝图。接入前必须盘出：

- 哪些动画通知 / 蓝图调用了这三个函数、各自在什么时点；
- `BP_FirstSkillEffect` / `BP_SecondSkillEffect` / `BP_FinalSkillEffect` / `BP_SummonEffect` 这类纯表现事件挂在哪些资产上；
- 现有 `HasAuthority()` 判断与"两端都执行"的写法分别散落在哪些通知里（这是 2.7 通知拆分的输入）。

**首轮盘点（资产名搜索，已有结果）**：通知资产集中在 `Naruto/Content/Game/BP/Arena/Character/Base/AnimNotify/`——`AN_ChangeAttack`（连段推进）、`AN_MakeMove`（位移）、`AN_ChangeAttackBox`（碰撞框）、`AN_ChangeState`（状态授予）。挂载情况：

| 动画序列 | 挂的通知 | 写入的 `CharacterState` |
| --- | --- | --- |
| `Attack1`–`Attack5` | `AN_ChangeAttack` + `AN_MakeMove` | — |
| `FirstSkill` / `SecondSkill` | `AN_ChangeAttack` + `AN_ChangeState` | `Armor` |
| `FinalSkill`（奥义）/ `Summon` | `AN_ChangeAttack` + `AN_ChangeState` | `Unbreakable` |
| `FirstSkillb` | `AN_ChangeAttack`（有 `Adamantine`，**无 `AN_ChangeState`**） | 来路待查 |
| `Idle` / `Walk` | `AN_ChangeAttack` | — |

`AN_ChangeState` 调的是 `AC_Character::ChangeState`（`C_Character.cpp:207-210`，两端各自执行）——这是 `Armor` / `Unbreakable` / `Adamantine` 三个值在工程里的**唯一**写入路径（C++ 里无人写）。`FirstSkillb` 的 `Adamantine` 与各通知的 `HasAuthority()` 有无，**必须回编辑器逐个打开确认**——字符串搜索到此为止。

产出：一张"资产 → 通知 → 现在做了什么 → 应归哪一类"的清单，写进本节或笔记。**没有这张清单，2.7 与切片 3.3 / 3.4 都无法开工。**

### 2.1 组件挂载、上下文初始化、超时接入

- 内容：`AC_Character` 构造函数 `CreateDefaultSubobject`；`BeginPlay` 调 `InitializePredictionContext`（失败可重试）；`Tick` 中调 `TickPredictionTimeout`；建 `Prediction.Log` / `Draw` / `Enabled`。
- 落点：`C_Character` 构造函数、`BeginPlay`、`Tick`（**必须放在 `C_Character.cpp:456-459` 的 PS / GameState 空指针早退之后**，否则数据未就绪期间超时检查静默停摆，见设计 5.7）。
- 验收：屏上看到上下文有效、权威值表组件已绑定；键表与记录表恒为空；跑一局无 Warning 刷屏（"键不存在"一类竞态本就不该记 Warning，见设计 3.4.5）。

### 2.2 既有问题修复（设计 5.8 全表）

- 内容：判空、直接调用改为走 RPC、补权限语义、`bPreInputLock` 复位点改由 C++ 接管、`Tick` 里的 `Mult_ChangeGrabLocation` 明确触发条件。
- 落点：`C_Character.cpp:154-159`（`AddChakra`）、`MyInitialize`、`:185-205`（`ChangeAttack`）、`:207-210`（`ChangeState`）、`:212-228`（`MakeMove`）、`:510` 附近，以及 `ChangeAttack` 里对 `Server_ChangeToward_Implementation` 的直调。
- 注意：**只做与预测无关的修复**，不引入闸门、不写标记、不建键（设计 5.8 末注：单独提交、单独验证，不要与预测逻辑混在一起）。`Attack`（`:274-277`）本阶段只补判空，两道闸门留到切片 3.3。
- 验收：与修复前行为一致（除被修掉的错误路径）；`bPreInputLock` 的复位点要确认动画蓝图里旧的复位节点已移除，否则会出现"一处置位、两处复位"。

### 2.3 服务器校验（设计 5.1）

- 内容：把客户端的判据原样搬到服务器——`Server_ChangeSkillState` / `Server_ChangeChakra` / `Server_ChangeAttackState` / `Server_ChangeCharacterState`。
- 落点：`C_PlayerController.cpp:67-73`（`Server_ChangeChakra`）与 `:133-155`（`Server_ChangeAttackState` / `Server_ChangeCharacterState` / `Server_ChangeSkillState`）。
- 验收：正常操作行为不变；用调试命令**故意在不可行时机发请求**（如 CD 未好、状态不是 `Normal`/`Protected`）→ 服务器拒绝且不写值。
- 说明：此刻还没有任何键，拒绝表现为"世界状态不变"，客户端看不到任何变化——这正是这一项的验收方式。

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

### 2.5 锁更正通道（设计 5.5 / 5.6）

- 内容：`AC_Character::Client_CorrectLocks(Mask, Values)`；在服务器**每一处写锁的位置**下发（`Server_Attack` 接受时、`ChangeAttack` 连段结束、`PlayerStateReset`、`OnAttackBoxOverlap`）；**`Server_Attack_Implementation` 末尾（含拒绝路径）补发一次**；`bSuccessHit` 的复位点按设计 5.6 定案（复位点与读取点同处）。
- 落点：`C_Character.cpp:412-424`、`:185-205`、`:433-446`；`C_PlayerController.cpp:123-131`。
- 验收：客户端打印三个锁，在服务器写锁的每个场景（普攻被接受 / 被拒绝 / 连段结束 / 受击打断 / 命中）与服务器一致；**拒绝场景**（此时服务器没写锁）验证补发那一次确实让客户端锁复位——这是 2.6.1 的核心验收点，切片 3.3（普攻段）的闸门 1 直接依赖它。

### 2.6 回执通道与调试工具（设计 2.11.1）

- 内容：`AC_PlayerController::Client_ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked)`（此刻暂无调用点）；`Prediction.ForceReject` / `Prediction.DropResolve`。
- 落点：`C_PlayerController.h / .cpp`，服务器侧拒绝开关放在 2.3 建好的校验分支上。
- 验收：用一条调试命令发送带键请求（键由本地临时构造），观察回执到达与 `ResolvePrediction` 的日志（此时键表为空，应走"静默返回"路径——顺带验证设计 3.4.5 的竞态约定）。

### 2.7 动画通知拆分（设计 5.9a）

- 内容：按 2.0 的清单，把通知切成**时机类**（连段推进 `AN_ChangeAttack`、位移 `AN_MakeMove`、碰撞框变更、**状态授予 `AN_ChangeState`** —— 两端都执行）与**纯权威类**（特效、音效、纯表现开关 —— 改 `HasAuthority()` 门控，服务器触发后多播分发）。`AN_ChangeState` 的写入**不进技能键**（设计 5.9a 的定案）——本地霸体被表覆盖一次再由服务器通知恢复，是已知代价（约一帧，`Net PktLag` 下可见），不要当成本项失败。
- 落点：**蓝图**（动画通知图表），可能涉及 `Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 这类"兼具表现与状态"的多播——表现部分按权威门控，状态部分必须保持两端一致。
- 验收：特效 / 音效不再"两端各播一次"；位移与连段推进仍两端执行；判定标准只有一条——**"这个通知在客户端提前执行了，会不会让某个量进入服务器可能不同意的值？"**（设计 5.9a）。
- 说明：这一项改的是现有实现，开关管不到，必须单独提交、单独回归。

### 2.8 阶段验收

- 一局完整对战：无预测、无回执，表 / 锁两条通道工作，行为与表现与改造前一致。
- `Prediction.Enabled` 0 / 1 两种状态行为一致（本阶段两者都等于"无预测"）。
- 2.0 的蓝图清单归档进笔记。

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
- **本地先行**：`Self.PS.MySkill`、`Self.PS.CharacterState`、`Self.PS.Chakra`（奥义）、`Self.Char.LastFirstSkillTime`（用 `GameState->GetServerWorldTimeSeconds()` 估算）；表现记录（技能动画、特效）。
- **冻结**：`BindStateLifecycle(PK, "Self.PS.MySkill", 0)`——技能结束时自动冻结。
- **已知差异**：CD 时间戳的本地值是"服务器世界时间估算值"，与服务器精确值不等，要等下一张表才写回（设计 2.7.2 竞态 1）。验收以"CD 状态（能否再次释放）服从服务器权威"为准，**不要求时间戳逐位相等**。
- **奥义**：这次输入**和别的技能一样会创建预测键**（本地先行里已含 `MySkill` / `CharacterState` / `Chakra`，键由本次输入创建）。要注意的不是"奥义没键"，而是 **`Server_ChangeChakra(0)` 这条 RPC 不带键**——设计 3.4.2 只允许三个 RPC 带键，且禁止同一次输入发两条带键 RPC。所以本地预扣的 `Chakra` 标记登记在**本次输入的那个键**下，由该键的回执结算清除（Confirmed 清标记、不写回；Rejected 由下一张表写回）。服务器若没扣 Chakra，下一张表按"无标记 → 写回本地"把它拉回权威值。验收里专门加一条"奥义被拒后 Chakra 回到服务器值"。
- **不进本键的东西**：技能霸体（`AN_ChangeState` 写的 `Armor` / `Unbreakable`）**不挂在技能键下**——归 2.7 的通知拆分（设计 5.9a 的定案）。本地霸体被表覆盖一次、再由服务器自己的通知恢复，是已知代价（约一帧），验收时不要当成本切片的失败。

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
- **标记不共享**：同名属性被新键接管时按设计 2.7.2 的接管规则处理（普攻段与技能都会写 `MySkill` / `CharacterState`，切换时必然互相接管）——这是切片间唯一的"共享面"，验收时要专门覆盖"技能进行中按普攻"这类交叉输入。
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
| 6 | 蓝图侧调用点清单（2.0） | **必须先盘完再动 2.7 / 3.3 / 3.4**——它是这三处的输入 | 2.0 |
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

---

## 风险与回退总表

| 风险点 | 影响面 | 手段 |
| --- | --- | --- |
| 权威值表接入（2.4） | **全局**：表坏 = 属性不同步 | 单独一笔提交、逐字段比对验收、随时 `revert` |
| 段号写入点前移（3.3） | 普攻语义 | 开关管不到 → 单独提交；回归项就是连段与收招 |
| 动画通知拆分（2.7） | 表现触发 | 开关管不到 → 单独提交；回归项是"特效不再双播"与"位移仍两端执行" |
| 本地闸门与服务器判据不同源 | 出现"被拒绝的一段"、观感抖动 | 判据逐行对照（设计 2.4.4）；`ForceReject` 下逐片验收 |
| 拒绝路径下本地锁不复位 | **输入卡死** | 2.5 的补发验收；3.3 的三连验收必须覆盖"拒绝后能立刻再按" |
| 超时兜底失效 | 标记残留、本地值永久偏离 | `DropResolve` 验证 2.0s 回滚 |
| 结构改动与预测行为混在一笔提交 | 无法回退、无法定位 | 每个工作包 / 切片单独提交（第 0 节的"开关能退什么"表） |
