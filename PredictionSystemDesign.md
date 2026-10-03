# 预测系统设计

## 1. 项目功能点

在进入预测系统设计之前，先对项目现有功能点做一次完整总结。原因有三点：

- 预测系统不是独立系统，而是依附于现有功能点和网络同步方式之上的本地先行模块；
- 预测涉及判断需要以功能点为粒度；
- 功能点总表可以作为后续设计的索引。

表中「预测归属」列的含义：

| 取值 | 含义 |
| --- | --- |
| 状态 | 纳入**状态预测**：客户端输入后本地立即切换的状态变量 |
| 属性 | 纳入**属性预测**：客户端输入后本地立即修改的数值、位置、朝向、时间戳；包含对敌方本地代理的影响。数值 / 朝向 / 时间戳的回滚依据是**权威影子**，**位置用一次性基线**（见 2.5.1-C / 2.9） |
| 锁 | 纳入**锁预测**：本地立即置位的输入控制开关（不建键、不记录、不结算） |
| 否 | 不纳入预测系统，仍由原有网络层负责 |
| 否（派生） | 由其它变量派生，不作为预测对象 |
| （部分） | 该功能点只有部分环节纳入预测 |
| 表现（记录） | **不是独立预测类别**；作为表现记录参与跟进/回滚（见 2.1） |
| 属性（多播更正） | 该属性走属性预测，但其权威值由 NetMulticast 下发——多播到达即视为该数据权威值已到，清除对应预测标记（见 2.11.3 第 6 条） |
| 属性（位移，基线回滚） | 位置类属性预测：回滚**不用影子**，用位移前捕获的一次性基线（见 2.5.1-C / 2.9 / `RecordMoveBaseline`） |
| 属性（部分，需碰撞框本地先行） | 该环节的本地预测依赖"碰撞框在拥有者客户端上已是最新几何"这一前提；该前提未落地前整体降级为服务器判定（见 5.9） |

### 1.1 功能点总表

| 编号 | 模块 | 功能点 | 网络涉及方式 | 预测归属 |
| --- | --- | --- | --- | --- |
| 1.1.1-01 | 游戏框架与流程 | Host 以 Listen Server 模式创建 Lobby | Listen Server、ServerTravel | 否 |
| 1.1.1-02 | 游戏框架与流程 | Join 输入 IP 加入服务器 | ClientTravel | 否 |
| 1.1.1-03 | 游戏框架与流程 | 大厅等待玩家显示等待计时、Cancel 返回 | 联机标识、无缝传送 | 否 |
| 1.1.1-04 | 游戏框架与流程 | 关卡无缝切换 Lobby → Arena → Lobby | ServerTravel、SeamlessTravel | 否 |
| 1.1.1-05 | 游戏框架与流程 | 玩家人数监测 | PostLogin、HandleSeamlessTravelPlayer | 否 |
| 1.1.1-06 | 游戏框架与流程 | 随机分配红蓝方 | 服务器 AssignTeams、PlayerState::Team 复制 | 否 |
| 1.1.1-07 | 游戏框架与流程 | 出生点生成角色 | 服务器 Spawn、Possess | 否 |
| 1.1.1-08 | 游戏框架与流程 | 生成并启用摄像机 | 服务器 Spawn、Client_Activate RPC | 否 |
| 1.1.1-09 | 游戏框架与流程 | 战斗开始延迟后启动战斗时钟、允许输入 | 服务器计时、Client_ChangeInputAbility | 否 |
| 1.1.1-10 | 游戏框架与流程 | 倒计时更新 | Tick 更新剩余时间、Client_SetWidgetTime RPC | 否 |
| 1.1.1-11 | 游戏框架与流程 | 胜负判定 | 服务器 GameTerminate | 否 |
| 1.1.1-12 | 游戏框架与流程 | 结算 UI 显示胜/负/平 | Client_SetWidgetEnd RPC | 否 |
| 1.1.1-13 | 游戏框架与流程 | 返回大厅 | 3 秒后 ClientTravel 回 Lobby | 否 |
| 1.1.1-14 | 游戏框架与流程 | 玩家暂停/时停 | 服务器 SetPlayerPauseState、Client_ChangeInputAbility | 否 |
| 1.1.1-15 | 游戏框架与流程 | 奥义等暂停敌方输入 | 服务器 SetPlayerPauseState、Client_ChangeInputAbility | 否 |
| 1.1.2-01 | 角色基础与移动 | 角色碰撞体 AttackBox / PlayerBox | SetIsReplicated(true)（**仅模拟代理**收到组件变换） | 属性（部分，需碰撞框本地先行） |
| 1.1.2-02 | 角色基础与移动 | 位置标记底部椭圆环，红/蓝 | 本地表现，颜色由队伍决定 | 否 |
| 1.1.2-03 | 角色基础与移动 | 动画显示 PaperZD + Flipbook | 部分 Flipbook 复制，朝向复制 | 表现（记录） |
| 1.1.2-04 | 角色基础与移动 | 角色朝向 Toward 属性 | DOREPLIFETIME(AC_Character, Toward) | 属性 |
| 1.1.2-05 | 角色基础与移动 | 移动输入 WASD / EnhancedInput | 本地输入，Server_SetTryTargetToward | 否（UE 移动组件负责） |
| 1.1.2-06 | 角色基础与移动 | 移动意图同步 TryTargetToward | Server_SetTryTargetToward Server RPC | 否（随输入同步，无回滚语义） |
| 1.1.2-07 | 角色基础与移动 | 转向同步 Server_ChangeToward | Server RPC, Toward 复制 | 属性 |
| 1.1.2-08 | 角色基础与移动 | 移动可行性判断 | 依赖 PlayerState 复制状态 | 否（UE 移动组件负责） |
| 1.1.2-09 | 角色基础与移动 | 移动范围限制 MaxLocation / MinLocation | 服务器 Tick 限制（两端各自执行） | 否（UE 移动组件负责） |
| 1.1.2-10 | 角色基础与移动 | 重力与下落 GravityScale | Mult_ChangeGravity 多播 | 属性（多播更正） |
| 1.1.2-11 | 角色基础与移动 | 被抓取时位置同步 | Mult_ChangeGrabLocation 多播 | 属性（位置，多播更正） |
| 1.1.2-12 | 角色基础与移动 | 角色初始化根据队伍设置朝向/颜色 | OnRep_PlayerState、OnTeamChanged | 否 |
| 1.1.3-01 | 战斗与攻击 | 普攻输入 J 键 | Server_Attack RPC | 状态 |
| 1.1.3-02 | 战斗与攻击 | 普攻连段 5 段 | PlayerState::Attack 复制 | 状态 |
| 1.1.3-03 | 战斗与攻击 | 攻击动画触发 | 根据 Attack 计数播放，Attack 属性复制 | 状态（表现） |
| 1.1.3-04 | 战斗与攻击 | 攻击框检测 AttackBox Overlap | 服务器 OnAttackBoxOverlap | 属性（攻方预测敌方代理） |
| 1.1.3-05 | 战斗与攻击 | 受击框变化 | 动画通知改变碰撞，Server_ChangeBox + Mult_ChangeBoxSize | 属性（部分，需碰撞框本地先行） |
| 1.1.3-06 | 战斗与攻击 | 伤害计算 | 服务器 BeDamaged → PlayerGetDamage | 属性（攻方预测敌方代理血量） |
| 1.1.3-07 | 战斗与攻击 | 攻击类型 Push / Launch / Grab | 状态与位移服务器处理，状态复制 | 属性（敌方代理状态） |
| 1.1.3-08 | 战斗与攻击 | 平推短暂僵直 | CharacterState 复制 | 属性（敌方代理状态） |
| 1.1.3-09 | 战斗与攻击 | 击飞空中下落、弹起、起身 | 服务器 Tick 处理，位置复制（敌方为模拟代理，复制可达） | 属性（敌方代理状态；位移不预测，见 2.5.2） |
| 1.1.3-10 | 战斗与攻击 | 抓取被抓取状态、抓取点 | 服务器处理，Mult_ChangeGrabLocation | 属性（部分） |
| 1.1.3-11 | 战斗与攻击 | 保护状态替身保护、起身保护 | 服务器状态，Mult_ChangeProtectedAnim | 属性（多播更正） |
| 1.1.3-12 | 战斗与攻击 | 保护表现保护时半透明 | Mult_ChangeProtectedAnim 多播 | 表现（多播更正） |
| 1.1.3-13 | 战斗与攻击 | 攻击命中标记 bSuccessHit | 服务器命中判定，动画派生用 | 锁 |
| 1.1.3-14 | 战斗与攻击 | 攻击预输入 bPreInputLock | 服务器攻击逻辑 | 锁 |
| 1.1.3-15 | 战斗与攻击 | 攻击输入锁 bAttackInputLock | 服务器与本地表现 | 锁 |
| 1.1.3-16 | 战斗与攻击 | 伤害数值 DamageValue 等 | 动画通知修改，服务器读取 | 否 |
| 1.1.3-17 | 战斗与攻击 | 攻击体生成 | 服务器 Spawn，多播表现 | 属性（部分） |
| 1.1.3-18 | 战斗与攻击 | 攻击体伤害 AAN_SetDamage | 服务器伤害判定 | 属性（部分） |
| 1.1.3-19 | 战斗与攻击 | 攻击体销毁 AAN_Over | 服务器销毁，客户端表现 | 属性（部分） |
| 1.1.4-01 | 技能、替身、秘卷、通灵 | 技能一 10s CD，跳抓、螺旋丸 | Server_ChangeSkillState，CD 复制/同步 | 状态 |
| 1.1.4-02 | 技能、替身、秘卷、通灵 | 技能二 12s CD，法阵抓取 | Server_ChangeSkillState，CD 复制/同步 | 状态 |
| 1.1.4-03 | 技能、替身、秘卷、通灵 | 奥义 4 奥义点，金刚体 | Server_ChangeChakra、Server_ChangeSkillState | 状态 |
| 1.1.4-04 | 技能、替身、秘卷、通灵 | 替身空格，1 奥义点 | Server_Escape RPC | 状态 |
| 1.1.4-05 | 技能、替身、秘卷、通灵 | 替身冷却 15s | LastEscapeTime 复制 | 属性（CD 时间戳） |
| 1.1.4-06 | 技能、替身、秘卷、通灵 | 替身保护 1.5s 无敌 | 服务器状态，多播半透明 | 属性 |
| 1.1.4-07 | 技能、替身、秘卷、通灵 | 替身范围检测 300 内敌人 | 服务器查找敌方 Pawn | 属性（本地先行检测，服务器权威） |
| 1.1.4-08 | 技能、替身、秘卷、通灵 | 替身瞬移移动到敌方位置 | 服务器 SetActorLocation（客户端需补本地先行，见 5.4） | 属性（位移，基线回滚） |
| 1.1.4-09 | 技能、替身、秘卷、通灵 | 秘卷 U 键，查克拉 +2 | Server_SetSummonIndex、Server_ChangeSkillState | 状态 |
| 1.1.4-10 | 技能、替身、秘卷、通灵 | 通灵 O 键，大螺旋丸 | Server_SetSummonIndex、Server_ChangeSkillState | 状态 |
| 1.1.4-11 | 技能、替身、秘卷、通灵 | 技能 CD 计算 | 服务器世界时间差值，客户端 UI 读取 | 属性（CD 时间戳） |
| 1.1.4-12 | 技能、替身、秘卷、通灵 | 技能特效 BP_XXXSkillEffect | 服务器调用，客户端多播/复制表现 | 状态（表现） |
| 1.1.4-13 | 技能、替身、秘卷、通灵 | 技能状态同步 MySkill | DOREPLIFETIME(AC_PlayerState, MySkill) | 状态 |
| 1.1.4-14 | 技能、替身、秘卷、通灵 | 查克拉同步 Chakra | DOREPLIFETIME(AC_PlayerState, Chakra) | 属性 |
| 1.1.4-15 | 技能、替身、秘卷、通灵 | 查克拉增加 AddChakra | Server_ChangeChakra | 属性 |
| 1.1.4-16 | 技能、替身、秘卷、通灵 | 奥义点清零 FinalSkill | Server_ChangeChakra(0) | 属性 |
| 1.1.5-01 | 动画系统 | 动画状态机 Idle/Run/Attack/Skill/受击等 | 依赖 PlayerState 复制变量 | 状态（表现） |
| 1.1.5-02 | 动画系统 | 状态变量驱动 MyAttack/MyCState/MySkill/MySpeed | 状态复制后驱动 | 否（派生镜像） |
| 1.1.5-03 | 动画系统 | AN_ChangeAttack 切换普攻连段 | 服务器/客户端逻辑 | 状态（本地先行触发点） |
| 1.1.5-04 | 动画系统 | AN_ChangeAttackBox 改变攻击框 | Server_ChangeBox | 属性（部分） |
| 1.1.5-05 | 动画系统 | AN_ChangePalyerBox 改变受击框 | Server_ChangeBox | 属性（部分） |
| 1.1.5-06 | 动画系统 | AN_ChangeState 切换角色状态 | 状态复制 | 状态 |
| 1.1.5-07 | 动画系统 | AN_ChangeDamageValue 修改伤害参数 | 服务器读取 | 否 |
| 1.1.5-08 | 动画系统 | AN_ChangeGravity 控制重力 | Mult_ChangeGravity | 属性（多播更正） |
| 1.1.5-09 | 动画系统 | AN_StartHitCheck 开启命中检测 | 服务器判定 | 属性（命中判定） |
| 1.1.5-10 | 动画系统 | AN_HitJump 命中派生 | 服务器命中标记 | 锁（由 bSuccessHit 派生） |
| 1.1.5-11 | 动画系统 | AN_LockTargetToward 锁定移动意图 | 服务器朝向同步 | 状态 |
| 1.1.5-12 | 动画系统 | AN_MakeMove 技能位移 | 动画通知两端执行，客户端本地先行位移 | 属性（位移，基线回滚） |
| 1.1.5-13 | 动画系统 | AN_PlaySound 播放音效 | 本地表现 | 表现（记录） |
| 1.1.5-14 | 动画系统 | AN_PreInput 预输入窗口 | 服务器攻击逻辑 | 锁（bPreInputLock） |
| 1.1.5-15 | 动画系统 | AN_SetGrab 生成抓取点 | 服务器生成，抓取点同步 | 属性（部分） |
| 1.1.5-16 | 动画系统 | AN_SetOtherPauseState 对手停顿 | 服务器 SetPlayerPauseState | 否 |
| 1.1.5-17 | 动画系统 | AN_SpawnAttacker 生成攻击体 | 服务器 Spawn | 属性（部分） |
| 1.1.5-18 | 动画系统 | AN_SpawnSE 生成特效 | 多播/复制 | 表现（记录） |
| 1.1.5-19 | 动画系统 | AN_StopGrab 结束抓取 | 服务器状态恢复 | 属性（部分） |
| 1.1.5-20 | 动画系统 | AN_Summon 秘卷/通灵 | 服务器召唤 | 状态 |
| 1.1.5-21 | 动画系统 | AAN_ChangeBox 攻击体碰撞开关 | 服务器控制 | 属性（部分） |
| 1.1.5-22 | 动画系统 | AAN_Over 攻击体销毁 | 服务器销毁 | 属性（部分） |
| 1.1.5-23 | 动画系统 | AAN_SetDamage 攻击体伤害 | 服务器伤害 | 属性（部分） |
| 1.1.6-01 | UI 系统 | 大厅 UI Host/Join/等待/Cancel | ClientTravel、联机标识 | 否 |
| 1.1.6-02 | UI 系统 | 战斗 UI 布局血量、查克拉、技能栏 | 本地 UI | 否 |
| 1.1.6-03 | UI 系统 | 时间显示剩余时间 | Client_SetWidgetTime | 否 |
| 1.1.6-04 | UI 系统 | 血量显示双方血量 | HealthValue 复制 | 属性（读取预测值） |
| 1.1.6-05 | UI 系统 | 血量条蓝/红 | HealthValue 复制 | 属性（读取预测值） |
| 1.1.6-06 | UI 系统 | 查克拉槽 4 格 | Chakra 复制 | 属性（读取预测值） |
| 1.1.6-07 | UI 系统 | 技能 CD 显示替身/技能/秘卷/通灵 | 服务器 CD 计算，客户端 Tick 读取 | 属性（读取预测值） |
| 1.1.6-08 | UI 系统 | 奥义栏亮起 4 奥义点 | Chakra 复制 | 属性（读取预测值） |
| 1.1.6-09 | UI 系统 | 通灵栏状态使用前亮/后灰 | CD 状态 | 属性（读取预测值） |
| 1.1.6-10 | UI 系统 | 冷却图标灰色 CD 中灰色 | 本地 UI | 属性（读取预测值） |
| 1.1.6-11 | UI 系统 | CD 文本整数秒 | 本地 UI | 属性（读取预测值） |
| 1.1.6-12 | UI 系统 | 入场/结束动画 WidgetGameStart/End | Client_ShowWidget、Client_SetWidgetEnd | 否 |
| 1.1.6-13 | UI 系统 | Z 轴碰撞显示碰撞摄像头画面 | 客户端捕获碰撞体信息 | 否 |
| 1.1.6-14 | UI 系统 | 结算显示胜/负/平 | Client_SetWidgetEnd | 否 |
| 1.1.7-01 | 摄像机系统 | 碰撞摄像头捕获碰撞体给 UI | 客户端捕获，提供给 UI | 否 |
| 1.1.7-02 | 摄像机系统 | 跟踪摄像头正交俯视侧向 | bReplicates = true | 否 |
| 1.1.7-03 | 摄像机系统 | 客户端激活 Client_Activate | Client RPC | 否 |
| 1.1.7-04 | 摄像机系统 | 追踪目标本地玩家 Pawn | IsLocalController 校验 | 否 |
| 1.1.7-05 | 摄像机系统 | 追踪偏移根据 Toward 调整 | 读取角色复制朝向 | 否（被动跟随预测值） |
| 1.1.7-06 | 摄像机系统 | 边界限制 Min/Max | 本地摄像机逻辑 | 否 |
| 1.1.7-07 | 摄像机系统 | 平滑插值 VInterpTo | 本地表现 | 否 |
| 1.1.7-08 | 摄像机系统 | 视角切换 SetViewTarget | 客户端执行 | 否 |

