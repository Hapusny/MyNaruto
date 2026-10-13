# 蓝图

> （无关）代表无可能与预测系统有关联的蓝图部分  
> 文字描述即为实现流程，没写权威时就说明该处没有权威判定，其他同理

## Game/ 游戏目录

### BP/ 蓝图

#### Arena/ 决斗场

##### Character/ 角色

###### Base/ 基类

- **AnimNotify/ 动画通知**  
  实现均为直接调用接口 `BPI_Character`，无额外逻辑

- **BP_Character**  
  继承 `C_Character`。蓝图实现 `BPI_Character` 的接口

  - `I_PlaySound`：权威时调用一条 Multicast 自定义事件（`Replicates` = Multicast、`Reliable`；事件体内就是原来的 `PlaySound2D`，引脚设置原样）；客户端不再自行播放（原为无判定、两端各自播放。2026-10-10 按计划 2.7 改，开发者执行）
  - `I_SetOtherPauseState`：权威时用传入的 `bool` 值作为参数调用 `SetOtherPauseState`
  - `I_CameraShake`：权威时调用一条 Multicast 自定义事件（同上形状，事件体内是原来的晃动节点）；客户端不再自行执行（原为无判定、两端各自执行。2026-10-10 按计划 2.7 改，开发者执行）
  - `I_ChangeGravity`：根据参数设置移动组件重力标度为 0 或 1
  - `I_ChangeState`：权威时用传入的 `State` 作为参数调用 `ChangeState`；**非权威分支调用 `PredictChangeState`**（入参 `State` 透传。2026-10-11 按计划 3.1 补的客户端那一半，开发者执行。**不调 `ChangeState`** —— 那一条是权威写者、本地值只留一个写者；`PredictChangeState` 自带 `IsLocallyControlled()` 与本片开关两道门，远端的模拟代理不写、等表）
  - `I_Summon`：根据 `SummonIndex` 调用 `I_SpawnAttacker` 生成攻击体，0 生成查克拉秘卷，1 生成通灵魔法师
  - `I_SpawnAttacker`：权威时根据传入的攻击体类型、x、y、z 和 `Size` 生成攻击体，并将自身（即角色）设置为其 `Owner`，然后调用 `StartUse` 启用
  - `I_StopGrab`：权威时将自身抓取点设置为无效
  - `I_SetGrab`：权威时根据自身的 `Toward` 和传入的 x、y、z 设置自身抓取点的位置并置为有效，如果无有效抓取点则生成并设置有效，存储为自身抓取点
  - `I_StartHitCheck`：权威时调用 `ResetSuccessHit`（原为直接将 `SuccsessHit` 置为 `false`；这里写的 `SuccsessHit` 与 `I_HitJump` 读的 `successHit` 是同一个 C++ 属性 `AC_Character::bSuccessHit`，拼写差异是蓝图侧节点显示名，开发者 2026-10-10 核对；全工程写它的只有本行与 `C_Character::OnAttackBoxOverlap`，`I_HitJump` 只读不写。本行已于 2026-10-10 按计划 2.5 改为调用 `ResetSuccessHit`，开发者确认，详见开发计划 2.5）
  - `I_GiveChakra`：调用 `AddChakra`
  - `I_MakeDamage`：用传入的 `Type`、`Effect`、`Value`、`Time`、`State`、`GrabPoint` 作为参数调用 `BeDamaged`
  - `I_LockTargetToward`：权威时用 `TryTargetToward` 为 `TargetToward` 赋值；**客户端那半边尚未确认（2026-10-12，待编辑器确认）**：客户端那份 `TargetToward` 只由权威分支写 ⇒ 本地 `PredictMakeMove` 拿到的是初值（`(0,0)`），而两端各自 `ComputeMoveOffset` 用的就是各自那份 ⇒ 锁定那一刻带上下输入时两端会算出不同的 Y 分量。要补的形状 = 与权威分支同形的一条写（`TargetToward = TryTargetToward`；`TryTargetToward` 是 `UPROPERTY(BlueprintReadWrite)`，客户端那份是本地输入意图，见 `C_Character.cpp` 的 `Move()`）、不判权威。**现有日志测不出**（一轮 33 条位移全无 Y 分量），读数办法见开发计划 3.4 W4.3
  - `I_ChangeAttack`：权威时用传入的 `Attack` 作为参数调用 `ChangeAttack`；**非权威分支调用 `PredictChangeAttack`**（入参 `Attack` 透传。2026-10-11 按计划 3.3 补的客户端那一半，开发者执行 —— 与 `I_ChangeState` / `PredictChangeState` 同形。**不调 `ChangeAttack`**：那一条是权威写者；`PredictChangeAttack` 自带 `IsLocallyControlled()` + `HasAuthority()` 早退两道端别门与本片开关，远端的模拟代理不写、等表）
  - `I_ChangeBox`：**去掉"本地控制时"这道门，改为无条件**用传入的 `Size`、`Offset`、`Type` 调用 `RequestChangeBox`（2026-10-13 按计划 3.5 / 设计 5.9b 改，**开发者执行**）。门本身搬进了 C++：`RequestChangeBox` 在 `Prediction.Box 0` 那条分支里自带 `IsLocallyControlled()`，所以"关掉 = 与接入前逐位一致"仍成立。**不调 `ServerChangeBox`**：`RequestChangeBox` 在开关打开时**就地应用**（两端各自派生 —— 通知在哪一端跑，哪一端当场改自己那一份几何），关掉时才走那条 RPC。`AN_ChangeAttackBox` 与 `AN_ChangePlayerBox` 两个通知共用这一个函数体。**这一处是打开 `Prediction.Box` 的前置**：未改之前服务器那份"通知几何"恒为零（Tick 的派生每帧把多播刚写上的框拽回零），表现为攻击框全灭；哨兵 = 日志里 `BoxChange:` 的 `side server` 行数为 0
  - `I_ChangeDamageValue`：权威时用传入的造成伤害相关参数为 `Type`、`Effect`、`Value`、`Time`、`State` 赋值
  - `I_MakeMove`：权威时用传入的 `Offset` 和本地的 `TargetToward` 调用 `MakeMove`；**非权威分支调用 `PredictMakeMove`**（入参 `Offset` 与本地 `TargetToward` 透传。2026-10-12 按计划 3.4 W4.1 补的客户端那一半，开发者执行 —— 与 `I_ChangeState` / `I_ChangeAttack` 同形。**不调 `MakeMove`**：那一条是权威写者，且非权威端调它会打一条 Warning。**日志佐证（2026-10-12）**：甲落地那一轮 33 条 `MoveRootMotion` 里 17 条来自客户端、非权威端 Warning 0 条 ⇒ 这一处是"换掉"不是"加调"，本项验收完成）
  - `I_StartPreInput`：权威时调用 `StartPreInput`（原为直接 `Set bPreInputLock = false` 与 `TryTargetToward = 0` 两条写；已于 2026-10-09 按计划 2.2 / 已定事项 24 换成调用 C++ 的 `StartPreInput()`，两条写的语义一起挪进了该函数，蓝图侧不再保留，开发者执行）；**非权威分支直接把 `bPreInputLock` 置 `false`**（2026-10-10 按计划 2.7 补的客户端那一半。**不调 `StartPreInput`**——那个函数会连 `TryTargetToward` 一起清零，而它在客户端是本地输入意图，见 `C_Character.cpp` 的 `Move()`）
  - `I_SpawnSE`：权威时根据传入的 `Offset` 和 `SEName` 和本地 `Toward` 调用 `SpawnBPSE`

