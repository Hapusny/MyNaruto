# 火影忍者·决斗场 - 局域网对战版

![UE5](https://img.shields.io/badge/Unreal%20Engine-5.0-000000?logo=unrealengine)
![Platform](https://img.shields.io/badge/Platform-Windows-blue)
![Network](https://img.shields.io/badge/Network-LAN%20Multiplayer-green)

>  **项目演示**：[点击观看实机演示](https://www.bilibili.com/video/BV1c5Vn6hEkY/)

一款基于UE5的2D横版局域网格斗游戏，采用**状态同步**架构实现1v1对战。完整实现了替身术、抓取/击飞/平推三种攻击类型、技能派生等格斗游戏核心机制。

>  详细设计见 [GDD.docx](./GDD.docx)

---

##  核心玩法

| 操作 | 效果 |
|------|------|
| WASD | 移动（前后移速1，上下移速0.5） |
| J | 普攻（5段连招，末段为抓取） |
| K/L | 技能（硬体攻击，命中+1查克拉） |
| I | 奥义（消耗4查克拉，金刚体） |
| 空格 | 替身（消耗1查克拉，受击时可瞬移逃脱，CD15秒） |
| U | 秘卷（40秒CD） |
| O | 通灵 |

### 攻击类型
- **平推**：造成僵直状态
- **击飞**：造成浮空，落地后弹起再起身
- **抓取**：强制控制对手，无法替身

### 角色状态
常态 → 硬体/金刚体（技能期间）→ 僵直/击飞/被抓取（受击）→ 保护（起身无敌）→ 常态

---

##  核心类职责

| 类 | 职责 |
|---|------|
| `C_ArenaGM` | 玩家登录/无缝旅行处理、队伍分配、角色生成、胜负判定、返回大厅 |
| `C_ArenaGS` | 存储战斗开始时间戳 |
| `C_PlayerState` | 队伍/血量/查克拉/攻击计数/技能计数/角色状态（全部Replicated） |
| `C_PlayerController` | 输入模式切换、UI更新、受击逻辑处理、角色状态RPC |
| `C_Character` | 移动/攻击/技能/替身、碰撞框控制、CD计算、网络同步 |
| `C_Camera` | 客户端追踪摄像机，根据角色朝向动态偏移，边界限制 |
| `C_GrabPoinnt` | 抓取点Actor，抓取期间将目标锁定至该位置 |

---

##  网络同步

### 属性复制
- `Team`：队伍分配后同步，客户端触发`OnRep_Team`初始化角色朝向和颜色
- `HealthValue`/`Chakra`：受击/命中后更新，客户端UI自动刷新
- `Attack`：攻击计数同步，驱动动画播放
- `CharacterState`：状态变更同步，驱动受击动画
- `Toward`：角色朝向同步

### RPC调用
- **Server RPC**：`Server_Attack`、`Server_Escape`、`Server_ChangeBox`、`Server_ChangeToward`
- **Client RPC**：`Client_ChangeInputAbility`（切换输入模式）、`Client_SetWidgetTime/End`、`Client_ShowWidget`
- **Multicast**：`Mult_ChangeBoxSize`（碰撞框同步）、`Mult_ChangeProtectedAnim`（保护状态半透明）、`Mult_ChangeGravity`（抓取时禁用重力）

### 关键流程举例

**队伍分配**：
`AssignTeams()` → 随机分配红蓝 → `SetTeam()` → 属性复制 → 客户端`OnRep_Team()`广播 → `MyInitialize()`设置朝向和标识颜色

**攻击**：
客户端`Attack()` → `Server_Attack()` → 服务器校验状态 → `Attack`自增（驱动动画）→ `OnAttackBoxOverlap()`碰撞 → `BeDameged()` → 更新血量/状态 → 属性复制同步UI

**替身**：
客户端`Escape()` → `Server_Escape()` → 校验查克拉/CD/受击状态 → 禁用碰撞框 → 瞬移至敌方位置 → 查克拉-1 → 进入Protected保护状态 → 属性复制同步

---

##  动画通知

| 通知 | 作用 |
|------|------|
| `AN_ChangeAttack` | 切换普攻连段或重置 |
| `AN_ChangeAttackBox/PlayerBox` | 调整攻击框/受击框大小位置 |
| `AN_ChangeState` | 切换角色状态（如硬体→常态） |
| `AN_MakeMove` | 技能位移 |
| `AN_SetOtherPauseState` | 对手停帧（奥义时停效果） |
| `AN_SpawnAttacker` | 生成飞行道具/召唤物 |
| `AN_SetGrab` / `AN_StopGrab` | 绑定/解绑抓取点 |
| `AN_PreInput` | 开启连段预输入缓存 |
| `AN_StartHitCheck` / `AN_HitJump` | 命中判定及后续派生 |

---

##  UI数据绑定

| 元素 | 数据源 | 更新方式 |
|------|--------|----------|
| 时间 | `GameState` | `Client_SetWidgetTime` |
| 血量/查克拉 | `PS1/PS2` | 属性复制自动 |
| 技能CD | `MyPawn->*CDState` | Tick每帧计算 |
| 奥义可用性 | `Chakra == 4` | 属性复制 |

CD≤0时图标亮白，CD>0时图标变灰，CD<60秒显示数字。

---

##  关卡与摄像机

**流程**：`Lobby` → (Seamless Travel) → `Transition` → `Arena`

**摄像机**：
- 正交投射，朝向-Z
- 根据角色`Toward`方向动态偏移（朝右偏移+X，朝左偏移-X）
- 插值平滑跟随，速度由`FollowSpeed`控制

**角色移动范围**：X:[-800,800]，Y:[80,280]（被抓取时不受限）

---

##  快速开始

### 环境要求
- Unreal Engine 5.3
- Visual Studio 2022
- PaperZD插件

### 编译运行
1. 右键`.uproject` → Generate Visual Studio project files
2. 打开`Naruto.sln`编译
3. 服务端：Lobby关卡点击Host
4. 客户端：输入服务端IP，点击Join

---

##  验收要点

- 双人联机，位置/血量/状态同步正常
- 攻击命中、技能特效双方可见
- 替身术在受击时可用，消耗查克拉且有CD
- 抓取/击飞/平推三种攻击类型正确生效
- 奥义需满4查克拉，释放后清空
- 血量归零或时间结束正确判定胜负
- 完整对局后返回大厅不崩溃

---

**开发者**：Koiro | **许可**：学习交流使用

---