### 1.2 网络机制层

原文档 1.1.8「网络同步与 RPC」中 1.1.8-01 ~ 1.1.8-27 与 1.1.2 ~ 1.1.7 各模块重复登记（同一属性、同一 RPC 在两处出现且标注不一致），已删除；对应条目以各模块原编号为准。本模块只保留**机制层**条目——它们不是功能点，而是预测系统赖以工作的前提。

| 编号 | 机制 | 说明 | 对预测系统的意义 |
| --- | --- | --- | --- |
| 1.1.8-01 | 属性复制频率 | `AC_PlayerState::NetUpdateFrequency = 100` | 决定权威值到达客户端的延迟上限 |
| 1.1.8-02 | 移动复制 | `SetReplicateMovement(true)` → `AActor::ReplicatedMovement`（`COND_SimulatedOrPhysics`，`ActorReplication.cpp:490`） | **只到模拟代理（对手侧）**。拥有者收不到自身移动复制（`bNetSimulated` 判定，`DataChannel.cpp:3511`），自身位置一致性由移动组件的 `ServerMove` → `ClientAdjustPosition` 校正通道保证。**因此位置预测不能依赖"复制兜底"，只能用一次性基线**（见 2.9） |
| 1.1.8-03 | 组件复制 | `AttackBox / PlayerBox / Flipbook` 等 `SetIsReplicated(true)` | 组件变换本身（`RelativeLocation` / `RelativeRotation` / `RelativeScale3D`）走默认 `COND_None`（`SceneComponent.cpp:3513-3515`），**拥有者也收得到**；但决定命中几何的两个量——`UBoxComponent::BoxExtent`（裸 `UPROPERTY`，`BoxComponent.h:23-24`）与碰撞启用状态（`BodyInstance` 未注册复制）——**根本不走复制**。因此碰撞框变化的唯一路径是 `Server_ChangeBox` → `Mult_ChangeBoxSize` 往返，拥有者本地几何滞后一个 RTT，"拥有者本地命中判定"是接入前置项（见 5.9-b） |
| 1.1.8-04 | 无缝传送 | `ServerTravel` / `SeamlessTravel` | 跨关卡时 Character 重建，预测上下文自然重置 |
| 1.1.8-05 | 返回大厅 | `ClientTravel(TRAVEL_Absolute)` | 同上，客户端重进关卡时预测上下文一并重置 |

---

## 2. 预测系统设计

### 2.1 系统划分

本项目为**服务器权威**架构，同步由原有网络层（属性复制 / Server RPC / Client RPC / NetMulticast）负责。预测系统为**外置模块**，负责客户端的表现即时响应，以及服务器校验后的接受或回滚。

项目里预测系统真正要处理的，为**状态预测、属性预测、锁预测**三类：

- **状态预测**处理客户端输入后本地立即切换的状态变量，包括 `CharacterState`（攻击、技能、受击、保护、被抓取等）、`MySkill`（技能一/二/奥义/秘卷/通灵）、`Attack`（普攻连段）。动画状态机读取这些状态后立即切换动画。
- **属性预测**处理客户端输入后本地立即修改的数值、位置、朝向、时间戳，包括 `Chakra`（技能/替身预扣）、`Toward`（本地翻转）、位置（技能位移、替身瞬移本地先行）、CD 时间戳（本地记录技能释放时间）。此外，还包括**对敌方本地代理的影响**：攻方客户端预测自己命中后，敌方代理的 `HealthValue` 与 `CharacterState` 变化（见 2.5.2）——**己方血量不由本人预测**，只有"攻方预测敌方"这一个方向。UI 读取这些属性后立即更新血量条、查克拉槽、CD 显示。
- **锁预测**处理客户端输入后本地立即置位的输入控制开关，包括 `bAttackInputLock`、`bPreInputLock`、`bSuccessHit`。

**表现触发（动画、特效、音效、UI、摄像机）不是第四类预测**。它们是读取上述三类数据后的本地表现，在预测系统中只作为「表现记录」参与跟进/回滚，不单独作为预测类别；UI、摄像机本身不纳入预测范围，只是被动读取预测后的值。

### 2.2 系统设计

三类预测的共同点是：都由客户端输入触发，都在本地立即生效，都需要与服务器结果配对，以决定「跟进」还是「回滚」。三者的差异在于**本地立即改变的对象不同**：

| 类别 | 本地立即改变的对象 | 是否建预测键 | 是否记录变化 | 回滚依据 |
| --- | --- | --- | --- | --- |
| 状态预测 | 状态变量 | 是（建键并持有生命周期） | 非复制状态记变化记录；可复制状态只写标记 | 非复制：变化记录；可复制：权威影子 |
| 属性预测 | 数值、位置、朝向、时间戳 | 否（共享当前活跃键） | 数值/朝向/时间戳写标记；位置只记一次性基线 | 数值/朝向/时间戳：权威影子；位置：一次性基线 |
| 锁预测 | 输入控制开关 | 否 | 不记录 | 不参与回滚；由服务器 `Client_CorrectLocks` 直接覆盖 |

**回滚依据共三类**，不要在文档与代码中混用：

| 依据 | 适用对象 | 何时建立 | 何时使用 |
| --- | --- | --- | --- |
| **权威影子** | 服务器权威、会被反复写入、收得到 `OnRep` 的属性 | 属性复制到达时（含初始复制；**前提是注册了 `REPNOTIFY_Always`**，见 2.7.1） | 拒绝 / 超时时恢复本地值 |
| **一次性基线** | 位移这类一次性离散写入的量（位置类属性） | 预测键创建后、位移执行前，捕获一次 | 拒绝 / 超时时恢复位置 |
| **变化记录 + 委托** | 非复制数据（本地状态变量、表现） | 本地修改时逐条记录 | 确认执行跟进委托；拒绝执行回滚委托 |

为了统一管理「本地先行」与「服务器同步」的配对关系，**状态预测与属性预测共用一套标识机制——预测键（PredictionKey）**。每个预测行为生成一个预测键，键下挂载该行为衍生的所有状态变化、属性标记、位置变化、表现触发，以及对应的回滚/跟进委托。预测键的有效窗口等于其标记的状态生命周期，状态结束则键冻结。

**锁预测不依托预测键**。原因是锁的语义是「输入控制开关」：其最终控制权明确在服务器，客户端只做本地预判，服务器同步到达后直接覆盖即可；锁的置位/复位与状态变化同步发生，不需要独立的生命周期管理，也不需要旧值记录与委托。

### 2.3 预测键

预测键（PredictionKey）是预测系统的**唯一标识单元**，用于标记客户端每一次预测行为的完整生命周期。其核心作用是：将客户端本地先行产生的状态、属性、表现与后续服务器的同步结果进行配对，从而支持「跟进」或「回滚」两种处理路径。

- 预测键采用**静态自增的 uint32 计数器**生成，由客户端本地维护，保证同一客户端内唯一。生成时机为「预测状态变化的瞬间」，即客户端输入触发本地先行逻辑、导致状态变化的那一刻。
- 预测键**不跨客户端同步**。客户端在发送 Server RPC 时把预测键附带在参数中，服务器只把它原样回传，用于客户端定位本地预测记录。
- 每个预测键的有效窗口**严格等于其标记的状态生命周期**：状态开始时生成；状态持续期间保持活跃，所有属于该状态的属性标记、变化记录、表现触发都挂载到该键下；状态结束时**冻结**，不再接受新的记录。
- 状态结束的判定依据为**服务器同步或本地状态机切换**。例如技能状态在服务器处理 `Server_ChangeSkillState` 后、`AC_PlayerState::MySkill` 属性复制到达时结束；攻击状态在连段窗口关闭时结束。
- 预测键本身不直接执行回滚或跟进，而是通过**委托（Delegate）**实现：每个预测键在组件中对应一组委托列表，记录该键下所有受影响的非复制数据的恢复/确认回调。

#### 2.3.1 冻结与结算

- **冻结**（`EndPredictionKey`）：只把预测键标记为 `bFrozen = true`，停止接受新的变化记录；**不执行结算**。
- **结算**（`ResolvePrediction`）：由服务器回执驱动，是唯一的结算入口，决定跟进或回滚。冻结键同样参与结算。
- **超时**（`TickPredictionTimeout`）：覆盖**所有 `!bResolved` 的记录**（含已冻结键），超过 `PredictionTimeout`（默认 2.0s）未结算则强制回滚，避免本地预测状态永久残留。

> **实现约束**：超时判定必须使用**世界时间**与记录中的 `StartTime` 比较，不能累加 `DeltaTime`。玩家被时停时（`CustomTimeDilation = 0`）`DeltaSeconds` 为 0，累加式计时会导致其预测永不超时。

#### 2.3.2 单活跃键与缓冲池

同一时刻同一客户端**只允许一个活跃（未冻结）预测键**；若需开新键，由状态预测先冻结旧键再创建新键。

但「冻结」不等于「已结算」，因此实际会出现「旧键冻结未结算 + 新键活跃」并存的情况。组件为此维护**未结算预测键缓冲池**：

| 项 | 规则 |
| --- | --- |
| 容量 | 复用 `MaxPredictionRecords`（默认 64，可配置） |
| 入池 | 预测键被冻结时 |
| 出池 | `ResolvePrediction` 结算后，或超时强制回滚后 |
| 淘汰 | 只淘汰**已结算**记录（按时间淘汰最旧）；**未结算记录不得淘汰** |

> 注意：普攻（`Attack`）与技能（`MySkill`）是两条独立的服务器通道，代码中可同时非零（例如连段中释放技能）。因此不能断言「同一时刻只有一个状态」，缓冲池是必要的。

### 2.4 状态预测

#### 2.4.1 处理对象与职责

状态预测是**预测键的创建者与生命周期持有者**。它处理客户端输入后本地立即切换的状态变量，分两类：

| 类别 | 变量 | 处理方式 |
| --- | --- | --- |
| 可复制状态变量 | `AC_PlayerState::CharacterState`、`MySkill`、`Attack` | 本地先行修改 + 写预测标记；结算由**权威影子**兜底 |
| 非复制状态变量 | `AC_Character::LaunchState`、`bInProtectAnim` | 本地先行修改 + 写变化记录 + 注册委托；结算时跟进或回滚 |

> **`AC_Character` 上的 `MyAttack` / `MyCState` / `MySkill` 不作为预测对象。** 它们是 `GetInformation()` 每帧从 `AC_PlayerState` 派生的本地镜像（`C_Character.cpp:580-588`），预测直接作用于 `AC_PlayerState` 的复制属性，镜像自动跟随。把它们也列为预测对象会造成同帧覆盖与重复回滚。

状态预测的职责：

1. 创建预测键：在客户端输入触发本地先行逻辑时创建预测键，返回预测键 ID。
2. 预测状态变量本身（可复制 → 标记；非复制 → 变化记录 + 委托）。
3. 持有预测键生命周期：状态持续期间保持活跃；状态结束时冻结预测键。
4. 服务器同步后结算状态变量（可复制 → 由影子兜底；非复制 → 回执确认跟进 / 拒绝回滚）。

状态预测**不负责**该状态衍生的属性变化、锁变化、表现触发；这些分别由属性预测、锁预测、表现记录在同一预测键下处理。

#### 2.4.2 流程与示例

每个状态预测行为生成一个预测键，键下记录：

- 可复制状态变量的预测标记（属性名 + 预测键 ID）；
- 非复制状态变量的变化记录（旧值、新值、时间戳、跟进/回滚委托）；
- 状态进入时间戳；
- 状态生命周期绑定（到达指定值时自动冻结本键）。

**示例：玩家释放技能一**

1. 本地生成预测键 `PK_001`，并绑定生命周期 `BindStateLifecycle(PK_001, "Self.PS.MySkill", 0)`（技能结束时自动冻结）。
2. `AC_PlayerState::MySkill` 本地置为「技能一」，`CharacterState` 置为「技能」；标记 `Self.PS.MySkill`、`Self.PS.CharacterState` 正在被 `PK_001` 预测。
3. 动画状态机读取后立即播放技能动画。
4. 服务器处理 `Server_ChangeSkillState` 后，`MySkill` / `CharacterState` 属性复制到达 → `OnRep` 更新权威影子（**依赖 `REPNOTIFY_Always`**，见 2.7.1）、清除预测标记、采用权威值。
5. `Client_ResolvePrediction` 回执到达 → 结算 `PK_001`：确认则执行跟进委托；拒绝则执行回滚委托，**非复制状态变量按变化记录恢复旧值、可复制属性从影子恢复**，重新按权威值驱动动画。