- **BPI_Character**  
  供动画通知调用的各种接口，基本在 `BP_Character` 中实现，`BP_Menma` 实现了一个 `I_HitJump`

###### Menma/ 角色-面麻

- **AnimSequences/ 动画序列**（无关）
- **AS_Menma/ 动画集**  
  调用动画通知
- **BP_AnimMenma**  
  状态机。**2026-10-11 由开发者改动（计划 3.3 验收期间）**：普攻各段的转移原先组织成**单向循环**（Attack1→2→3→4→5→…），而服务器经权威值表更正段号时 `PS->Attack` 是**跳变**（被拒绝的一段被拽回、或服务器推进的那一段带半个 RTT 才到）——跳变落不到单向循环的转移上会让**状态机卡住**。改成**各段之间可任意切换**后问题消失。本文件原盘点只记了通知挂载矩阵（计划 2.0.3），没有状态机转移表 —— 此处只记"单向循环 → 各段任意切换"这一事实与理由，**具体转移节点待编辑器确认**
- **BP_Menma**
  - 绑定状态机并同步数据
  - `I_HitJump` 技能一命中处理：权威且 `successHit` 为真时，将 `MySkill` 设置为 3 进入派生技能，并调用 `I_GiveChakra`

##### 其他

- **BP_ArenaGM**  
  继承 `C_ArenaGM`（无关）
