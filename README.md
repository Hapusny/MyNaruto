# 火影忍者·决斗场 — 局域网对战版

![UE5](https://img.shields.io/badge/Unreal%20Engine-5.3-000000?logo=unrealengine)
![Platform](https://img.shields.io/badge/Platform-Windows-blue)
![Network](https://img.shields.io/badge/Network-LAN%20Multiplayer-green)
![Netcode](https://img.shields.io/badge/Netcode-Client%20Prediction-orange)

基于 **Unreal Engine 5.3** 的 2D 横版格斗游戏，采用**服务器权威 + 状态同步**架构，支持 1v1 局域网对战。
完整实现替身术、抓取 / 击飞 / 平推三种攻击类型、技能派生等格斗游戏核心机制，设计并实现一套**客户端预测与回滚系统**，让存在延迟情况下操作依旧跟手。

**演示视频**

| 内容 | 链接 |
|------|------|
| 项目实机演示 | [点击观看](https://www.bilibili.com/video/BV1c5Vn6hEkY/) |
| 客户端预测系统演示 | [点击观看](https://www.bilibili.com/video/BV15nJy6KE1Q/) |

> 玩法设计详见 [GDD.docx](./GDD.docx)；预测系统详见 [PredictionSystemDesign.md](./PredictionSystemDesign.md)。

---

## 目录

- [核心玩法](#核心玩法)
- [快速开始](#快速开始)
- [系统架构](#系统架构)
- [网络同步](#网络同步)
- [客户端预测系统](#客户端预测系统)
- [动画通知](#动画通知)
- [UI 数据绑定](#ui-数据绑定)
- [关卡与摄像机](#关卡与摄像机)
- [验收要点](#验收要点)
- [项目文档](#项目文档)

---

## 核心玩法

### 操作

| 输入 | 动作 | 说明 |
|:----:|------|------|
| `WASD` | 移动 | 前后移速 1，上下移速 0.5 |
| `J` | 普攻 | 5 段连招，末段为抓取 |
| `K` / `L` | 技能 | 硬体攻击，命中 +1 查克拉 |
| `I` | 奥义 | 消耗 4 查克拉，金刚体 |
| `空格` | 替身 | 消耗 1 查克拉，受击时瞬移逃脱，CD 15 秒 |
| `U` | 秘卷 | CD 40 秒 |
| `O` | 通灵 | — |

### 攻击类型

| 类型 | 受击效果 |
|------|----------|
| 平推 | 造成僵直状态 |
| 击飞 | 造成浮空，落地后弹起再起身 |
| 抓取 | 强制控制对手，无法替身 |

### 角色状态流转

```
常态 → 硬体（技能期间）→ 僵直 / 击飞 / 被抓取（受击）→ 保护（起身无敌）→ 常态
```

---

## 快速开始

### 环境要求

| 项 | 版本 / 说明 |
|----|-------------|
| 引擎 | Unreal Engine 5.3 |
| IDE | Visual Studio 2022 |
| 插件 | PaperZD |

### 编译与运行

1. 右键 `Naruto.uproject` → Generate Visual Studio project files
2. 打开 `Naruto.sln`，以 `Development Editor` 配置编译
3. **服务端**：进入 `Lobby` 关卡，点击 `Host`
4. **客户端**：输入服务端 IP，点击 `Join`

---

## 系统架构

### 目录结构

```
Naruto/Source/Naruto/
├── C_Character.*                角色：移动 / 攻击 / 技能 / 替身 / 碰撞框 / 预测接入点
├── C_PlayerController.*         输入模式切换、UI 更新、受击处理、预测回执下发
├── C_PlayerState.*              队伍、血量、查克拉、段号、角色状态
├── C_ArenaGM.* / C_ArenaGS.*    对局流程、队伍分配、胜负判定
├── C_Camera.* / C_GrabPoinnt.*  追踪摄像机与抓取点
├── C_PlayerWidget.*             战斗 UI：时间、血量、技能 CD
├── C_BPLNetwork.*               联网流程蓝图函数库（无缝切图）
├── C_PredictionComponent.*      预测键、标记、结算与回滚          ← 预测系统
└── C_AuthorityValueComponent.*  权威值表（每 Actor 一张）        ← 预测系统
```

### 核心类职责

| 类 | 职责 |
|----|------|
| `C_ArenaGM` | 玩家登录 / 无缝旅行处理、队伍分配、角色生成、胜负判定、返回大厅 |
| `C_ArenaGS` | 存储战斗开始时间戳 |
| `C_PlayerState` | 队伍、血量、查克拉、攻击计数、技能计数、角色状态 |
| `C_PlayerController` | 输入模式切换、UI 更新、受击逻辑处理、角色状态 RPC |
| `C_Character` | 移动 / 攻击 / 技能 / 替身、碰撞框控制、CD 计算、网络同步与预测接入点 |
| `C_Camera` | 客户端追踪摄像机，随角色朝向动态偏移，含边界限制 |
| `C_GrabPoinnt` | 抓取点 Actor，抓取期间将目标锁定至该位置 |
| `C_PredictionComponent` | 预测键生命周期、属性标记、服务器回执结算、回滚 / 跟进 |
| `C_AuthorityValueComponent` | 权威值副本，服务器每帧刷新、客户端只读，被预测属性的统一回滚依据 |

---

## 网络同步

### 属性复制

- `Team`：队伍分配后同步，客户端 `OnRep_Team` 初始化朝向与标识颜色。
- **被预测属性**（`HealthValue` / `Chakra` / `Attack` / `MySkill` / `CharacterState`）**不再逐属性复制**，改由**权威值表**统一下发——这是客户端本地先行的前提（见[客户端预测系统](#客户端预测系统)）。

### RPC 调用

- **Server RPC**：`Server_Attack`、`Server_Escape`、`Server_ChangeSkillState`（三者均携带 `FPredictionKey`）、`Server_ChangeBox`、`Server_ChangeToward`
- **Client RPC**：`Client_ChangeInputAbility`（切换输入模式）、`Client_SetWidgetTime/End`、`Client_ShowWidget`、`Client_ResolvePrediction`（预测回执）、`Client_CorrectLocks`（锁更正）
- **Multicast**：`Mult_ChangeBoxSize`（碰撞框同步，降级路径）、`Mult_ChangeProtectedAnim`（保护状态半透明）、`Mult_ChangeGravity`（抓取时禁用重力）

### 关键流程

**队伍分配**
`AssignTeams()` → 随机分配红蓝 → `SetTeam()` → 属性复制 → 客户端 `OnRep_Team()` 广播 → `MyInitialize()` 设置朝向与标识颜色

**攻击**
客户端 `Attack()` → `Server_Attack()` → 服务器校验状态 → `Attack` 自增（驱动动画）→ `OnAttackBoxOverlap()` 碰撞 → `BeDameged()` → 更新血量 / 状态 → 同步回两端

**替身**
客户端 `Escape()` → `Server_Escape()` → 校验查克拉 / CD / 受击状态 → 禁用碰撞框 → 瞬移至落点 → 查克拉 −1 → 进入 `Protected` 保护状态 → 同步回两端

---

## 客户端预测系统

在状态同步架构下，一次输入要等服务器判定、再等同步回来才能看到结果——**整整一个 RTT**（局域网下表现为出招发涩、连段不跟手）。
预测系统让本地输入**当即生效**：客户端本地先行，服务器照常校验，回执确认则跟进、拒绝则回滚到权威值。

系统以**组件**形式外挂在原有网络层之上，不改动任何既有同步机制：

| 组件 | 职责 |
|------|------|
| `UC_PredictionComponent` | 预测键、属性标记、变化记录、结算与回滚 |
| `UC_AuthorityValueComponent` | 每 Actor 一份的权威值副本（玩家 PS、角色、敌方 PS 各一张） |

### 三类预测

| 类别 | 本地立即改变的对象 | 是否建键 | 回滚依据 |
|------|--------------------|:--------:|----------|
| **状态预测** | `MySkill`（技能 / 奥义 / 秘卷 / 通灵）、`Attack`（普攻连段）、`CharacterState`（受击 / 保护 / 霸体） | 是，持有完整生命周期 | 非复制状态走变化记录；可复制状态走权威值表 |
| **属性预测** | `Chakra`、`Toward`、位置、CD 时间戳 | 否，共享当前活跃键 | 数值 / 朝向 / 时间戳走权威值表；位置走一次性基线 |
| **锁预测** | `bAttackInputLock`、`bPreInputLock`、`bSuccessHit` | 否 | 不回滚，由服务器锁更正通道直接覆盖 |

> 动画、特效、音效、UI、摄像机**不是第四类预测**——它们只是读取上述三类数据后的本地表现，不单独纳入回滚范围。

### 核心机制

**预测键（PredictionKey）—— 每一次预测的唯一标识**

- 客户端在**输入点**建键，一个键 = 一次可被服务器独立裁决的输入；建键前统一冻结旧键，保证同一时刻只有一个活跃键
- 键随三个带键 RPC 上行的 `FPredictionKey` 参数传给服务器，服务器原样回传用于定位本地记录
- 键不跨客户端同步；未结算的键 **2.0 秒超时**强制回滚

**标记 + 权威值表 —— 回滚依据**

- 本地修改被预测属性时只写一个「预测标记」，不另存旧值
- 权威值由**权威值表**统一下发（服务器 `PreReplication` 时从真实属性刷新，客户端只读）
- 采用规则三行：`无标记 → 写回本地`、`有标记 → 只更新表`、`有标记但值已相等 → 更新并清标记`

**结算 —— 服务器裁决**

服务器回执 `Client_ResolvePrediction` 到达后统一走 `ResolvePrediction`：

| 结果 | 处理 |
|------|------|
| 确认 | 执行跟进委托 + 清标记（**不写回本地值**，本地预测值本就是对的） |
| 拒绝 / 超时 | 执行回滚委托 + 从权威值表恢复属性 + 恢复位置 |

**位置 —— 不靠复制兜底**

- 技能位移改走**根运动源**：两端各自按同一份共享动作状态（`MySkill` / `Attack`）命名并施加，服务器因此能重演客户端那一段位移，位置校正不再打架
- 替身瞬移走**一次性基线**：位移前记一次绝对位置，被拒时贴回
- 依据：拥有者收不到自身的移动复制（`ReplicatedMovement` 是 `COND_SimulatedOrPhysics`），自身位置一致性由移动组件的 `ServerMove` → `ClientAdjustPosition` 通道保证

**碰撞框 —— 两端各自派生**

- `BoxExtent` 与碰撞启用状态**根本不走复制**（裸 `UPROPERTY`），旧路要等一个 RTT 才能让拥有者看到自己的框
- 现方案：几何由**动画通知**在两端各自就地创建，翻转与状态闸门每帧从已复制状态派生 ⇒ 两端在**同一动画帧的框逐位一致**
- 旧的 `Server_ChangeBox` / `Mult_ChangeBoxSize` RPC 对保留，作为降级路径

**服务器校验与降级**

- 服务器对每条带键请求做状态 / 查克拉 / CD 校验，非法即回 `Rejected`，客户端回滚
- 十个 cvar 逐片开关，**每个开关的「关」都等价于「未接入」**，可随时退回改造前行为

### 调试开关（cvar）

| 开关 | 默认 | 关闭 = 什么 |
|------|:----:|-------------|
| `Prediction.Enabled` | 1 | 全局关闭：不再建键 / 本地先行 / 本地置锁 / 带键 RPC |
| `Prediction.Skill` | 1 | 技能切片（五个技能输入） |
| `Prediction.Escape` | 1 | 替身切片 |
| `Prediction.Attack` | 1 | 普攻段切片 |
| `Prediction.Move` | 1 | 位移切片：客户端不施加根运动源 |
| `Prediction.Box` | 1 | 碰撞框切片：回到 RPC 对（独立于 `Prediction.Enabled`） |
| `Prediction.Log` | 0 | 打开 `LogPrediction` 详细日志 |
| `Prediction.Draw` | 0 | 屏上绘制键 / 标记 / 锁 / 表 |
| `Prediction.ForceReject` | 0 | 服务器一律拒绝带键请求（验证回滚路径） |
| `Prediction.DropResolve` | 0 | 服务器不回执（验证超时兜底） |

### 落地情况与边界

- 阶段一 ~ 三、五**全部落地并实机验收通过**（局域网 `Net PktLag 0 / 100 / 200` 各档实测），全程 0 编译警告、0 错误
- **阶段四（命中预测）整体不做**：实机体验中「几百延迟的命中延迟并不明显」，且本地判定只能拿本机陈旧的敌方代理位置去判，收益只落在攻击方一屏、误判代价却是"对手身体抖一下又弹回"。设计保留在设计文档 2.5.2 / 2.5.3，附重开判据

---

## 动画通知

| 通知 | 作用 |
|------|------|
| `AN_ChangeAttack` | 切换普攻连段或重置 |
| `AN_ChangeAttackBox` / `AN_ChangePlayerBox` | 调整攻击框 / 受击框大小位置 |
| `AN_ChangeState` | 切换角色状态（如硬体 → 常态） |
| `AN_MakeMove` | 技能位移 |
| `AN_SetOtherPauseState` | 对手停帧（奥义时停效果） |
| `AN_SpawnAttacker` | 生成飞行道具 / 召唤物 |
| `AN_SetGrab` / `AN_StopGrab` | 绑定 / 解绑抓取点 |
| `AN_PreInput` | 开启连段预输入缓存 |
| `AN_StartHitCheck` / `AN_HitJump` | 命中判定及后续派生 |

> 接入预测系统后，`AN_ChangeState` / `AN_ChangeAttack` / `AN_MakeMove` 三条通知拆出**客户端半边**（`Predict*` 系列入口），使通知的本地效果不再等一个 RTT。

---

## UI 数据绑定

| 元素 | 数据源 | 更新方式 |
|------|--------|----------|
| 时间 | `GameState` | `Client_SetWidgetTime` |
| 血量 / 查克拉 | `PS1 / PS2` | 权威值表 / 客户端本地先行 |
| 技能 CD | `MyPawn->*CDState` | Tick 每帧计算 |
| 奥义可用性 | `Chakra == 4` | 同上 |

CD ≤ 0 时图标亮白，CD > 0 时图标变灰，CD < 60 秒时显示剩余秒数。

---

## 关卡与摄像机

**关卡流程**：`Lobby` →（Seamless Travel）→ `Transition` → `Arena`

**摄像机**

- 正交投射，朝向 −Z
- 随角色 `Toward` 方向动态偏移（朝右偏移 +X，朝左偏移 −X）
- 插值平滑跟随，速度由 `FollowSpeed` 控制

**角色移动范围**：`X: [-800, 800]`，`Y: [80, 280]`（被抓取时不受限）

---

## 验收要点

**基础对战**

- 双人联机，位置 / 血量 / 状态同步正常
- 攻击命中、技能特效双方可见
- 替身术在受击时可用，消耗查克拉且有 CD
- 抓取 / 击飞 / 平推三种攻击类型正确生效
- 奥义需满 4 查克拉，释放后清空
- 血量归零或时间结束正确判定胜负
- 完整对局后返回大厅不崩溃

**预测系统**

- `Net PktLag 0 / 100 / 200` 下，出招、连段、替身、位移均本地即时响应，不回弹、不瞬移
- 两端在同一动画帧的碰撞框逐位一致
- 被服务器拒绝的输入正确回滚到权威值
- 关闭对应 cvar 后退回改造前行为，游戏可正常进行

---

## 项目文档

| 文档 | 内容 |
|------|------|
| [GDD.docx](./GDD.docx) | 玩法设计文档 |
| [PredictionSystemDesign.md](./PredictionSystemDesign.md) | 预测系统设计（含冻结接口表） |
| [PredictionSystemDevPlan.md](./PredictionSystemDevPlan.md) | 预测系统开发计划、工作包与决策记录 |
| [AICollaborativeNotes.md](./AICollaborativeNotes.md) | 预测系统人机协同笔记与裁决记录 |
| [BlueprintSideCallSiteInventory.md](./BlueprintSideCallSiteInventory.md) | 蓝图侧调用点盘点 |

---

**开发者**：Koiro ｜ **许可**：学习交流使用