#### 2.4.3 关键设计点

- 可复制状态变量：本地先行修改，**只写标记**，不记录旧值、增量、委托；结算由权威影子兜底。
- 非复制状态变量：本地先行修改，记录变化记录 + 委托，回执确认 → 跟进，拒绝 → 回滚。
- 状态生命周期与预测键绑定：状态开始生成键，状态结束冻结键；冻结不结算。
- 职责边界：状态预测只预测状态变量本身；衍生的属性、锁、表现由其它模块在同一预测键下处理。
- 回滚策略：非复制状态变量恢复旧值并重新触发动画状态机切换；可复制状态变量从影子恢复（不是"等属性复制覆盖"，理由见 2.7）。

### 2.5 属性预测

属性预测处理客户端输入后本地立即修改的数值、位置、朝向、CD 时间戳，以及**对敌方本地代理的影响**。它**不创建预测键**，共享状态预测创建的预测键。

#### 2.5.1 可复制属性

可预测的可复制属性分三类，**记录方式与回滚依据各不相同**：

**A. 己方角色可预测属性**（由客户端输入直接触发）

| 来源 | 属性 | 回滚依据 |
| --- | --- | --- |
| `AC_PlayerState` | `Chakra`、`Attack`、`MySkill`、`CharacterState` | 权威影子 |
| `AC_Character` | `Toward`、`LastEscapeTime`、`LastFirstSkillTime`、`LastSecondSkillTime`、`LastScrollTime`、`LastSummonTime`（四个技能 CD 时间戳见 5.3 待实现项） | 权威影子 |

> `Self.PS.HealthValue` **不在**其中：血量不由本人预测，只由攻击方预测敌方代理（见 2.5.2）。
> `Team` **不**纳入属性预测：它只在服务器 `AssignTeams` 中赋值，客户端不存在本地先行修改队伍的场景。

**B. 敌方本地代理可预测属性**（由攻击方预测，见 2.5.2）

| 属性 | 回滚依据 |
| --- | --- |
| `Enemy.PS.HealthValue`、`Enemy.PS.CharacterState` | 权威影子（敌方 PlayerState 的这两个属性正常复制到本地；`OnRep` 本身需按 5.2 补 `ReplicatedUsing` + `REPNOTIFY_Always` 后才可用） |

**C. 位置类属性**（技能位移、替身瞬移）

位置**用一次性基线回滚，不用影子**。原因：本地控制角色的自身位置不作为常规复制属性下发——`ACharacter` 的移动同步属性全部标 `COND_SimulatedOnly`（`Character.cpp:1620-1627`），`AActor::ReplicatedMovement` 用 `COND_SimulatedOrPhysics`（`ActorReplication.cpp:490`），而条件判定取 `bIsSimulated = (RemoteRole == ROLE_SimulatedProxy)`（`DataChannel.cpp:3511` → `RepLayout.cpp:7142/7152/7156`），**拥有该 Actor 的连接 `RemoteRole` 是 `AutonomousProxy`，因此收不到这些属性**。所以"维护服务器权威值"在位置上无从做起，也无需做：位移是每个预测键一次性的离散跳变，每个键在自己开始时重新捕获基线即可，天然不存在"同属性多次预测"的级联问题。完整推导见 2.9。

**记录方式**：

| 类 | 记录内容 |
| --- | --- |
| 数值 / 朝向 / 时间戳 | 只在预测标记表写入「属性名 → 预测键 ID」，并在预测记录的 `ReplicatedAttributes` 中登记本键涉及的属性名。**不记录旧值、增量、回滚委托**——旧值由权威影子统一维护（见 2.7）。 |
| 位置 | 不写预测标记，只在预测记录的 `MoveBaseline`（`FVector`）中记录**位移执行前的位置**，并置 `bHasMoveBaseline = true`。 |

**结算方式**：

| 结果 | 数值 / 朝向 / 时间戳 | 位置 |
| --- | --- | --- |
| 确认 | 属性复制到达 → 影子已更新、标记已清除，无需额外处理 | 无需处理（服务器采纳后位置一致） |
| 拒绝 / 超时 | **从权威影子恢复本地值**，清除标记 | **从 `MoveBaseline` 恢复位置** |

#### 2.5.2 敌方代理属性

受击、被抓取、击飞**不由本地输入触发**，因此不作为状态预测，而归入属性预测：由**攻击方客户端**预测其攻击对**敌方本地代理**造成的影响。

- 预测对象：敌方 `AC_PlayerState::HealthValue`、`CharacterState`（在本地代理上本地先写）；
- 触发源：本地的攻击输入 + 本地攻击框命中判定（`AttackBox` 与目标 `PlayerBox` 在客户端均已存在）；
- 记录方式与结算方式与 2.5.1 相同，属性名以 `Enemy.` 前缀区分；
- **不对称性**：只有攻击方预测；受击方（被预测方）不预测自身血量与状态变化，其表现仍等服务器复制到达。

> **前提（必须先落地）**：本地命中判定要求攻击者客户端上 `AttackBox` 的尺寸/偏移/翻转与该动画帧的服务器状态一致。当前实现中，攻击框变化**唯一**的路径是 `Server_ChangeBox`（Server RPC）→ `Mult_ChangeBoxSize`（NetMulticast）往返（`C_Character.cpp:161-171` → `127-152`），因此拥有者本地要等一个 RTT 才拿到新几何。注意这里**不是**"拥有者收不到组件复制"——`USceneComponent` 的 `RelativeLocation` / `RelativeRotation` / `RelativeScale3D` 用默认 `COND_None`（`SceneComponent.cpp:3513-3515`，`FDoRepLifetimeParams` 默认值见 `UnrealNetwork.h:137`），拥有者同样收得到；真正不走复制的是 `UBoxComponent::BoxExtent`（裸 `UPROPERTY`，`BoxComponent.h:23-24`）与碰撞启用状态（`BodyInstance` 未注册复制），而这两者恰好是决定命中几何的量。**命中预测必须在"碰撞框本地先行"（见 5.9）落地后才能启用**；在此之前攻击命中仍走服务器判定。

> **范围界定**：敌方代理只预测**血量与状态**，不预测敌方**位移**（击飞、被抓取时的移动）。敌方是模拟代理，其位移由服务器权威下发（`Mult_ChangeGrabLocation` 与击飞状态由服务器 Tick 驱动），本地预测敌方位置会与该多播的写入互相打架。若后续实测"敌方击飞滞后"影响手感，再单独评估。

**示例：普攻命中**

1. 本地攻击框 Overlap 判定命中敌方代理；
2. 本地预判：`Enemy.PS.HealthValue` 减去伤害值，`Enemy.PS.CharacterState` 置为僵直；标记这两个属性正在被当前预测键预测；
3. 本地立即播放命中派生动画与特效；
4. 服务器权威判定后：命中成立 → 敌方属性复制到达 → 影子更新、标记清除；命中不成立 → 回执拒绝 → 从影子恢复敌方血量与状态，撤销本地表现。

#### 2.5.3 非复制数据

非复制数据包括：

- 本地状态变量：`LaunchState`、`bInProtectAnim`（`MyAttack` / `MySkill` / `MyCState` 是派生镜像，不计入）；
- 本地表现：动画状态机切换、Flipbook 翻转/透明度、特效、音效、UI 立即更新；
- 本地生成物：本地预测生成的攻击体、特效。

这些数据的权威值不来自属性复制，需要**完整变化记录 + 回滚/跟进委托**：确认 → 执行跟进委托；拒绝 → 执行回滚委托，恢复旧值。

#### 2.5.4 关键设计点

- 可复制属性：数值 / 朝向 / 时间戳只记预测标记，回滚依据是权威影子；**位置不写标记，只记 `MoveBaseline`**（见 2.5.1-C）。
- 非复制数据：记录完整变化记录 + 委托。
- 敌方代理属性：只有攻击方预测，受击方不预测；且只预测**血量与状态**，不预测敌方位移（见 2.5.2 范围界定）。
- CD 时间戳：属于可复制属性，本地先行记录，服务器同步到达后采用权威值。
- 伤害数值：`Enemy.PS.HealthValue` 属于可复制属性，由攻击方本地先行预判。
- 不创建预测键：属性预测共享状态预测创建的预测键。
- 命中预测有前置：必须先完成碰撞框本地先行（见 5.9），否则整体降级为服务器判定。

### 2.6 锁预测

锁预测处理客户端输入后本地立即置位的输入控制开关，包括 `bAttackInputLock`、`bPreInputLock`、`bSuccessHit`。锁属于**非复制数据**，处于状态和属性的上游，用于判断「状态是否可变化」。

**锁预测不创建预测键，也不记录变化，更不参与跟进/回滚结算**。原因是锁的语义是「输入控制开关」：最终控制权在服务器，客户端只做本地预判；锁的置位/复位与状态变化同步发生，不需要生命周期管理，也不需要旧值（回滚无从谈起——服务器说不行就不行）。

三个锁的具体规则：

| 锁 | 本地先行 | 服务器更正 |
| --- | --- | --- |
| `bAttackInputLock` | 普攻输入瞬间本地置位，阻止后续攻击输入 | 服务器 `Server_Attack` / `ChangeAttack` / `PlayerStateReset` 写入权威值后，通过 `Client_CorrectLocks` 下发 |
| `bPreInputLock` | 预输入缓存本地置位，缓存下一次输入 | 同上（消费预输入时服务器置位/复位） |
| `bSuccessHit` | 命中判定瞬间本地置位，用于提前触发命中派生动画 | 服务器 `OnAttackBoxOverlap` 写入权威判定后下发；攻击结束时的复位点见 5.6 |

锁预测的接入点主要在 `AC_Character` 的攻击、技能、动画通知等函数中：原有代码只需在本地置位/复位锁处保持原样，并在**服务器每一处写锁的位置**调用 `Client_CorrectLocks` 下发权威值（见 2.8）。

#### 2.6.1 关键设计点

- **本地预判**：锁在客户端输入瞬间即置位/复位，用于本地输入控制与表现触发。
- **服务器权威覆盖**：服务器写锁的位置统一通过 `Client_CorrectLocks` 向拥有者客户端下发权威值，客户端直接覆盖。
- **不建键、不记录、不结算**：锁不占用预测键、变化记录、委托与缓冲池中的任何资源。
- **独立性**：锁的更正通道独立于状态与属性的回滚路径，避免与预测键结算耦合。

### 2.7 权威影子（Prediction Shadow）

#### 2.7.1 为什么需要它

UE 的属性复制**只在服务器侧属性值发生变化时下发**：每个连接对每个属性维护"上次发出的值"，值没变就不发包。这带来两个后果：

1. **服务器接受预测**：服务器修改了属性 → 值变化 → 复制必然到达 → 客户端采用权威值。此路径无需额外机制。
2. **服务器拒绝预测**：服务器不修改属性 → 值没变 → **不会有任何属性复制到达** → 如果客户端把回滚"交给属性复制"，本地预测值将**永久残留**。

> 当前服务器侧的 `Server_ChangeSkillState` / `Server_ChangeChakra` 等是裸赋值、无校验，因而"永远会改值、永远会复制"，掩盖了这个洞。一旦按本文档补齐服务器校验（见 5.1），拒绝路径就会出现，必须由客户端本地回滚兜底。

因此，**可复制属性的回滚依据不是属性复制，而是权威影子**。

**还有一个更隐蔽的洞**：默认的 `ReplicatedUsing` 语义下，接收端是否触发 `OnRep`，取决于「刚收到的值」与「客户端当前本地值」是否相同，而不是与「上一次收到的值」是否相同：

```cpp
// RepLayout.cpp:3335-3349（接收端）
// 先把【当前本地值】存进影子缓冲，再反序列化收到的值
StoreProperty(Cmd, ShadowData + Cmd, Data + SwappedCmd);
Cmd.Property->NetSerializeItem(Bunch, Bunch.PackageMap, Data + SwappedCmd);

// 比较的是【收到的值】与【本地当前值】
if (Parent.RepNotifyCondition == REPNOTIFY_Always
    || !PropertiesAreIdentical(Cmd, ShadowData + Cmd, Data + SwappedCmd, NetSerializeLayouts))
{
    RepNotifies->AddUnique(Parent.Property);
}
else
{
    // 引擎日志："Skipping RepNotify for property %s because local value has not changed."
}
```

而「客户端本地值恰好等于服务器权威值」恰恰是**预测被采纳时的常态**——本地已经先写成了服务器将要写的值。于是：

1. 服务器接受预测 → 复制到达 → 收到的值与本地值相同 → **`OnRep` 不触发** → 影子保持旧值；
2. 之后某次预测被拒绝、需要回滚时，影子会把这个**过期的旧值**写回本地，凭空制造一次错值。

这不是偶发边界，而是"接受路径"上的必然。因此：

> **被影子跟踪的属性必须注册为 `REPNOTIFY_Always`**，强制每次收到复制都触发 `OnRep`，即使收到的值与本地值相同。

```cpp
// 声明侧仍是普通的 ReplicatedUsing
UPROPERTY(ReplicatedUsing = OnRep_Chakra)
int32 Chakra;

// 注册侧改用带 rncond 的变体（UnrealNetwork.h:287-295）
DOREPLIFETIME_CONDITION_NOTIFY(AC_PlayerState, Chakra, COND_None, REPNOTIFY_Always);
```

`REPNOTIFY_Always` 是 `ELifetimeRepNotifyCondition` 的取值之一，默认值 `REPNOTIFY_OnChanged`（`UnrealNetwork.h:143`）。

**附带好处**：`REPNOTIFY_Always` 让"服务器接受预测"这条路径也走 `OnRep`，于是**接受路径与拒绝路径的到达方式统一**——组件在 `OnRep` 里只需做一件事：更新影子 + 清除预测标记（见 2.11.3）。

#### 2.7.2 定义与使用

**权威影子**是预测组件内部维护的一张表：为每个被预测的可复制属性保存**服务器最新权威值**。它**只在属性复制到达（`OnRep`）时更新**，本地预测写入绝不触碰它。

| 时机 | 动作 |
| --- | --- |
| 属性复制到达（`OnRep`） | 更新影子为该权威值；清除该属性的预测标记 |
| 预测开始（本地写值前） | 写入预测标记（属性名 → 预测键 ID）；影子已就位，无需另行记录旧值 |
| 服务器拒绝 / 预测超时 | **从影子恢复到本地**，并清除预测标记 |

**前提**：这些属性必须按 2.7.1 注册 `REPNOTIFY_Always`。否则"服务器接受预测"时 `OnRep` 不触发，影子不更新，"更新影子"这一行就是空的。

**适用范围**：影子只覆盖「服务器权威、会被反复写入、且客户端收得到复制」的属性，即 2.5.1-A / 2.5.1-B 中的**数值、朝向、时间戳**。**位移（位置）不进影子**，改用一次性基线（见 2.5.1-C 与 2.9）——拥有者客户端根本收不到自身位置的复制。

**两条防护规则**（回滚时逐项判断）：

1. 该属性**已无预测标记**（说明权威值已到达并在 `OnRep` 中清除）→ **跳过**，不要用旧值覆盖权威值；
2. 该属性的标记**已易主**（`Mark[Name] != 本键 ID`，说明已被新键接管）→ **跳过**，由新键负责。