- **BP_ArenaGS**  
  继承 `C_ArenaGS`（无关）
- **BP_PlayerController**  
  继承 `C_PlayerController`（无关）
- **BP_PlayerState**  
  继承 `C_PlayerState`（无关）

#### Lobby/ 大厅（无关）

- **BP_LobbyGM**  
  显示大厅 UI，当大厅有两位玩家时，进行无缝传送并让玩家控制器显示加载 UI（无关）
- **BP_LobbyPC**  
  显示加载 UI（无关）

#### 其他

- **BP_GameInstance**  
  存储 `isHosting` 控制大厅 UI 表现（无关）

### Data/ 数据（无关）

- **SE/ 特效数据表**（无关）
- **Input/ 输入相关**（无关）
  - **Inputs/ 输入**（无关）
- **Map/ 地图**（无关）

### Material/ 材料

#### Attacker/ 攻击体

> 项目由于攻击体生成是权威的，只在服务器生成，由复制同步到各个客户端，所以内部部分函数没有权威判断

##### Base/ 基类

- **AAN/ 攻击体动画通知**  
  实现均为直接调用接口 `BPI_AttackerBase`，无额外逻辑

- **BP_AttackerBase**
  - `Tick` 实现类似 `C_Character` 的坐标变换，在 2D 中实现高度效果，根据 Z 轴的值对 Y 轴进行偏移
  - `StartUse` 生命周期控制，基类空实现
  - `BeginPlay` 根据生成时传入的 `Toward` 设置 `PaperFlipbook` 的翻转
  - 实现 `BPI_AttackerBase` 的接口
    - `I_ASpawnAttacker`：权威时根据传入的攻击体类型、x、y、z 和 `Size` 生成攻击体，并将自身 `Owner`（即角色）设置为其 `Owner`，然后调用 `StartUse` 启用
    - `I_AStopGrab`：权威时将自身抓取点设置为无效
    - `I_ASetGrab`：权威时根据自身的 `Toward` 和传入的 x、y、z 设置自身抓取点的位置并置为有效，如果无有效抓取点则生成并设置有效，存储为自身抓取点
    - `I_AOver`：摧毁自身
    - `I_ASetDamage`：根据传入值设置 `Type`、`Effect`、`Value`、`Time`、`State` 这些造成伤害的相关变量
    - `I_AChangeBox`：根据传入的 `Size` 和 `Offect` 设置攻击体碰撞盒体的碰撞类型、相对位置和尺寸
  - `Overlap` 碰撞检测：碰撞时如果权威且命中对象不为自身，根据存储的造成伤害的相关变量调用 `I_MakeDamage` 造成伤害，且如果 `IfGiveChakra` 为真调用 `I_GiveChakra`

- **BPI_AttackerBase**  
  供动画通知调用的各种接口，在 `BP_AttackerBase` 中实现

##### Menma/ 面麻相关攻击体

均继承 `BP_AttackerBase`，实现动画表现以及调用攻击体动画通知（无关）

##### Scroll/ 秘卷相关

- **Chrkra/ 秘卷查克拉**
  - **BP_AScrollChakra**  
    继承 `BP_AttackerBase`，启用后调用两次 `I_GiveChakra` 后摧毁

##### Summon/ 生成物相关

均继承 `BP_AttackerBase`，实现动画表现以及调用攻击体动画通知（无关）

#### 其他

- **CharacterM/ 角色素材**（无关）
- **MapM/ 地图素材**（无关）
- **SpecialEffects/ 特效**（无关）
- **Summon/ 生成素材**（无关）
- **UIM/ UI素材**（无关）

### Sound/ 音效（无关）

### UI/ UI（无关）

#### Arena/ 决斗场UI（无关）

- **BP_PlayerWidget**  
  继承 `C_PlayerWidget`，显示对局 UI，开始动画，结算动画（无关）

#### Lobby/ 大厅UI（无关）

- **W_Loading**  
  加载 UI（无关）
- **W_Lobby**  
  根据 `BP_GameInstance` 的 `isHosting` 值控制控件创建时是否显示匹配 UI。有开始和退出按钮（无关）
- **W_Matchmaking**  
  创建房间和加入房间的相关按钮，创建房间会设置 `BP_GameInstance` 的 `isHosting` 为真，并以监听模式重新进入 Lobby 关卡（无关）