**接管规则**：新键标记一个已有标记的属性时，旧键记录的 `ReplicatedAttributes` 中应移除该属性名。

#### 2.7.3 命名约定

预测对象的属性名统一带**目标与来源前缀**，避免 `AC_PlayerState::MySkill` 与 `AC_Character::MySkill` 同名冲突：

| 前缀 | 含义 | 示例 |
| --- | --- | --- |
| `Self.PS.*` | 己方 PlayerState 上的可复制属性（走影子） | `Self.PS.MySkill`、`Self.PS.CharacterState`、`Self.PS.Chakra`、`Self.PS.Attack` |
| `Self.Char.*` | 己方 Character 上的可复制属性/本地变量 | `Self.Char.Toward`、`Self.Char.LastEscapeTime`、`Self.Char.LaunchState` |
| `Enemy.PS.*` | 敌方本地代理上的可复制属性（走影子） | `Enemy.PS.HealthValue`、`Enemy.PS.CharacterState` |
| `Self.Move.*` | 位置类预测（走一次性基线，不入影子） | `Self.Move.Displacement` |

> 血量只在 `Enemy.PS.HealthValue` 上预测，**没有** `Self.PS.HealthValue`。

#### 2.7.4 与 COD 式预测的区别

COD / Quake 式客户端预测需要维护**历史状态队列 + 重放**，因为其预测对象是连续物理量（位置、速度需要逐帧重演）。本项目不必如此：

- 预测对象全是**离散写值**（状态机切换、属性赋值、锁置位），不存在需要重演的中间过程，因此**只需要一个"当前影子"，不需要历史队列与重放**；
- 唯一的连续量是常规移动，而它已由 UE `CharacterMovementComponent` 自带的预测与校正覆盖（见 2.9）。

#### 2.7.5 实现说明

- 影子表：`TMap<FName, FAuthoritativeShadowEntry>`，条目含标量值（可字符串化）槽位。**位置不占影子条目**（走一次性基线，见 2.5.1-C）。
- 影子的更新依赖各属性的 `OnRep`。建议 `AC_PlayerState` / `AC_Character` 提供非动态多播委托 `FOnReplicatedAttribute(FName AttributeName)`，在各自的 `OnRep` 中广播；预测组件在客户端绑定**己方与敌方的 PlayerState**（通过 `GameState->PlayerArray` 定位，做法与 `AC_PlayerController::Client_ShowWidget` 一致）。
- 被预测的可复制属性需要 `ReplicatedUsing`（当前均为裸 `Replicated`，见 5.2），**且注册时必须用 `REPNOTIFY_Always`**（见 2.7.1），二者缺一不可。

### 2.8 锁更正通道

锁不加 `Replicated`，其权威值由一条专用 Client RPC 下发：

```cpp
// 定义在 AC_Character 上（角色已 SetOwner 给 PlayerController，可正确路由到拥有者客户端）
UFUNCTION(Client, Reliable)
void Client_CorrectLocks(uint8 LockMask, uint8 LockValues);
// LockMask / LockValues 位域约定：
//   bit0 = bAttackInputLock
//   bit1 = bPreInputLock
//   bit2 = bSuccessHit
```

**服务器调用点**（每一处写锁的位置都必须下发）：

| 位置 | 写入内容 |
| --- | --- |
| `AC_Character::Server_Attack_Implementation` | `bPreInputLock = true`、`bAttackInputLock = true` |
| `AC_Character::ChangeAttack` | 连段结束 `bAttackInputLock = false`；预输入被消费分支 |
| `AC_PlayerController::PlayerStateReset` | 受击/抓取/击飞打断时 `bAttackInputLock = false` |
| `AC_Character::OnAttackBoxOverlap` | `bSuccessHit = true` |
| 新增复位点 | `bSuccessHit` 的复位（见 5.6） |

**约定**：

- 使用 `Reliable`，与项目现有 RPC 风格一致；同一 Actor 的可靠 RPC 保序。
- 客户端收到后**直接覆盖**本地锁值，不检查预测键（锁不参与结算）。
- 迟到覆盖：若服务器已决定 `false` 并发出，而客户端本地又置了 `true`，更正到达后会覆盖较新的本地预测（表现为一帧闪烁，下一次服务器写锁会再次纠正）。如需消除，可在参数中附带 `KeyID`，客户端忽略比当前预测更旧的更正。
- 若服务器是**主动改锁**（如 `PlayerStateReset` 把 `bAttackInputLock` 由 true 改为 false），本次写锁本身也会触发下发，不存在"值没变不下发"的问题——该问题只出现在拒绝路径，而拒绝路径正是本 RPC 覆盖的场景。

### 2.9 移动、位移与位置

| 对象 | 归属 | 机制 |
| --- | --- | --- |
| 常规移动（WASD） | **不进入预测系统** | UE `CharacterMovementComponent` 自带客户端预测与服务器校正；输入意图经 `Server_SetTryTargetToward` 同步 |
| 移动范围限制 | 不进入预测系统 | 由原有 Tick 逻辑在两端各自执行 |
| 技能位移（`AN_MakeMove`） | 属性预测（位置，**一次性基线回滚**） | 动画通知在两端执行，本地先行位移（`AddActorLocalOffset`）；服务器位置经移动校正通道兜底 |
| 替身瞬移 | 属性预测（位置，**一次性基线回滚**） | 客户端本地先行（见 5.4 待实现项）；服务器 `SetActorLocation` 权威 |

**为什么位置不能用影子**（这也是"位置只能走基线"的根据）：

`ACharacter` 的移动同步属性全部标 `COND_SimulatedOnly`（`Character.cpp:1620-1627`：`RepRootMotion`、`ReplicatedBasedMovement`、`ReplicatedMovementMode`、`bIsCrouched` 等）；而 `AActor::ReplicatedMovement` 用的是 `COND_SimulatedOrPhysics`（`ActorReplication.cpp:490`）。两者的条件判定都取 `bIsSimulated`，它来自 `RepFlags.bNetSimulated = (Actor->GetRemoteRole() == ROLE_SimulatedProxy)`（`DataChannel.cpp:3511`，在其上一行 `FScopedRoleDowngrade` 完成"非拥有连接降级"之后）。条件映射见 `RepLayout.cpp:7142/7152/7156`。

结论：**拥有该角色的连接，其 `RemoteRole` 是 `AutonomousProxy` 而非 `SimulatedProxy`，因此收不到自身角色的这些移动属性**（`bRepPhysics` 为假时 `COND_SimulatedOrPhysics` 同样为假）。所以客户端无法从属性复制中得知"服务器认为我在哪"，影子在位置上根本无从维护；自身位置的一致性由移动组件自带的预测校正通道保证（`ServerMove` → 服务器 `ServerCheckClientError` 判定超差 → `ClientAdjustPosition`，`CharacterMovementComponent.cpp:9808/9914/10551`）。反过来，**敌方角色在本机是模拟代理**，`bNetSimulated` 为真，其位置复制正常到达——所以观感上"敌人在动"，而"自己瞬移后服务器没动"却不会自动弹回来。

因此位置的记录与回滚改用**一次性基线**：

| 时机 | 动作 |
| --- | --- |
| 预测键创建后、执行位移前 | 捕获当前位置到 `FPredictionRecord::MoveBaseline`，置 `bHasMoveBaseline = true` |
| 服务器接受 | 无需处理：服务器执行同样的位移，位置自然一致 |
| 服务器拒绝 / 超时 | `SetActorLocation(MoveBaseline, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics)` 恢复 |

- 基线**每键独立**：位移是离散的一次性跳变，一个键只发生一次位移（多段位移拆成多个键），因此不存在"同一属性被反复预测"的级联问题，也不需要影子那种"服务器最新值"的语义。
- 位移执行语句仍留在动画通知里（两端各自执行），以保证动画与位移对齐；客户端多执行的那一份由基线兜底。
- 恢复时机：`Rejected` 回执到达时立即恢复；若回执丢失未到，由预测键超时（见 2.10.1）兜底恢复。

### 2.10 时间戳语义

预测系统中涉及两类时间戳，语义不同，不能混用。

#### 2.10.1 预测时间戳

指「这次预测行为发生在客户端本地时间的哪一刻」。用途：

- **预测键超时管理**：判断预测键是否超过 `PredictionTimeout`，超时后强制回滚，避免本地预测状态永久残留；
- **调试与日志**：记录预测行为完整时间线，便于调试网络延迟、回滚频率、预测命中率。

预测时间戳**不参与「服务器确认还是拒绝」的判定**，也不用于"服务器校验请求时效"。最终判定依据是服务器权威状态。

> 附：预测键同时不跨客户端同步，因此不存在"多个客户端按时间戳排序"的语义。

#### 2.10.2 CD 时间戳

指「技能/替身释放时刻的服务器世界时间」。用途：

- **CD 倒计时计算**：`CD剩余 = CD总时长 - (服务器当前时间 - LastXXXTime)`；
- **服务器权威校验**：服务器检查 `服务器当前时间 - LastXXXTime >= CD总时长`，未满足则拒绝。

预测场景下：客户端本地预测释放技能时，本地记录 `LastXXXTime = 服务器世界时间估算值`，UI 立即开始倒计时；服务器确认后由属性复制覆盖为权威值；若服务器拒绝，由**影子**恢复本地值，UI 随之恢复。

CD 时间戳属于可复制属性，必须是**服务器世界时间**，不能是客户端本地时间；其回滚由影子兜底，预测系统只需记录预测标记。

### 2.11 服务器确认与回滚

#### 2.11.1 回执参数设计

服务器通过 Client RPC 回执告知客户端预测结果。**该 RPC 定义在 `AC_PlayerController` 上**，而不是 `AC_Character` 上：

- 预测键本来就是"每个客户端一份"的语义（每个客户端各自持有活跃键），`AC_PlayerController` 是引擎保证的**每连接唯一** Actor，路由最直接；
- 角色 Actor 在换人/重生时会被销毁重建（`AC_ArenaGM::SpawnPawnToPlayer` 里 `Player->GetPawn()->Destroy()` 后重新 Spawn），把回执挂在角色上会让"回执在途时角色已被替换"变成一个真实的竞态；
- 回执需要携带"键属于哪个连接"的判断依据，放在 PlayerController 上最自然。

```cpp
// AC_PlayerController
UFUNCTION(Client, Reliable)
void Client_ResolvePrediction(
    uint32 KeyID,
    uint8 Result,                // 0 = Confirmed, 1 = Rejected
    uint8 ConfirmedStatePacked   // 打包的 CharacterState / MySkill
);
```

- `KeyID`：预测键 ID，客户端据此找到本地预测记录。**键表中找不到该 ID 时静默返回**（回执迟到于超时回滚属正常竞态，见 3.4.5）。
- `Result`：只有两种结果，确认或拒绝（其他值按 Rejected 处理并记录 Warning）。
- `ConfirmedStatePacked`：服务器确认的关键状态值，用于客户端跟进时校正本地非复制状态变量。打包方式：高 4 位 `CharacterState`（0–15），低 4 位 `MySkill`（0–15），即 `(CharacterState << 4) | (MySkill & 0x0F)`。

**属性值不塞进 RPC，交给原有属性复制同步**。原因：属性复制已经在做这件事，RPC 再传会重复；避免"RPC 值"和"复制值"时序不一致；RPC 保持精简。

> **唯一例外**：锁的 3 个布尔量由 `Client_CorrectLocks` 携带（锁不复制、不记录，且拒绝路径下没有属性复制可依赖）。

**为什么不需要额外的"键所有者"字段**：本 RPC 由服务器**定向发给某一个连接**，天然只有该连接的客户端会收到；再加上 `FPredictionKey` 的自定义 `NetSerialize` 只对发起连接写真实值（见 3.3.1），键不可能在错误的客户端上被当成有效键。因此参数里只需要裸 `KeyID`（回收端先用 `IsValidKey` 语义找键，找不到即忽略），不需要携带连接标识。

#### 2.11.2 回滚边界

若服务器拒绝状态变化，客户端回滚的边界由**预测键下的记录**决定——即：预测键下登记了哪些内容，就回滚哪些。

| 回滚项 | 依据 |
| --- | --- |
| 非复制状态变量 | `LaunchState`、`bInProtectAnim` 恢复旧值（变化记录 + 回滚委托） |
| 本地表现 | 重新按权威状态驱动动画状态机、UI（表现记录委托） |
| 本地生成物 | 销毁本地预测生成的攻击体、特效，或标记为无效 |
| 可复制属性（数值 / 朝向 / 时间戳，含敌方代理） | **从权威影子恢复**（`Self.PS.*`、`Self.Char.*`、`Enemy.PS.*`） |
| 位置（位移 / 瞬移） | **从 `MoveBaseline` 恢复**（`Self.Move.*`），不走影子 |
| 锁 | 不在回滚边界内；由 `Client_CorrectLocks` 直接覆盖 |

> 回滚逐项判断，遵循 2.7.2 的两条防护规则（已无标记 → 跳过；标记已易主 → 跳过）。位置项额外判断 `bHasMoveBaseline`，未捕获过基线的键不恢复位置。

#### 2.11.3 结算流程

1. **客户端预测**：状态预测创建预测键；属性预测写预测标记、记录非复制数据变化；锁预测本地置位；表现记录注册回调。
2. **服务器校验**：只校验状态变化是否可行（含 CD、当前状态是否允许等）。
3. **服务器回执**：`Client_ResolvePrediction(KeyID, Result, ConfirmedStatePacked)`。
4. **客户端结算**（`ResolvePrediction`，唯一入口）：
   - **确认** → 执行跟进委托；清除非复制数据预测标记；**可复制属性标记保留**，等属性复制到达后清除。
   - **拒绝** → 执行回滚委托，恢复非复制数据旧值；**从权威影子恢复本键登记的数值/朝向/时间戳属性**；**从 `MoveBaseline` 恢复位置**；清除对应标记。
5. **属性复制到达**：`OnRep` → 更新权威影子、清除预测标记、采用权威值。**该路径依赖 `REPNOTIFY_Always`**（见 2.7.1）：预测被采纳时收到的值与本地值相同，默认语义下这一步根本不会触发。
6. **多播 / 客户端 RPC 到达**：按 `MulticastName` 从标记表移除对应标记（语义：该数据的权威值已到，回滚时跳过它）；不触发委托。
7. **后续同步**：服务器多播、RPC 到达后，按原有逻辑修正客户端表现。

**约定**：

- 服务器只确认状态变化；后续变化由网络同步修正。
- **整体确认与整体回滚**：预测键结算只有两种结局，不做部分回滚。理由：服务器校验粒度本来就是状态级；可复制属性差异由影子或属性复制修正；逐条确认的复杂度与收益不成正比。若未来出现"状态确认了但非复制数据无法自动修正"的场景，再考虑引入部分回滚。
- `ConfirmPrediction` / `RollbackPrediction` 为 private，外部只能通过 `ResolvePrediction` 触发结算。
- 结算后再次调用 `ResolvePrediction` 忽略并记录 Warning。
- 结算后若可复制属性标记仍未清除（服务器值恰好未变化、复制未到达），保留至超时清理，或由下一次 `MarkReplicatedAttribute` 覆盖。

#### 2.11.4 服务器端调用时机

服务器在以下时机调用 `Client_ResolvePrediction`：

| 时机 | 说明 | 是否需要回执 |
| --- | --- | --- |
| **带键的 Server RPC 到达** | 客户端发送 `Server_Attack`、`Server_Escape`、`Server_ChangeSkillState` 时携带 `FPredictionKey`；服务器校验后回执 | 需要（`KeyID > 0`） |
| **不带键的同名 Server RPC 到达** | 客户端处于降级路径（`CanPredict() == false`）时直接发原 RPC，`KeyID == 0` | **不需要**：没有键可回执，服务器照常校验执行 |
| **服务器主动打断** | 服务器因受击、暂停、时停等**自行**改变客户端状态 | **不需要回执**，见下 |

**服务器主动打断为什么不回执**：这类打断的权威结果**本来就是通过属性复制与多播到达客户端的**（服务器改了 `CharacterState` → 属性复制；改了锁 → `Client_CorrectLocks`；踢飞 → 多播）。客户端的本地预测值会被这些权威同步直接覆盖，客户端的预测键随后由**超时**（`TickPredictionTimeout`）收敛——不需要服务器专门发一条回执去"否认"一个它从未见过的键。

> 预测键不跨客户端同步：服务器只对**随 RPC 到达过的键**有认知（`FPredictionKey::PredictiveConnectionKey` 也正是这么记下来的），因此"服务器对未知键主动回执"在机制上就不可行，在设计上也不需要。

**RPC 归属带来的顺序问题（已接受）**：三个可带键的 RPC 并不在同一个 Actor 上——`Server_Attack` / `Server_Escape` 定义在 `AC_Character` 上，`Server_ChangeSkillState` 定义在 `AC_PlayerController` 上；而同一处输入还会伴随 `AC_Character` 上的 `Server_ChangeBox` / `Server_ChangeToward`。它们分属**不同的 ActorChannel**，UE 只保证"同一 Actor 上的可靠 RPC 保序"，跨 Actor 不保证。因此服务器处理 `Server_ChangeSkillState` 时，**不能假设**同一次输入里的 `Server_ChangeBox` 已经先到。

处理办法是让它们不互相依赖：**键只挂在状态变更请求上**（见 3.4.2 的挂载约束），碰撞框等同步自身携带完整参数（尺寸 / 偏移 / 翻转），服务器按"最后到达者为准"处理即可，不需要与键建立顺序关系。

### 2.12 预测系统实现方式

预测系统并非对原有网络层、角色逻辑或动画系统的重写，而是在现有项目之上叠加的一层**外置预测模块**。原有架构仍保持服务器权威：属性复制、Server RPC、Client RPC、NetMulticast 继续负责权威同步与表现分发；预测系统只负责在客户端输入瞬间先行产生本地结果，并在服务器同步到达后决定「跟进」还是「回滚」。因此实现原则是：**尽量不影响项目原先架构和代码，尽量以增量方式接入**。

基于这一原则，预测系统采用**组件（Component）形式**实现：组件挂载到角色上，随角色一同生成、复制和销毁，持有该角色的预测上下文；对外暴露预测键创建、预测标记、变化记录、委托注册、跟进/回滚结算等接口。角色原有逻辑不需要了解预测系统的内部结构，只需要在「需要预测的功能函数」中调用组件接口，并在适当位置注册少量回调。

具体接入方式：

- **组件挂载**：在角色初始化时创建并挂载预测组件，与角色生命周期一致，服务器与客户端均可持有；预测逻辑只在客户端生效。
- **预测键创建与使用**：客户端输入触发本地先行逻辑时，由角色调用组件接口创建预测键；键下挂载本次预测衍生的状态变化、属性标记、位置变化、表现触发以及回滚/跟进委托。
- **原有代码的增量修改**：原有功能函数不需要被替换，只需在关键位置插入组件调用——状态本地置位处调用 `CreatePredictionKey`；可复制属性本地修改处调用 `MarkReplicatedAttribute`；非复制数据本地修改处调用 `RecordStateChange` / `RecordPresentation`；服务器同步到达处调用 `ResolvePrediction` / `OnReplicatedAttributeArrived` / `OnMulticastArrived`。
- **委托回调的增量添加**：原有代码只需为需要跟进或回滚的非复制数据增加少量委托回调，回调内容通常是「恢复旧值」「确认新值」「重新按权威值驱动动画/UI」。可复制属性不需要委托——由权威影子统一负责。
- **锁的接入**：保持本地置位/复位不变，在服务器写锁处调用 `Client_CorrectLocks`；不涉及预测键、记录与委托。
- **动画通知的两类分法**：接入时必须先把现有动画通知切成两类（见 3.8 与 5.9）。**时机类**（连段推进、位移 `AN_MakeMove`、碰撞框变更）承载的是时序信号，两端都要执行，且是"本地先行 + 记录"的首写点；**纯权威类**（特效、音效、纯表现开关）应改为 `HasAuthority()` 门控，只由服务器触发、再经多播分发。当前项目中两类混写（`HasAuthority()` 判断与两端执行交错），需要一并整理——否则预测系统会把"两端各播一次"的问题放大成"回滚后又播一次"。
- **降级策略（重要）**：预测系统不是"必须存在"的。若 `AC_Character` 上找不到预测组件，或组件判定当前不可预测（`CanPredict() == false`，如非本地控制、观战、调试开关关闭），各处接入点**回退到原有路径**——直接发原 Server RPC，不建键、不写标记、不记变化。这既保证"未接入 / 被关闭"时游戏仍按原逻辑跑，也是逐功能增量接入的基础。

因此，预测系统的实现可以概括为：**一个挂载在角色上的组件，一套预测键与预测标记机制，一张权威影子，一组由原代码注册的跟进/回滚委托**。

---

## 3. 预测系统组件

预测系统组件（后文简称预测组件）是预测系统的**唯一入口与运行时容器**。它挂载在 `AC_Character` 上，与角色生命周期一致，负责：

- **预测上下文管理**：保存当前客户端是否处于可预测状态、当前活跃预测键、历史预测记录等。
- **预测键生命周期管理**：创建、冻结、结算、销毁预测键。
- **预测标记管理**：为可复制属性记录「预测标记 + 预测键 ID」，在属性复制到达时更新影子并清除标记。
- **权威影子管理**：维护被预测可复制属性的服务器最新值，供回滚恢复使用。
- **非复制数据变化记录管理**：为非复制数据提供变化记录的写入、查询、回滚、确认接口。
- **委托注册与结算**：集中保存每个预测键的跟进/回滚委托，并在服务器同步到达时统一触发。
- **锁更正接收**：接收 `Client_CorrectLocks` 下发的权威锁值并覆盖本地锁（不建键、不记录）。
- **服务器同步配对**：在服务器回执 / 属性复制 / 多播到达时，根据预测键 ID 找到对应记录，决定跟进还是回滚。

### 3.1 挂载位置与生命周期

预测组件挂载在 `AC_Character` 上，因为预测系统本地先行修改的对象主要是 Character 上的状态、位置、朝向、锁，以及 PlayerState 上的权威属性；组件挂 Character 可以直接访问二者。

预测系统需要随 Character 生成而创建、随 Character 销毁而销毁。跨关卡传送（Lobby → Arena 的 `SeamlessTravel`，以及返回大厅的 `ClientTravel(TRAVEL_Absolute)`）时 Character 重新 Spawn，预测上下文自然重置，避免残留上一局的预测键与变化记录。

**适用范围**：预测作用于

1. **己方 Pawn 自身**的状态、属性、位置、锁；
2. **敌方 Pawn 本地代理**的血量与状态（仅由攻击方预测，且不含位移，见 2.5.2）。

**本地控制判断**由调用方负责——组件不判断 `IsLocallyControlled()`，输入函数入口处自行完成判断；组件的 `CanPredict()` 只判断预测上下文是否有效。

### 3.2 组件类与头文件规范

```cpp
// 文件路径：Source/Naruto/C_PredictionComponent.h
// 文件路径：Source/Naruto/C_PredictionComponent.cpp

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "C_PredictionComponent.generated.h"

class AC_Character;
class AC_PlayerState;
class AC_PlayerController;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class NARUTO_API UC_PredictionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UC_PredictionComponent();
    // ... 接口
};
```

| 项 | 规范 |
| --- | --- |
| 类名 | `UC_PredictionComponent` |
| 父类 | `UActorComponent` |
| UCLASS 宏 | `UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))` |
| 头文件路径 | `Source/Naruto/C_PredictionComponent.h` |
| 源文件路径 | `Source/Naruto/C_PredictionComponent.cpp` |
| API 宏 | `NARUTO_API` |
| BlueprintSpawnableComponent | 允许蓝图挂载；预测逻辑主要在 C++ 调用，蓝图只需挂载、无需实现 |

**约定**：

- 组件**不继承** `USceneComponent`：预测组件不参与场景变换，只做逻辑管理。
- 组件在 `AC_Character` 构造函数中通过 `CreateDefaultSubobject<UC_PredictionComponent>(TEXT("PredictionComponent"))` 创建，不手动 `SetupAttachment`。
- 组件**不单独开启 Tick**：`PrimaryComponentTick.bCanEverTick = false;`，超时检查由 `AC_Character::Tick` 调用 `TickPredictionTimeout` 完成。
- 预测组件内部持有引用：

| 引用 | 用途 |
| --- | --- |
| `AC_Character*` | 访问位置、朝向、Flipbook、动画状态变量、锁。 |
| `AC_PlayerState*` | 读写 Chakra、CharacterState、MySkill、Attack、HealthValue、LastXXXTime 等权威属性。 |
| `AC_PlayerController*` | 发送带预测键的 Server RPC，并作为 `AC_PlayerController::Client_ResolvePrediction` 的宿主接收回执（见 2.11.1）。 |
| 敌方 `AC_PlayerState*` | 预测敌方本地代理的血量与状态（通过 `GameState->PlayerArray` 定位）。 |

- 服务器端同样挂载预测组件，但**只**用于：读取随 RPC 到达的 `FPredictionKey`（其 `PredictiveConnectionKey` 由 `NetSerialize` 记下）、在回执时把键原样带回、执行服务器校验。**不执行任何本地先行逻辑**——`CanPredict()` 在服务器上恒为 false。

### 3.3 存储结构

组件内部维护以下容器：

| 容器 | 类型 | 用途 |
| --- | --- | --- |
| 预测键表 | `TMap<uint32, FPredictionKey>` | 存储**活跃与未结算**的预测键，键为预测键 ID，值为轻量预测键结构。结算后移除。 |
| 预测记录表 | `TMap<uint32, FPredictionRecord>` | 存储每个预测键对应的完整本地记录。 |
| 预测标记表 | `TMap<FName, uint32>` | 键为属性名（含 `Self.` / `Enemy.` 前缀），值为预测键 ID，表示该属性当前正在被哪个预测键预测。 |
| 权威影子表 | `TMap<FName, FAuthoritativeShadowEntry>` | 被预测可复制属性的服务器最新值（**仅标量**），只在属性复制到达时更新，**前提是这些属性注册了 `REPNOTIFY_Always`**（见 2.7.1）。 |
| 委托表 | `TMap<uint32, FPredictionDelegates>` | 每个预测键的跟进/回滚委托列表，按注册顺序执行。 |
| 未结算缓冲池 | `TArray<uint32>` | 已冻结、待结算的预测键 ID；结算或超时回滚后出池。 |

**约定**：

- 预测记录保留上限：`MaxPredictionRecords`（默认 64，可配置）。**只淘汰已结算记录**（按时间淘汰最旧）；未结算记录不得淘汰。
- 预测键表保存「活跃键 + 未结算键」；结算后从表中移除。
- 预测标记表只保留"尚未收到权威值"的属性；属性复制到达时清除。**位置类属性从不进入该表**（只记 `MoveBaseline`，见 2.5.1-C）。

#### 3.3.1 结构体定义

```cpp
// 轻量预测键，跨 RPC 传入服务器；对"非发起连接"自动失效（GAS 式）
// 注意：成员必须带 UPROPERTY，否则不会被序列化，RPC 传过去全是默认值
USTRUCT()
struct FPredictionKey
{
    GENERATED_BODY()

    UPROPERTY() uint32 KeyID = 0;
    UPROPERTY() uint8 PredictionType = 0;       // EPredictionType
    UPROPERTY() float ClientRequestTime = 0.f;  // 仅用于调试与超时管理，不参与确认/拒绝判定

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
        KeyID = 0;   // 非发起连接：键自动变为无效，调用方只需查 IsValidKey()
    }

    if (Ar.IsLoading() && KeyID > 0)
    {
        // 服务器读到了某客户端给的键：记下这个连接的标识，之后只回传给该连接
        PredictiveConnectionKey = (UPTRINT)Map;
    }

    bOutSuccess = true;
    return true;
}
```

> **为什么 RPC 参数也能用自定义 `NetSerialize`**：UE 的 RPC 参数不走普通序列化，而是走 `FRepLayout`。完整链条（可逐级核对）：
>
> 1. `UNetDriver::ProcessRemoteFunctionForChannelPrivate` → `RepLayout->SendPropertiesForRPC(...)`（`NetDriver.cpp:2268-2270`）
> 2. `FRepLayout::SendPropertiesForRPC`（`RepLayout.cpp:6943`）→ `SerializeProperties_r`（`:6996`）
> 3. → `Cmd.Property->NetSerializeItem(Ar, Map, ...)`（`RepLayout.cpp:6690`）
> 4. `FStructProperty::NetSerializeItem`（`PropertyStruct.cpp:178-199`）在结构体带 `STRUCT_NetSerializeNative` 标志时调用 `CppStructOps->NetSerialize(Ar, Map, bSuccess, Data)`
>
> 而 `TStructOpsTypeTraits<...>::WithNetSerializer = true` 正是设置 `STRUCT_NetSerializeNative` 的方式（GAS 的 `FPredictionKey` 就是这么做的，`GameplayPrediction.h:424-432`）。所以该机制对 **Server RPC 与 NetMulticast 参数同样生效**，不限于复制属性。
>
> **服务器侧的一步依赖**：`PredictiveConnectionKey` 只在服务器**读到过**该键之后才有值，因此服务器只能对"随 RPC 到达过的键"回传有效键（与 2.11.4 的结论一致）；服务器自己发起的多播若带一个从未收到过的键，则在所有连接上都是无效键——这正是我们想要的语义。

// 以下结构体只存在于客户端，不跨 RPC，使用普通 struct 即可（无需反射）
// 预测键 ID 为 0 表示无效键（不再单独维护 bValid 标志）

// 状态生命周期绑定
struct FStateLifecycleBinding
{
    FName StateName;
    uint8 EndStateValue = 0;
};

// 非复制状态变化记录
struct FStateChangeRecord
{
    FName StateName;
    FString OldValue;   // 仅支持可字符串化的值
    FString NewValue;
    float Timestamp = 0.f;
};

// 表现触发记录
struct FPresentationRecord
{
    FName PresentationName;
};

// 权威影子条目（只存数值 / 枚举 / 布尔 / 时间戳；位置不入影子）
struct FAuthoritativeShadowEntry
{
    FString TextValue;   // 标量属性（数值 / 枚举 / 布尔 / 时间戳），仅支持可字符串化的值
    bool bValid = false; // 是否已收到过服务器权威值
};

// 委托列表
DECLARE_DELEGATE(FConfirmDelegate);
DECLARE_DELEGATE(FRollbackDelegate);

struct FPredictionDelegates
{
    TArray<FConfirmDelegate> ConfirmDelegates;
    TArray<FRollbackDelegate> RollbackDelegates;
};

// 预测记录，存储在预测组件中
struct FPredictionRecord
{
    uint32 KeyID = 0;
    TArray<FName> ReplicatedAttributes;                 // 本预测键涉及的可复制属性名（含前缀）
    TArray<FStateChangeRecord> StateChanges;            // 非复制状态变化记录
    TArray<FPresentationRecord> Presentations;          // 表现触发记录
    TArray<FStateLifecycleBinding> LifecycleBindings;   // 状态生命周期绑定列表
    FVector MoveBaseline = FVector::ZeroVector;         // 位置类预测的一次性基线（位移执行前的位置）
    bool bHasMoveBaseline = false;                      // 本键是否捕获过位置基线
    float StartTime = 0.f;
    float EndTime = 0.f;
    bool bResolved = false;   // 是否已结算
    bool bConfirmed = false;  // 结算结果：true = 确认，false = 回滚
    bool bFrozen = false;     // 是否已冻结（EndPredictionKey 后置位）
};
```

| 结构体 | 用途 |
| --- | --- |
| `FPredictionKey` | 轻量预测键，只存 ID、类型、客户端请求时间；跨 RPC 传入服务器。带自定义 `NetSerialize`：**只对发起连接有效**，其他连接读到 `KeyID = 0`。 |
| `FPredictionRecord` | 完整预测记录，存属性标记、变化记录、生命周期、**位置基线**；只存客户端。**不含委托列表**，委托集中存于委托表。 |
| `FStateLifecycleBinding` | 状态生命周期绑定，记录 `StateName` 与 `EndStateValue`。 |
| `FStateChangeRecord` | 非复制状态变量变化记录。 |
| `FPresentationRecord` | 表现触发记录。 |
| `FAuthoritativeShadowEntry` | 权威影子条目，存服务器最新权威值（仅标量；位置不入影子）。 |
| `FPredictionDelegates` | 每个预测键的跟进/回滚委托列表。 |

**约定**：

- `FPredictionKey` 必须轻量，不存储变化记录、委托列表、旧值/新值。
- `FPredictionKey` 是唯一带自定义 `NetSerialize` 的结构体；`PredictiveConnectionKey` 是它唯一的非 `UPROPERTY` 成员，由 `NetSerialize` 手工维护。
- `FPredictionKey::KeyID > 0` 即"有效键"。**收到 `KeyID == 0` 时不要记 Warning**：这是"该键不属于本连接"的正常表示。
- `FPredictionRecord` 只存客户端，不跨 RPC。
- 委托使用 `DECLARE_DELEGATE` 而非动态多播，保证性能；绑定对象生命周期由原代码保证。
- `FStateChangeRecord::OldValue / NewValue` 使用 `FString`，仅支持可字符串化的值（枚举、int、float、bool、FName）。
- 时间戳使用 `GetWorld()->GetTimeSeconds()` 或服务器世界时间估算值。

#### 3.3.2 预测类型枚举

```cpp
UENUM(BlueprintType)
enum class EPredictionType : uint8
{
    None        UMETA(DisplayName = "None"),
    Attack      UMETA(DisplayName = "Attack"),      // 普攻
    Skill       UMETA(DisplayName = "Skill"),       // 技能
    Escape      UMETA(DisplayName = "Escape"),      // 替身
    Scroll      UMETA(DisplayName = "Scroll"),      // 秘卷
    Summon      UMETA(DisplayName = "Summon")       // 通灵
};
```

| 枚举值 | 用途 |
| --- | --- |
| `None` | 无效预测类型。 |
| `Attack` | 普攻预测，对应 `Self.PS.Attack` 与连段状态。 |
| `Skill` | 技能预测，对应 `Self.PS.MySkill` / `CharacterState`。 |
| `Escape` | 替身预测，对应 `MySkill`、`Chakra`、`LastEscapeTime`、位置瞬移。 |
| `Scroll` | 秘卷预测。 |
| `Summon` | 通灵预测。 |

**约定**：

- `PredictionType` 由状态预测在 `CreatePredictionKey` 时传入，服务器据此分类处理。属性预测、表现记录不单独定义类型，共享状态预测的类型。
- 受击、被抓取、击飞、保护**不是**预测类型：它们不由本地输入触发，而是作为「攻击方对敌方本地代理的影响」走属性预测路径，其影响记录在攻击方的预测键下（见 2.5.2）。

### 3.4 对外接口

预测组件对外暴露的接口按职责分为**五类**：生命周期接口、预测键接口、预测标记接口、非复制数据变化记录接口、结算接口。

#### 3.4.1 生命周期接口

```cpp
// 组件初始化时调用，缓存 AC_Character / AC_PlayerState / AC_PlayerController 引用
// 返回 true 表示上下文有效；false 表示角色/PlayerState/Controller 缺失
bool InitializePredictionContext();

// 判断预测上下文是否有效（组件已初始化、角色有效、PlayerState 有效）
// 不判断 IsLocallyControlled，本地控制判断由调用方在输入函数入口处完成
// 返回 false 时，调用方必须回退到原有路径（直接发原 Server RPC，不建键/不标记/不记录）
bool CanPredict() const;

// 获取当前活跃预测键 ID，无活跃键时返回 0
uint32 GetActivePredictionKeyID() const;

// 获取当前活跃预测键的轻量结构，用于填充 Server RPC 参数
// 无活跃键时返回 KeyID = 0
FPredictionKey GetActivePredictionKey() const;
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `InitializePredictionContext` | `AC_Character::BeginPlay` 或组件 `BeginPlay` | 缓存角色、PlayerState、Controller 引用，并绑定己方/敌方 PlayerState 的属性复制委托。返回 false 时后续预测接口调用应直接忽略或断言。若 PlayerState 后续同步到达，可再次调用重试。 |
| `CanPredict` | 每次预测行为前 | 只判断预测上下文是否有效，不判断本地控制。返回 false 时调用方**降级**到原有路径（见 2.12），这是逐功能增量接入的基础。 |
| `GetActivePredictionKeyID` | 属性预测、表现记录需要挂载到当前键时 | 若当前无活跃键，返回 0，调用方应自行决定是否创建新键。 |
| `GetActivePredictionKey` | Server RPC 发送前，填充 RPC 参数 | 返回轻量预测键结构，供 RPC 携带到服务器。 |

#### 3.4.2 预测键接口

```cpp
// 创建预测键，返回预测键 ID；由状态预测调用
// 若当前已有活跃键，断言（Debug）并返回 0（Shipping）
uint32 CreatePredictionKey(uint8 PredictionType);

// 冻结预测键：停止接受新的变化记录，但不执行结算
// 冻结时自动进入未结算缓冲池
// 若 KeyID 不存在，忽略并记录 Warning 日志
void EndPredictionKey(uint32 KeyID);

// 绑定状态生命周期与预测键：状态到达指定值时自动冻结预测键
// StateName 取值：带前缀的可复制属性名（"Self.PS.CharacterState" / "Self.PS.MySkill" / "Self.PS.Attack"）
// EndStateValue：状态结束的目标值（uint8 形式）
// 检查触发点：AC_Character::Tick 中调用 TickPredictionTimeout 时一并检查
// 支持多绑定：一个预测键可绑定多个 StateName；任一满足即冻结
void BindStateLifecycle(uint32 KeyID, FName StateName, uint8 EndStateValue);

// 根据预测键 ID 查找预测记录，找不到返回 nullptr
FPredictionRecord* FindPredictionRecord(uint32 KeyID);
const FPredictionRecord* FindPredictionRecord(uint32 KeyID) const;

// 判断预测键是否仍然活跃（未冻结、未结算）
bool IsPredictionKeyActive(uint32 KeyID) const;
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `CreatePredictionKey` | 客户端输入触发本地先行逻辑、状态变化瞬间 | 生成自增 uint32 ID；同时创建轻量预测键与预测记录。已有活跃键时断言并返回 0。 |
| `EndPredictionKey` | 状态结束时（本地状态机切换） | 只将预测键标记为已冻结（`bFrozen = true`），停止接受新变化记录；**不执行结算**；同时入未结算缓冲池。 |
| `BindStateLifecycle` | 创建预测键后，绑定状态结束条件 | 当指定状态变量到达指定值时，自动调用 `EndPredictionKey`。 |
| `FindPredictionRecord` | 服务器回执到达、属性复制到达、多播到达 | 按 ID 定位预测记录。非 const 版本用于结算时修改；const 版本用于只读查询。 |
| `IsPredictionKeyActive` | 结算前校验 | 判断预测键是否仍活跃，避免重复结算。 |

**约定**：

- 预测键由状态预测创建并持有生命周期；属性预测、表现记录不创建预测键，只共享当前活跃键。
- 同一时刻同一客户端**只允许一个活跃（未冻结）预测键**；若需开新键，由状态预测先冻结旧键再创建新键。冻结键与新键可以并存，由未结算缓冲池统一管理。
- 预测键 ID 为 0 表示无效键。
- `EndPredictionKey` 只冻结，不结算；结算由 `ResolvePrediction` 执行。若服务器回执超时，由 `TickPredictionTimeout` 强制回滚。
- `BindStateLifecycle` 的 `StateName` 若不在 `AC_PlayerState` 上，忽略并记录 Warning；`EndStateValue` 与属性类型不匹配时，忽略并记录 Warning。

**键的挂载约束**（与 2.11.4 一致，此处是接口侧的硬约束）：

| 约束 | 内容 |
| --- | --- |
| 只有三个 RPC 可以带键 | `Server_Attack`、`Server_Escape`（定义在 `AC_Character` 上）、`Server_ChangeSkillState`（定义在 `AC_PlayerController` 上）。**不为任何其他 RPC 新增预测键参数。** 三者分属两个 Actor，因此**服务器不能假设它们之间有序**（见 2.11.4 末段）。 |
| 一条输入一个键一条 RPC | 禁止为同一个本地先行逻辑同时发两条带键 RPC；也禁止"一条带键 RPC + 一条独立建键 RPC"。 |
| 键只挂在请求上 | 禁止把预测键挂到服务器 → 客户端的同步 RPC 上（`Mult_*` / `Client_*`）。键是**请求的凭据**；服务器 → 客户端的同步若需要区分对象，用 `FPredictionKey` 参数携带，由键自身的 `NetSerialize` 决定"只有发起连接读得到"。 |
| 服务器收到 `KeyID == 0` | 按**非预测请求**处理：照常校验并执行，但不回执（没有键可回执）。这让"客户端降级"在服务器侧不需要任何特殊分支。 |

#### 3.4.3 预测标记接口（可复制属性）

```cpp
// 标记某个可复制属性正在被预测，记录预测键 ID
// 首次标记时确保该属性的权威影子已存在
// 若该属性已有预测标记，更新为新 KeyID，从旧键记录的 ReplicatedAttributes 中移除该属性名，并记录 Warning
// 若 KeyID == 0 或 AttributeName 为空，忽略并记录 Warning
void MarkReplicatedAttribute(FName AttributeName, uint32 KeyID);

// 服务器同步到达后更新影子并清除预测标记
// 内部同时更新权威影子为该权威值
// 若该属性无预测标记，仍更新影子，但不记录日志
void OnReplicatedAttributeArrived(FName AttributeName);

// 查询某个可复制属性是否正在被预测
bool IsReplicatedAttributePredicted(FName AttributeName) const;

// 记录位置类预测的【一次性基线】：捕获当前位置到活跃预测键的 MoveBaseline
// 位移执行前调用（动画通知内），每键只调用一次
// 已捕获过基线时忽略并记录 Warning（一个键只发生一次位移；多段位移应拆成多个键）
// 无活跃键时忽略、记录 Warning，并返回 false（调用方据此决定是否仍执行位移）
bool RecordMoveBaseline();
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `MarkReplicatedAttribute` | 属性预测本地修改可复制属性后 | 写入预测标记表；同时把属性名加入预测记录的 `ReplicatedAttributes`。重复标记更新为最新 KeyID（并从旧键移除）并警告。 |
| `OnReplicatedAttributeArrived` | 属性复制到达（`OnRep` 广播）时 | **更新权威影子**为该权威值；清除该属性的预测标记；采用权威值。 |
| `IsReplicatedAttributePredicted` | `OnRep` 中判断是否需要清除标记 | 避免重复清除。 |
| `RecordMoveBaseline` | 位移（技能位移 / 替身瞬移）执行前 | 捕获一次性位置基线，供拒绝/超时时恢复。位置**不走影子**（见 2.5.1-C / 2.9）。 |

**约定**：

- 可复制属性的预测标记由属性复制到达时清除，不由 RPC 回执清除。
- 若 RPC 回执先到，只结算非复制数据与影子恢复；可复制属性标记保留，等待属性复制。
- 不提供批量清除接口；标记由 `OnReplicatedAttributeArrived` 逐个清除，避免清除掉尚未收到属性复制的标记。
- **位置类属性不调用 `MarkReplicatedAttribute`**：位置没有影子、也不需要标记（标记的语义是"等属性复制到达后清除"，而拥有者客户端根本等不到自身位置的复制）。位置只调用 `RecordMoveBaseline`。

#### 3.4.4 非复制数据变化记录接口

```cpp
// 记录非复制状态变量的旧值、新值、时间戳、回滚/跟进委托
// 无活跃预测键时忽略并记录 Warning
void RecordStateChange(FName StateName, const FString& OldValue, const FString& NewValue,
                       float Timestamp, FConfirmDelegate ConfirmDelegate, FRollbackDelegate RollbackDelegate);

// 记录表现触发（动画、特效、音效、UI）的跟进/回滚回调
// 内部自动挂载到当前活跃预测键；委托参与结算
// 不需回滚的表现传空委托即可；无活跃键时忽略并记录 Warning
void RecordPresentation(FName PresentationName, FConfirmDelegate ConfirmDelegate,
                        FRollbackDelegate RollbackDelegate);

// 获取某个预测键下的所有非复制变化记录
const TArray<FStateChangeRecord>& GetStateChangeRecords(uint32 KeyID) const;
const TArray<FPresentationRecord>& GetPresentationRecords(uint32 KeyID) const;
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `RecordStateChange` | 非复制状态变量本地修改后 | 记录旧值、新值、时间戳、跟进/回滚委托；挂载到当前活跃预测键。无活跃键时忽略并警告。 |
| `RecordPresentation` | 表现触发（动画、特效、音效、UI）本地执行后 | 记录跟进/回滚回调；委托参与结算。不需回滚的表现传空委托。无活跃键时忽略并警告。 |
| `GetStateChangeRecords` 等 | 结算时 | 获取完整变化列表，供跟进/回滚委托执行。 |

**约定**：

- 所有变化记录必须挂载到当前活跃预测键；若当前无活跃键，接口内部忽略并记录 Warning。
- 冻结后 `RecordStateChange` / `RecordPresentation` 忽略并记录 Warning。
- 变化记录只针对**非复制数据**；可复制属性不写入变化记录，只写预测标记（回滚依据是权威影子）。
- 变化记录的 `StateName` 必须带来源前缀（`Self.Char.*` 等），避免与可复制属性同名。
- 时间戳使用 `GetWorld()->GetTimeSeconds()` 或服务器世界时间估算值。

#### 3.4.5 结算接口

```cpp
// 统一入口：服务器回执到达时调用，根据结果自动选择跟进或回滚
// Result：0 = Confirmed，1 = Rejected；其他值视为 Rejected 并记录 Warning
// ConfirmedStatePacked：高 4 位 CharacterState，低 4 位 MySkill
// KeyID 在本地键表中找不到时：【静默返回】——回执迟到于超时回滚属正常竞态，不是错误
// 结算后：预测键从预测键表移除；从缓冲池出池；预测记录保留（bResolved = true），供调试
void ResolvePrediction(uint32 KeyID, uint8 Result, uint8 ConfirmedStatePacked);

// 属性复制到达时调用，更新权威影子、清除预测标记、采用权威值
void OnReplicatedAttributeArrived(FName AttributeName);

// 多播 / 客户端 RPC 到达时调用，根据预测键 ID 配对本地预测记录
// MulticastName：多播对应的数据名（如 "GrabLocation" / "ProtectedAnim" / "Gravity" / "BoxSize"）
// 配对规则：按 MulticastName 从预测标记表中移除对应标记（语义：该数据的权威值已到，回滚时跳过它）
// 不触发跟进/回滚委托；找不到对应标记时静默返回（多数多播与本地预测无关，属常态）
void OnMulticastArrived(FName MulticastName);

// 预测键超时检查，超时则自动回滚
// 由 AC_Character::Tick 调用，组件不单独开启 Tick
// 检查范围：所有 !bResolved 的记录（含已冻结键）
// 超时判定：世界时间与记录 StartTime 之差 > PredictionTimeout（默认 2.0s，可配置）
//   —— 禁止累加 DeltaTime，玩家被时停时 DeltaSeconds 为 0
// 超时后行为：调用 RollbackPrediction(KeyID)，标记 bResolved = true、bConfirmed = false，出池
//   —— 回滚内容与 Rejected 完全一致：非复制数据恢复旧值、可复制属性从影子恢复、位置从 MoveBaseline 恢复
// 可复制属性标记不清除，等待属性复制到达后清除
void TickPredictionTimeout(float DeltaTime);

// 只读查询权威影子（调试用）
bool GetAuthoritativeShadow(FName AttributeName, FString& OutValue) const;

private:
    // 服务器回执确认时执行跟进委托，清除非复制数据预测标记
    void ConfirmPrediction(uint32 KeyID, uint8 ConfirmedStatePacked);
    // 服务器回执拒绝/超时时执行回滚委托，恢复非复制数据旧值
    // 数值/朝向/时间戳从影子恢复；位置从 MoveBaseline 恢复（仅当 bHasMoveBaseline）
    void RollbackPrediction(uint32 KeyID);
    // 内部：更新权威影子
    void UpdateAuthoritativeShadow(FName AttributeName);
    // 内部：从权威影子恢复某个属性的本地值
    void RestoreFromShadow(FName AttributeName);
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `ResolvePrediction` | 服务器 `Client_ResolvePrediction` 回执 | 统一入口，根据 Result 自动调用 Confirm 或 Rollback。**键不存在时静默返回**（迟到回执属正常竞态）。结算后预测键移出预测键表与缓冲池；预测记录保留（`bResolved = true`），供调试与历史查询。 |
| `OnReplicatedAttributeArrived` | `OnRep` 广播中 | 更新权威影子；若该属性有预测标记则清除；采用权威值。**依赖 `REPNOTIFY_Always`**（见 2.7.1）。 |
| `OnMulticastArrived` | 多播 / 客户端 RPC 到达时 | 按 `MulticastName` 从预测标记表移除对应标记；不触发委托；无对应标记时静默返回。 |
| `TickPredictionTimeout` | `AC_Character::Tick` 中调用 | 检查**所有未结算记录**是否超时；超时则强制回滚，避免本地预测状态永久残留。必须使用世界时间判定。 |
| `ConfirmPrediction` | `ResolvePrediction` 内部调用 | 执行跟进委托；清除非复制数据预测标记；可复制属性标记保留，等待属性复制。 |
| `RollbackPrediction` | `ResolvePrediction` / `TickPredictionTimeout` 内部调用 | 按变化记录逐项执行回滚委托，恢复非复制数据旧值；**按影子恢复本键登记的可复制属性**（跳过已无标记或已易主的属性）；**位置按 `MoveBaseline` 恢复**。 |

**约定**：

- 服务器只确认状态变化；后续变化由网络同步修正。
- 整体确认与整体回滚：预测键结算只有两种结局，不做部分回滚。
- 可复制属性的预测标记由属性复制到达时清除，不由 RPC 回执清除；但**回滚不再依赖属性复制**——可复制属性的恢复由权威影子完成。
- 回滚边界由预测键下登记的内容决定：记录了哪些非复制变化、标记了哪些可复制属性，就回滚哪些。
- `ConfirmPrediction` 与 `RollbackPrediction` 为 private，外部只能通过 `ResolvePrediction` 触发结算。
- 结算后再次调用 `ResolvePrediction` 忽略并记录 Warning。
- `ResolvePrediction` 的 `ConfirmedStatePacked` 打包方式：高 4 位 `CharacterState`（0–15），低 4 位 `MySkill`（0–15），即 `(CharacterState << 4) | (MySkill & 0x0F)`。
- **"键不存在"与"标记不存在"都不记 Warning**：前者（`ResolvePrediction` / `OnMulticastArrived` 找不到键或标记）是迟到回执、或与本地预测无关的同步，属正常竞态与常态；否则每帧的多播都会刷屏。
- **多播按名字配对，不携带预测键**：现有 4 条 `Mult_*` 都是服务器权威**同步**，不是预测回执，加键参数只会污染既有签名（理由与替代方案见 5.10）。标记表本身保证一个属性名同一时刻只属于一个键，因此按名字清除是无歧义的。
- 回滚的位置项：仅当 `bHasMoveBaseline == true` 时执行恢复；恢复用 `SetActorLocation(Baseline, false, nullptr, ETeleportType::TeleportPhysics)`，不使用 sweep，避免被地形/角色卡住而恢复不到位。

### 3.5 委托约定

预测组件**不单独提供委托注册接口**。所有跟进/回滚委托通过 `RecordStateChange` / `RecordPresentation` 的委托参数传入，由组件集中存入**委托表**（`TMap<uint32, FPredictionDelegates>`），按预测键 ID 映射。结算时预测组件取出该键的委托列表，依次执行。

| 约定 | 内容 |
| --- | --- |
| 传入方式 | 委托由原代码在记录变化时通过参数传入，存入委托表。 |
| 空委托 | 不需回滚的变化（例如纯表现）传空委托。 |
| **执行顺序** | **按注册顺序执行**；接入规范要求「先注册状态委托、后注册表现委托」，以保证表现基于已恢复的状态。 |
| 存储位置 | 委托集中存于组件的委托表，**不在各变化记录中存储**。 |
| 委托类型 | 使用 `DECLARE_DELEGATE` 而非动态多播，保证性能；绑定对象生命周期由原代码保证。 |

### 3.6 组件接口调用时序

#### 3.6.1 示例一：释放技能一

```
客户端输入触发 FirstSkill()
    │
    ├─ CanPredict() 校验上下文
    │
    ├─ CreatePredictionKey(EPredictionType::Skill) → PK_001
    │       BindStateLifecycle(PK_001, "Self.PS.MySkill", 0)   // 技能结束时自动冻结
    │
    ├─ 状态预测：本地置位 + 写预测标记
    │       Self.PS.MySkill = 1；Self.PS.CharacterState = Skill
    │       MarkReplicatedAttribute("Self.PS.MySkill", PK_001)
    │       MarkReplicatedAttribute("Self.PS.CharacterState", PK_001)
    │
    ├─ 属性预测：预扣 Chakra、记录 CD 时间戳
    │       MarkReplicatedAttribute("Self.PS.Chakra", PK_001)
    │       MarkReplicatedAttribute("Self.Char.LastFirstSkillTime", PK_001)
    │
    ├─ 属性预测：位置本地先行（由动画通知 AN_MakeMove 在两端执行）
    │       RecordMoveBaseline()                // 捕获位移前位置（一次性基线，不入影子）
    │       AddActorLocalOffset(Offset)         // 位移本体
    │
    ├─ 锁预测：本地置位
    │       bAttackInputLock = true
    │
    ├─ 表现记录：技能动画、特效、音效
    │       RecordPresentation("SkillAnim", ConfirmDelegate, RollbackDelegate)
    │
    └─ GetActivePredictionKey() → 填充 RPC 参数（FPredictionKey）
            Server_ChangeSkillState(PK) 发送 RPC        // 定义在 AC_PlayerController 上

服务器处理 Server_ChangeSkillState
    │
    ├─ 校验状态是否可行（含 CD）；KeyID == 0 时按非预测请求处理，不回执
    │
    └─ Client_ResolvePrediction(PK, Result, ConfirmedStatePacked) 回执  // 定义在 AC_PlayerController 上

客户端收到回执
    │
    └─ ResolvePrediction(PK_001, Result, ConfirmedStatePacked)
            │
            ├─ Result == Confirmed → ConfirmPrediction(PK_001, ConfirmedStatePacked)
            │       ├─ 执行跟进委托
            │       ├─ 清除非复制数据预测标记
            │       └─ 可复制属性标记保留，等待属性复制到达
            │
            └─ Result == Rejected → RollbackPrediction(PK_001)
                    ├─ 执行回滚委托：恢复非复制状态变量旧值
                    ├─ 从权威影子恢复 Self.PS.MySkill / CharacterState / Chakra
                    ├─ 从 MoveBaseline 恢复位置（若 bHasMoveBaseline）
                    └─ 重新按权威值驱动动画

属性复制到达（OnRep 广播）
    │
    └─ OnReplicatedAttributeArrived("Self.PS.MySkill")
            ├─ 更新权威影子
            └─ 清除预测标记，采用权威值

多播到达（如 Mult_ChangeGrabLocation / Mult_ChangeProtectedAnim）
    │
    └─ OnMulticastArrived("GrabLocation")
            └─ 按名字从预测标记表移除对应标记（该数据权威值已到，回滚时跳过）；
               无对应标记则静默返回（多播不带键，见 5.10）

锁更正到达
    │
    └─ Client_CorrectLocks(Mask, Values)
            └─ 直接覆盖本地锁值
```

#### 3.6.2 示例二：替身（含敌方代理属性）

```
客户端输入触发 Escape()
    │
    ├─ CreatePredictionKey(EPredictionType::Escape) → PK_002
    │
    ├─ 状态预测：Self.PS.MySkill = 替身（本地置位 + 标记）
    │
    ├─ 属性预测：Chakra 本地预扣 1 点
    │       MarkReplicatedAttribute("Self.PS.Chakra", PK_002)
    │       记录影子基线由组件自动维护
    │
    ├─ 属性预测：LastEscapeTime 本地记录时间戳
    │       MarkReplicatedAttribute("Self.Char.LastEscapeTime", PK_002)
    │
    ├─ 属性预测：位置本地先行检测并瞬移到目标点
    │       RecordMoveBaseline()                      // 捕获瞬移前位置
    │       SetActorLocation(TargetLocation)          // 瞬移本体
    │
    ├─ 表现记录：替身动画、特效
    │
    └─ Server_Escape(PK_002)

UI 读取 Chakra 后立即更新为 2 格，CD 显示开始倒计时

服务器 Server_Escape 处理完成后
    ├─ Chakra、LastEscapeTime 属性复制到达 → OnRep → 更新影子 + 清标记
    ├─ 位置【不经属性复制回到本机】：拥有者收不到自身 ReplicatedMovement（见 2.9）；
    │   对手看到的瞬移由其模拟代理的位置复制体现，自身一致性由移动校正通道兜底
    └─ Client_ResolvePrediction(PK_002, Result, ...) → 结算非复制数据

若结果为 Rejected
    └─ RollbackPrediction：Chakra / LastEscapeTime 从权威影子恢复；位置从 MoveBaseline 恢复
```

### 3.7 接口冻结说明

以下接口规范自本文档发布起冻结，后续开发不得随意更改签名与语义：

| 类别 | 冻结内容 |
| --- | --- |
| 组件类 | `UC_PredictionComponent : UActorComponent`，`UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))`，`NARUTO_API`，路径 `Source/Naruto/C_PredictionComponent.h` |
| 生命周期 | `InitializePredictionContext`（返回 bool）、`CanPredict`、`GetActivePredictionKeyID`、`GetActivePredictionKey` |
| 预测键 | `CreatePredictionKey`、`EndPredictionKey`、`BindStateLifecycle`、`FindPredictionRecord`（const / 非 const）、`IsPredictionKeyActive` |
| 预测标记 | `MarkReplicatedAttribute`、`OnReplicatedAttributeArrived`、`IsReplicatedAttributePredicted`、`RecordMoveBaseline`（位置一次性基线） |
| 非复制数据 | `RecordStateChange`、`RecordPresentation`、`GetStateChangeRecords`、`GetPresentationRecords` |
| 结算 | `ResolvePrediction`、`OnMulticastArrived(FName)`、`TickPredictionTimeout`、`GetAuthoritativeShadow` |
| 回执宿主 | `AC_PlayerController::Client_ResolvePrediction`（Client, Reliable；宿主为 PlayerController 而非 Character，理由见 2.11.1） |
| 锁更正 | `AC_Character::Client_CorrectLocks`（Client, Reliable，位域约定见 2.8） |
| 枚举 | `EPredictionType` |
| 结构体 | `FPredictionKey`（USTRUCT + UPROPERTY + **自定义 `NetSerialize`**，只对发起连接有效）、`FPredictionRecord`（含 `MoveBaseline` / `bHasMoveBaseline`）、`FStateLifecycleBinding`、`FStateChangeRecord`、`FPresentationRecord`、`FAuthoritativeShadowEntry`（仅标量）、`FPredictionDelegates` |
| 委托类型 | `FConfirmDelegate`、`FRollbackDelegate` |
| 前提（非接口，同样冻结） | 被影子跟踪的属性必须注册 `REPNOTIFY_Always`（2.7.1）；位置类属性**不入影子**、只记一次性基线（2.5.1-C / 2.9） |

**扩展原则**：

- 新增预测功能时，优先复用现有接口，不新增接口。
- 若必须新增接口，需在本文档追加版本号与变更说明。
- 接口内部实现可优化，但签名与语义保持稳定。

### 3.8 与原有系统的协作边界

预测组件不直接修改角色逻辑，也不直接驱动动画/UI。它只负责「标记、记录、配对、结算」，具体恢复/确认逻辑由原代码注册的委托完成。这样，预测组件与角色逻辑、动画系统、UI 系统之间保持松耦合。

| 原有系统 | 预测组件的协作方式 |
| --- | --- |
| 网络层 | 不替代原有网络层；属性复制、Server RPC、Client RPC、NetMulticast 仍按原方式工作。预测组件在 **3 个状态变更请求**（`Server_Attack` / `Server_Escape` / `Server_ChangeSkillState`）的参数中附加 `FPredictionKey`，并在回执到达时配对；额外新增一条锁更正 RPC `Client_CorrectLocks` 与一条回执 RPC `AC_PlayerController::Client_ResolvePrediction`。**现有 4 条 `Mult_*` 的签名不变**（见 5.10）。 |
| 角色逻辑 | 不替代角色逻辑；原有移动、攻击、技能、受击、替身等核心流程仍由原代码驱动。预测组件只在关键位置插入组件调用。 |
| 移动系统 | 不介入常规移动；移动由 UE `CharacterMovementComponent` 自带客户端预测与服务器校正负责。技能位移与替身瞬移做位置预测，回滚用**一次性基线**（`RecordMoveBaseline`），**不**依赖 `SetReplicateMovement`——拥有者收不到自身移动复制（见 2.9）。 |
| 动画系统 | 不替代动画系统；动画状态机仍读取 `MyAttack`、`MyCState`、`MySkill`、`MySpeed` 等变量。预测组件只让这些变量（经由 PlayerState）在客户端更早进入预测值。**接入前必须先把动画通知切成两类**（时机类 / 纯权威类，见 2.12 与 5.9）。 |
| UI 系统 | 不替代 UI 系统；UI 仍读取 `HealthValue`、`Chakra`、CD 状态等变量。预测组件只让这些变量在客户端更早进入预测值。 |
| 摄像机 | 不纳入预测；摄像机读取（含预测值在内的）最终朝向与位置，被动跟随，无需预测支持。 |
| 锁 | 不进入预测键机制：本地先行置位，服务器写锁处通过 `Client_CorrectLocks` 下发权威值。 |
| 服务器 | 服务器端同样挂载预测组件，但只作为预测键回传与校验结果的参照，不执行本地先行逻辑。 |

---

## 4. 预测系统的扩展方式

后续扩展新预测功能时，只需要重复以下固定模式：

0. **判定通知类别（先决）**：确认本功能涉及的动画通知属于「时机类」还是「纯权威类」（见 2.12 / 5.9）。只有时机类才能在客户端作为本地首写点；纯权威类必须改为 `HasAuthority()` 门控。
1. **创建预测键**：在客户端输入触发本地先行逻辑时，由状态预测调用 `CreatePredictionKey`，并按需 `BindStateLifecycle`。
2. **标记可复制属性**：在本地修改可复制属性处调用 `MarkReplicatedAttribute`（含敌方代理属性）。**位置例外**：位移 / 瞬移改调 `RecordMoveBaseline()`，不写标记。
3. **记录非复制变化**：在本地修改非复制数据处调用 `RecordStateChange` / `RecordPresentation`，并注册跟进/回滚委托（先状态、后表现）。
4. **服务器同步后结算**：在服务器回执到达处调用 `ResolvePrediction`；在属性复制到达处调用 `OnReplicatedAttributeArrived`；在多播到达处调用 `OnMulticastArrived`。
5. **超时兜底**：由 `AC_Character::Tick` 调用 `TickPredictionTimeout`，无需逐功能实现。
6. **降级**：每个接入点都以 `CanPredict()` 为前置判断，为 false 时走原有路径。**不允许出现"只有预测路径、没有原始路径"的接入**——这是"预测系统可整体关闭"的保证（见 2.12）。

**锁预测的扩展方式不同**：只需要在服务器每一处写锁的位置调用 `Client_CorrectLocks` 下发权威值，客户端本地置位/复位保持不变；不建键、不记录、不注册委托。

---

## 5. 接入前置条件（待实现项）

以下事项是预测系统能够按本文档工作的**前提**，当前代码尚未具备，需在接入阶段完成。

### 5.1 服务器校验（P3）

当前 `Server_ChangeSkillState` / `Server_ChangeChakra` / `Server_ChangeAttackState` / `Server_ChangeCharacterState` 均为**裸赋值，无任何校验**；客户端已在输入函数中做了可行性判断（状态是否为 `Normal`/`Protected`、CD 是否为 0 等）。

需把这些校验**原样搬到服务器**（客户端保留同样的校验作为本地先行判定）。补齐后，拒绝路径才真正存在，权威影子与回滚机制才成为必需。

### 5.2 属性补 `ReplicatedUsing` + `REPNOTIFY_Always`（权威影子前提）

**两件事都要做，缺一不可。**

**(a) 补 `ReplicatedUsing`**：以下属性当前为裸 `Replicated`，没有 `OnRep`，无法更新权威影子、无法清除预测标记：

| 类 | 属性 |
| --- | --- |
| `AC_PlayerState` | `HealthValue`、`Chakra`、`Attack`、`MySkill`、`CharacterState` |
| `AC_Character` | `Toward`、`LastEscapeTime` |

同时建议提供非动态多播 `FOnReplicatedAttribute(FName)`，在各 `OnRep` 中广播，供预测组件绑定（含敌方 PlayerState）。

**(b) 注册时用 `REPNOTIFY_Always`**：只加 `ReplicatedUsing` 还不够。接收端 `OnRep` 是否触发，取决于「刚收到的值」与「本地当前值」是否相同（`RepLayout.cpp:3335-3349`），而"预测被采纳"恰恰让两者相同——默认语义会**跳过** `OnRep`，影子因此永远更新不到。注册侧必须写成：

```cpp
DOREPLIFETIME_CONDITION_NOTIFY(AC_PlayerState, Chakra,         COND_None, REPNOTIFY_Always);
DOREPLIFETIME_CONDITION_NOTIFY(AC_PlayerState, CharacterState, COND_None, REPNOTIFY_Always);
DOREPLIFETIME_CONDITION_NOTIFY(AC_PlayerState, MySkill,        COND_None, REPNOTIFY_Always);
// ... 其余被影子跟踪的属性同理（含 AC_Character 的 Toward / LastEscapeTime 与 5.3 的 CD 时间戳）
```

> 例外：**位置类不进影子**（见 2.5.1-C / 2.9），因此不需要为位置补 `ReplicatedUsing`，也不需要 `REPNOTIFY_Always`。
> 提醒：`REPNOTIFY_Always` 会提高 `OnRep` 的调用频率（每次收到复制都调），因此 `OnRep` 里只做"更新影子 + 广播委托 + 尝试清标记"，不要放重逻辑。

### 5.3 CD 时间戳补复制

`LastFirstSkillTime` / `LastSecondSkillTime` / `LastScrollTime` / `LastSummonTime` 当前是**未复制的局部变量**（只有 `LastEscapeTime` 已复制）。它们需要成为可复制属性（`Replicated` 或 `ReplicatedUsing`），否则"CD 时间戳本地先行 + 服务器权威"不成立，且服务器无法据此校验 CD。

### 5.4 替身瞬移的客户端本地先行

`Server_Escape_Implementation` 中的瞬移（`SetActorLocation`，`C_Character.cpp:402`）目前**仅在服务器执行**，客户端没有本地先行路径。需要在客户端 `Escape()` 中加入本地判定与本地瞬移，并**在瞬移前调用 `RecordMoveBaseline()`** 捕获一次性基线（见 2.9）。

> 客户端本地判定所需的数据当前都已在客户端可得：`LastEscapeTime` 已复制（`C_Character.h:339-340`），落点可用本机的 `PlaceMark` 计算。若某个必需量最终仍不可得，则本项**降级为不预测瞬移**，只保留服务器权威路径（见 2.12 降级策略）——不要为了预测而临时新增一条数据通道。

### 5.5 锁更正通道

新增 `AC_Character::Client_CorrectLocks`，并在 2.8 列出的每一处服务器写锁位置调用。锁**不**加 `Replicated`。

### 5.6 `bSuccessHit` 复位点

`bSuccessHit` 目前只在服务器命中判定时被置 `true`，**没有任何复位路径**。需要明确复位时机（建议：连段结束或本次攻击结束时复位），并通过 `Client_CorrectLocks` 下发。

### 5.7 超时检查接入

在 `AC_Character::Tick` 中调用组件 `TickPredictionTimeout`，且必须使用世界时间判定（见 2.3.1 实现约束）。

放置位置有讲究：`AC_Character::Tick` 在 `GetInformation()` 之后有 PS / GameState 的空指针早退（`C_Character.cpp:456-459`），超时检查必须放在该早退**之后**，否则角色数据未就绪期间超时检查会静默停摆。同时 `Tick` 在服务器与客户端都会执行：`TickPredictionTimeout` 内部应首先判断 `CanPredict()`，在服务器与模拟代理上直接返回，不要依赖调用方过滤。

### 5.8 顺带修复（重构时一并处理）

接入预测会把下面这些问题从"平时不发作"放大成"必然发作"，需一并处理：

| 位置 | 问题 | 处理 |
| --- | --- | --- |
| `AC_Character::AddChakra`（`C_Character.cpp:154-159`） | 直接调用 `Server_ChangeChakra_Implementation` 而非走 RPC，且 `Cast<AC_PlayerController>(Controller)` 结果未判空 | 改为调用 RPC，并判空 |
| `AC_Character::MyInitialize` | 同样直接调用 `Server_ChangeToward_Implementation` | 改为走 RPC |
| `AC_Character::ChangeAttack`（`C_Character.cpp:185-205`） | 写 `PS->Attack` / `PS->MySkill` / `PS->CharacterState` / 锁，并在第 199-200 行直接调用 `Server_ChangeToward_Implementation`；`PS` 未判空 | 判空；本地先行部分改为走预测接口 |
| `AC_Character::ChangeState`（`C_Character.cpp:207-210`） | `GetPlayerState<AC_PlayerState>()->CharacterState = target;` 无权限检查、无判空 | 加权限语义与判空；客户端先行改为走预测接口 |
| `AC_Character::MakeMove`（`C_Character.cpp:212-228`） | `AddActorLocalOffset` 无权限检查，客户端直接调用会被引擎拒绝并告警 | 明确"两端各自执行"的语义；客户端路径改由动画通知内先行 + `RecordMoveBaseline` |
| `AC_Character::Tick` 内直接调用 `Mult_ChangeGrabLocation`（`C_Character.cpp:510`） | 服务器从 Tick 里调 NetMulticast，触发频率与调用点都不明确 | 明确触发条件与频率（建议只在抓取状态变化时调一次） |
| 服务器校验（P3） | 四个 `Server_*` 均为裸赋值、零校验 | 见 5.1，接入前必须补齐 |

> 上面这些"先判空、再走 RPC、再分权限"的修复与预测无关，是接入过程中必然要碰到的既有问题；单独提交、单独验证，不要与预测逻辑混在一个改动里。

### 5.9 动画通知拆分与碰撞框本地先行

**两件事，先后有序。**

**(a) 动画通知拆成两类。** 当前项目在动画通知里把"时序信号"与"纯表现"混在一起，多处 `if (HasAuthority())` 与两端执行交错。接入预测前必须切开：

| 类别 | 判定标准 | 处理 |
| --- | --- | --- |
| **时机类** | 通知的**发生时刻**本身对状态机有意义：连段推进、位移（`AN_MakeMove`）、碰撞框尺寸 / 偏移变更、状态切换到下一段的时点 | 两端都执行；在客户端它是"本地先行 + `Record*` 记录"的首写点 |
| **纯权威类** | 通知只产生表现，不改变任何被复制 / 被校验的量：特效、音效、纯表现开关 | 改为 `HasAuthority()` 门控，由服务器触发后经 NetMulticast 分发；客户端不再自行执行 |

> 判定标准只有一条：**"如果这个通知在客户端提前执行了，会不会让某个量进入一个服务器可能不同意的值？"** 会 → 时机类；不会 → 纯权威类。
>
> 注意 `Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 这类多播**兼具表现与状态**：表现部分（动画示意）按权威门控，状态部分（`bInProtectAnim`、`LaunchState`）必须保持两端一致，不能简单当作"纯表现"处理。

**(b) 碰撞框本地先行。** 这是 2.5.2 命中预测的前置条件，单独列为验收项：

- 现状：攻击框的尺寸 / 偏移 / 翻转**唯一**的变化路径是 `Server_ChangeBox`（Server RPC）→ `Mult_ChangeBoxSize`（NetMulticast）往返（`C_Character.cpp:161-171` → `127-152`）；组件上的 `SetIsReplicated(true)`（`C_Character.cpp:33/41`）只覆盖 `USceneComponent` 那几个 `COND_None` 的变换属性，而决定命中几何的 `UBoxComponent::BoxExtent`（裸 `UPROPERTY`，`BoxComponent.h:23-24`）与碰撞启用状态（`BodyInstance` 未注册复制）**都不走复制**，只能等 `Mult_ChangeBoxSize` 到达，所以攻击者本地做命中判定时几何会滞后一个 RTT。
- 目标：把碰撞框的尺寸 / 偏移 / 翻转改为**由已复制状态派生**——两端在 `AC_Character::Tick` 里按 `PS->CharacterState`（经 `GetInformation()` 已同步到 `MyCState`）与 `Toward` 计算，或另在 `AC_Character` 上新增一组 `Replicated` + `REPNOTIFY_Always` 的碰撞框属性（尺寸 / 偏移 / 翻转），在 `OnRep` 里应用到组件。**注意**：引擎的 `UBoxComponent::BoxExtent` 本身不是复制属性（`BoxComponent.h:23-24`），不能直接给它加 `Replicated`，所以要复制就必须在角色上另立一份。**优先选"派生"**：它同时消掉了往返延迟与乱序问题，也让 `Server_ChangeBox` / `Mult_ChangeBoxSize` 这对 RPC 有机会整体删除。
- 验收标准：客户端在任意动画帧打印 `AttackBox` 的 `GetUnscaledBoxExtent()` 与 `GetRelativeLocation()`，与服务器同一帧的值一致（容差 0）。
- 只有本项完成后，才能启用 2.5.2 的"本地命中判定 → 敌方代理属性预测"；在此之前，命中预测整体降级为服务器判定。

### 5.10 键的跨机传递约束

预测键只走两条路，**不新增第三条**：

| 路径 | 载体 | 说明 |
| --- | --- | --- |
| 客户端 → 服务器 | 带键的 Server RPC 参数（`FPredictionKey`） | 只有 3 个：`Server_Attack` / `Server_Escape` / `Server_ChangeSkillState`（见 3.4.2） |
| 服务器 → 发起客户端 | `Client_ResolvePrediction` 的 `uint32 KeyID` | 该 RPC 本身按连接路由，只有发起者收得到 |

**不给现有 4 条 `Mult_*` 增加键参数。** `Mult_ChangeBoxSize` / `Mult_ChangeGrabLocation` / `Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 都是服务器权威**同步**，不是预测回执；按 2.11.3 第 6 条，多播到达只需按**名字**清除对应的预测标记（`OnMulticastArrived(FName)`），不需要知道这是谁的键。理由是收益为零而代价明确：改签名会波及全部调用点，还要处理"服务器手上根本没有键"（如服务器主动触发）的分支。

**明确的禁止项**：

- **禁止用裸 `uint32 KeyID` 作为跨机传递的键**。两端各自自增编号必然撞号，且无法表达"属于哪个连接"。凡是要跨机传键的地方，参数类型必须是 `FPredictionKey`。
- **禁止为同一个本地先行逻辑再引入一条独立的"建键 RPC"**（见 3.4.2 挂载约束）。

**保留的扩展口**：将来若确有多播需要区分"这是不是我的预测引起的"，直接把参数类型写成 `FPredictionKey` 即可——它的自定义 `NetSerialize` 已经保证只有发起连接能读到真实 `KeyID`，其他客户端收到的恒为 0（见 3.3.1）。这个能力**现在就必须实现**（它是请求 / 回执两条路径上的既有需求），但**现在不需要**在任何 `Mult_*` 上使用。
