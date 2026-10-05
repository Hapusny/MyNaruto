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
| 属性 | 纳入**属性预测**：客户端输入后本地立即修改的数值、位置、朝向、时间戳；包含对敌方本地代理的影响。数值 / 朝向 / 时间戳的回滚依据是**权威值表**，**位置用一次性基线**（见 2.5.1-C / 2.9） |
| 锁 | 纳入**锁预测**：本地立即置位的输入控制开关（不建键、不记录、不结算） |
| 否 | 不纳入预测系统，仍由原有网络层负责 |
| 否（派生） | 由其它变量派生，不作为预测对象 |
| （部分） | 该功能点只有部分环节纳入预测 |
| 表现（记录） | **不是独立预测类别**；作为表现记录参与跟进/回滚（见 2.2 / 2.11.2） |
| 属性（多播更正） | 该属性走属性预测，但其权威值由 NetMulticast 下发——多播到达即视为该数据权威值已到，清除对应预测标记（见 2.11.3 第 6 条） |
| 属性（位移，基线回滚） | 位置类属性预测：回滚**不用权威值表**，用位移前捕获的一次性基线（见 2.5.1-C / 2.9 / `RecordMoveBaseline`） |
| 属性（部分，需碰撞框本地先行） | 该环节的本地预测依赖"碰撞框在拥有者客户端上已是最新几何"这一前提；该前提未落地前整体降级为服务器判定（见 5.9） |

### 1.1 功能点总表

> **本表「网络涉及方式」一列描述的是原项目的现状**（含 `DOREPLIFETIME`、属性复制等），用途是把原项目整理清楚、供后续改造；**「预测归属」一列才是预测系统接入时对该功能点的判断**。因此本表与 5.2 的"摘除清单"不矛盾：先照现状登记，改造时再按 5.2 删除被预测属性的 `DOREPLIFETIME`。

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
| 1.1.2-11 | 角色基础与移动 | 被抓取时位置同步 | Mult_ChangeGrabLocation 多播 | 否（**位置不预测**：多播只做同步、不写预测标记，见 2.5.2 / 2.9） |
| 1.1.2-12 | 角色基础与移动 | 角色初始化根据队伍设置朝向/颜色 | OnRep_PlayerState、OnTeamChanged | 否 |
| 1.1.3-01 | 战斗与攻击 | 普攻输入 J 键 | Server_Attack RPC（**带预测键**） | 状态（本地先行触发点、**段键创建点**） |
| 1.1.3-02 | 战斗与攻击 | 普攻连段 5 段 | DOREPLIFETIME(AC_PlayerState, Attack) | 状态（**每段一个预测键**，不是一条连段一个键；段号写入点按 2.4.4 移到普攻输入点） |
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
| 1.1.3-15 | 战斗与攻击 | 攻击输入锁 bAttackInputLock | 服务器与本地表现 | 锁（**普攻输入点的本地置位由服务器补发的 `Client_CorrectLocks` 解除**，见 2.6.1） |
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
| 1.1.4-13 | 技能、替身、秘卷、通灵 | 技能状态同步 MySkill | DOREPLIFETIME(AC_PlayerState, MySkill) | 状态（**取值 1/2/4/5 各自是一个状态、各自一个预测键**，其中 `4` 同时代表秘卷与通灵——两者动画相同，按同一状态处理；技能内部的动画段落不建键，见 2.4.4） |
| 1.1.4-14 | 技能、替身、秘卷、通灵 | 查克拉同步 Chakra | DOREPLIFETIME(AC_PlayerState, Chakra) | 属性 |
| 1.1.4-15 | 技能、替身、秘卷、通灵 | 查克拉增加 AddChakra | Server_ChangeChakra | 属性 |
| 1.1.4-16 | 技能、替身、秘卷、通灵 | 奥义点清零 FinalSkill | Server_ChangeChakra(0) | 属性 |
| 1.1.5-01 | 动画系统 | 动画状态机 Idle/Run/Attack/Skill/受击等 | 依赖 PlayerState 复制变量 | 状态（表现） |
| 1.1.5-02 | 动画系统 | 状态变量驱动 MyAttack/MyCState/MySkill/MySpeed | 状态复制后驱动 | 否（派生镜像） |
| 1.1.5-03 | 动画系统 | AN_ChangeAttack 切换普攻连段 | 服务器/客户端逻辑 | 状态（**时机类**：只在动画推进点切换表现，**不再写 `PS->Attack`**，见 2.4.4） |
| 1.1.5-04 | 动画系统 | AN_ChangeAttackBox 改变攻击框 | Server_ChangeBox | 属性（部分） |
| 1.1.5-05 | 动画系统 | AN_ChangePalyerBox 改变受击框 | Server_ChangeBox | 属性（部分） |
| 1.1.5-06 | 动画系统 | AN_ChangeState 切换角色状态 | 状态复制 | 状态（霸体授予：技能一/二 `Armor`、奥义/通灵 `Unbreakable`；由通知两端各自写入，**不进技能键**，见 5.9a） |
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

| 编号 | 机制 | 说明 | 对预测系统的意义 |
| --- | --- | --- | --- |
| 1.1.8-01 | 属性复制频率 | `AC_PlayerState::NetUpdateFrequency = 100` | 决定权威值到达客户端的延迟上限 |
| 1.1.8-02 | 移动复制 | `SetReplicateMovement(true)` → `AActor::ReplicatedMovement`（`COND_SimulatedOrPhysics`，`ActorReplication.cpp:490`） | **只到模拟代理（对手侧）**。拥有者收不到自身移动复制（`bNetSimulated` 判定，`DataChannel.cpp:3511`），自身位置一致性由移动组件的 `ServerMove` → `ClientAdjustPosition` 校正通道保证。**因此位置预测不能依赖"复制兜底"，只能用一次性基线**（见 2.9） |
| 1.1.8-03 | 组件复制 | `AttackBox / PlayerBox / Flipbook` 等 `SetIsReplicated(true)` | 组件变换本身（`RelativeLocation` / `RelativeRotation` / `RelativeScale3D`）走默认 `COND_None`（`SceneComponent.cpp:3513-3515`），**拥有者也收得到**；但决定命中几何的两个量——`UBoxComponent::BoxExtent`（裸 `UPROPERTY`，`BoxComponent.h:23-24`）与碰撞启用状态（`BodyInstance` 未注册复制）——**根本不走复制**。因此碰撞框变化的唯一路径是 `Server_ChangeBox` → `Mult_ChangeBoxSize` 往返，拥有者本地几何滞后一个 RTT，"拥有者本地命中判定"是接入前置项（见 5.9-b） |
| 1.1.8-04 | 无缝传送 | `ServerTravel` / `SeamlessTravel` | 跨关卡时 Character 重建，预测上下文自然重置 |
| 1.1.8-05 | 返回大厅 | `ClientTravel(TRAVEL_Absolute)` | 同上，客户端重进关卡时预测上下文一并重置 |
| 1.1.8-06 | 权威值表复制 | 每 Actor 一个复制的权威值表组件；服务器在 `PreReplication` 里从真实属性刷新（`Actor.cpp:1619-1631`），客户端只读 | 权威值的统一通道，客户端手上的权威值就是它（见 2.7）。表变 ⟺ 服务器写值，因此**不需要 `REPNOTIFY_Always`** |
| 1.1.8-07 | `APlayerState` 复制频率 | 引擎的 `APlayerState` 构造函数把 `NetUpdateFrequency` 设为 **1**（`PlayerState.cpp:19-33`），`AActor` 默认是 100（`Actor.cpp:173`）；**本项目的 `AC_PlayerState` 已在构造函数里显式改回 100**（`C_PlayerState.cpp:9`） | 表挂在 PlayerState 上时，这一行就是表的下发频率上限。**不要删掉这一行**——删掉就退回引擎默认的 1Hz（见 2.7.5） |

---

## 2. 预测系统设计

### 2.1 系统划分

本项目为**服务器权威**架构，同步由原有网络层（属性复制 / Server RPC / Client RPC / NetMulticast）负责。预测系统为**外置模块**，负责客户端的表现即时响应，以及服务器校验后的接受或回滚。

项目里预测系统真正要处理的，为**状态预测、属性预测、锁预测**三类：

- **状态预测**处理客户端输入后本地立即切换的状态变量，分两层：**动作层**是 `MySkill`（技能一/二/奥义/秘卷/通灵）、`Attack`（普攻连段），由**输入点**本地先行；**状态层**是 `CharacterState`（受击、击飞、倒地、被抓、保护、霸体等），它**不由输入点写入**——受击类走受击路径（含攻方对敌方代理的预测，见 2.5.2），霸体类由动画通知 `AN_ChangeState` 写入（见 5.9a）。动画状态机读取这些状态后立即切换动画。
- **属性预测**处理客户端输入后本地立即修改的数值、位置、朝向、时间戳，包括 `Chakra`（技能/替身预扣）、`Toward`（本地翻转）、位置（技能位移、替身瞬移本地先行）、CD 时间戳（本地记录技能释放时间）。此外，还包括**对敌方本地代理的影响**：攻方客户端预测自己命中后，敌方代理的 `HealthValue` 与 `CharacterState` 变化（见 2.5.2）——**己方血量不由本人预测**，只有"攻方预测敌方"这一个方向。UI 读取这些属性后立即更新血量条、查克拉槽、CD 显示。
- **锁预测**处理客户端输入后本地立即置位的输入控制开关，包括 `bAttackInputLock`、`bPreInputLock`、`bSuccessHit`。

**表现触发（动画、特效、音效、UI、摄像机）不是第四类预测**。它们是读取上述三类数据后的本地表现，在预测系统中只作为「表现记录」参与跟进/回滚，不单独作为预测类别；UI、摄像机本身不纳入预测范围，只是被动读取预测后的值。

### 2.2 系统设计

三类预测的共同点是：都由客户端输入触发，都在本地立即生效，都需要与服务器结果配对，以决定「跟进」还是「回滚」。三者的差异在于**本地立即改变的对象不同**：

| 类别 | 本地立即改变的对象 | 是否建预测键 | 是否记录变化 | 回滚依据 |
| --- | --- | --- | --- | --- |
| 状态预测 | 状态变量 | 是（建键并持有生命周期） | 非复制状态记变化记录；可复制状态只写标记 | 非复制：变化记录；可复制：权威值表 |
| 属性预测 | 数值、位置、朝向、时间戳 | 否（共享当前活跃键） | 数值/朝向/时间戳写标记；位置只记一次性基线 | 数值/朝向/时间戳：权威值表；位置：一次性基线 |
| 锁预测 | 输入控制开关 | 否 | 不记录 | 不参与回滚；全部锁（含普攻输入点的本地置位）由服务器 `Client_CorrectLocks` 直接覆盖，见 2.6.1 |

**回滚数据只有两处存放地**，不要在文档与代码中混用（锁的更正通道是第三处，但它不参与回滚）：

| 存放地 | 装什么 | 何时写入 | 回滚时怎么用 |
| --- | --- | --- | --- |
| **权威值表**（挂在组件上，**跨键共享**） | 服务器权威值的副本，即表里对应的那一格 | **权威值表到达时**（含初始复制；**不再依赖 `REPNOTIFY_Always`**，见 2.7.1） | 拒绝 / 超时时把该格写回本地属性；判据是该字段**预测标记还在且未易主** |
| **预测记录**（键下，**每键一份**） | ① 变化记录 / 表现记录（非复制数据的旧值与表现，逐条配对跟进 / 回滚委托）<br>② 位置的一次性基线 `MoveBaseline` / `bHasMoveBaseline` | ① 本地修改时逐条写<br>② 位移执行前捕获一次 | ① 逐条执行回滚委托（确认时执行跟进委托）<br>② 判据是 `bHasMoveBaseline`——位置**从不写预测标记**（拥有者收不到自身位置复制，等不到权威值，标记没有清除时机） |

这两处与 2.1 的三类预测的对应关系：可复制状态 / 属性 → 权威值表；非复制状态、表现、位置 → 预测记录；锁 → 更正通道，**不进预测记录**（普攻输入点的本地置位也只有"本地置位 + 服务器更正"这一条路径，见 2.6.1）。

为了统一管理「本地先行」与「服务器同步」的配对关系，**状态预测与属性预测共用一套标识机制——预测键（PredictionKey）**。每个状态变化预测行为生成一个预测键，键下挂载该行为的状态变化和其衍生的属性标记、位置变化、表现触发，以及对应的回滚/跟进委托。预测键的有效窗口等于其标记的状态生命周期，状态结束则键冻结。

**锁预测不依托预测键**。原因是锁的语义是「输入控制开关」：其最终控制权明确在服务器，客户端只做本地预判，服务器同步到达后直接覆盖即可；锁的置位/复位与状态变化同步发生，不需要独立的生命周期管理，也不需要旧值记录与委托。普攻输入点的本地置位（2.6.1）同样不建键；它需要的是**配对的下发时机**，而不是回滚。

### 2.3 预测键

预测键（PredictionKey）是预测系统的**唯一标识单元**，用于标记客户端每一次预测行为的完整生命周期。其核心作用是：将客户端本地先行产生的状态、属性、表现与后续服务器的同步结果进行配对，从而支持「跟进」或「回滚」两种处理路径。

- 预测键采用**静态自增的 uint32 计数器**生成，由客户端本地维护，保证同一客户端内唯一。生成时机为「预测状态变化的瞬间」，即客户端输入触发本地先行逻辑、导致状态变化的那一刻。对普攻而言，这一瞬间是**普攻输入点**而不是动画推进点——段号的本地写入点已经移到输入点（见 2.4.4），因此输入即建键。
- 预测键**不跨客户端同步**。客户端在发送 Server RPC 时把预测键附带在参数中，服务器只把它原样回传，用于客户端定位本地预测记录。
- 每个预测键的有效窗口**严格等于其标记的状态生命周期**：状态开始时生成；状态持续期间保持活跃，所有属于该状态的属性标记、变化记录、表现触发都挂载到该键下；状态结束时**冻结**，不再接受新的记录。
- 状态结束的判定依据为**服务器同步或本地状态机切换**。例如技能状态在服务器处理 `Server_ChangeSkillState` 后、`AC_PlayerState::MySkill` 经**权威值表**到达时结束（普攻状态则在本地就结束，见 2.4.4）；攻击状态在连段窗口关闭时结束。
- 预测键本身不直接执行回滚或跟进，而是通过**委托（Delegate）**实现：每个预测键在组件中对应一组委托列表，记录该键下所有受影响的非复制数据的恢复/确认回调。

#### 2.3.1 冻结与结算

- **冻结**（`EndPredictionKey`）：只把预测键标记为 `bFrozen = true`，停止接受新的变化记录；**不执行结算**。
- **结算**（`ResolvePrediction`）：由服务器回执驱动，是唯一的结算入口，决定跟进或回滚。冻结键同样参与结算。
- **超时**（`TickPredictionTimeout`）：覆盖**所有 `!bResolved` 的记录**（含已冻结键），超过 `PredictionTimeout`（默认 2.0s）未结算则强制回滚，避免本地预测状态永久残留。

> **实现约束**：超时判定必须使用**世界时间**与记录中的 `StartTime` 比较，不能累加 `DeltaTime`。玩家被时停时（`CustomTimeDilation = 0`）`DeltaSeconds` 为 0，累加式计时会导致其预测永不超时。

#### 2.3.2 单活跃键与缓冲池

同一时刻同一客户端**只允许一个活跃（未冻结）预测键**；若需开新键，由状态预测先冻结旧键再创建新键。

**冻结由「下一次预测」驱动，而不是由轮询驱动**：连段期间每按一次普攻就是一次独立的段预测，新键创建时先冻结旧键（见 2.4.4）；技能之间同理，技能取值改变即冻结旧键。`BindStateLifecycle` 降为兜底，只覆盖「没有下一次预测」的结束路径：收招（`Attack` 归 0）、被打断（`PlayerStateReset`）、技能自然结束。

> 不要写「同一时刻角色只存在一个用以驱动动画的状态」。`Attack` 与 `MySkill` 是两条独立的服务器通道且**可以并存**（`C_Character.cpp:260` 同时判二者；两条通道同时清零的有两处——`ChangeAttack(0)`（`C_Character.cpp:185-205` 内）与 `PlayerStateReset`（`C_PlayerController.cpp:123-131`，受击/抓取/击飞的打断路径）），这一点在 `AICollaborativeNotes.md` 3.8.3 的代码复核中已被推翻过一次。

但「冻结」不等于「已结算」，因此实际会出现「旧键冻结未结算 + 新键活跃」并存的情况。组件为此维护**未结算预测键缓冲池**：

| 项 | 规则 |
| --- | --- |
| 容量 | 复用 `MaxPredictionRecords`（默认 64，可配置） |
| 入池 | 预测键被冻结时 |
| 出池 | `ResolvePrediction` 结算后，或超时强制回滚后 |
| 淘汰 | 只淘汰**已结算**记录（按时间淘汰最旧）；**未结算记录不得淘汰** |

### 2.4 状态预测

#### 2.4.1 处理对象与职责

状态预测是**预测键的创建者与生命周期持有者**。它处理客户端输入后本地立即切换的状态变量，分两类：

| 类别 | 变量 | 处理方式 |
| --- | --- | --- |
| 可复制状态变量 | `AC_PlayerState::CharacterState`、`MySkill`、`Attack` | 本地先行修改 + 写预测标记；结算由**权威值表**兜底。其中 `Attack` 的本地写入点是**普攻输入点**（见 2.4.4） |
| 非复制状态变量 | `AC_Character::LaunchState`、`bInProtectAnim` | 本地先行修改 + 写变化记录 + 注册委托；结算时跟进或回滚 |

> **`AC_Character` 上的 `MyAttack` / `MyCState` / `MySkill` 不作为预测对象。** 它们是 `GetInformation()` 每帧从 `AC_PlayerState` 派生的本地镜像（`C_Character.cpp:580-588`），预测直接作用于 `AC_PlayerState` 的复制属性，镜像自动跟随。把它们也列为预测对象会造成同帧覆盖与重复回滚。

状态预测的职责：

1. 创建预测键：在客户端输入触发本地先行逻辑时创建预测键，返回预测键 ID。
2. 预测状态变量本身（可复制 → 标记；非复制 → 变化记录 + 委托）。
3. 持有预测键生命周期：状态持续期间保持活跃；状态结束时冻结预测键。
4. 服务器同步后结算状态变量（可复制 → 由权威值表兜底；非复制 → 回执确认跟进 / 拒绝回滚）。

状态预测**不负责**该状态衍生的属性变化、锁变化、表现触发；这些分别由属性预测、锁预测、表现记录在同一预测键下处理。

#### 2.4.2 流程与示例

每个状态预测行为生成一个预测键，键下记录：

- 可复制状态变量的预测标记（属性名 + 预测键 ID）；
- 非复制状态变量的变化记录（旧值、新值、时间戳、跟进/回滚委托）；
- 状态进入时间戳；
- 状态生命周期绑定（到达指定值时自动冻结本键）。

**示例：玩家释放技能一**

1. 本地生成预测键 `PK_001`（**每个技能取值都是一个独立状态，各自一个键**：技能一 / 技能二 / 奥义 / 秘卷与通灵——秘卷与通灵同为 `4`，动画相同、按同一状态处理，见 2.4.4），并绑定生命周期 `BindStateLifecycle(PK_001, "Self.PS.MySkill", 0)`（技能结束时自动冻结）。
2. `AC_PlayerState::MySkill` 本地置为「技能一」；标记 `Self.PS.MySkill` 正在被 `PK_001` 预测。（技能附带的霸体状态——技能一 / 技能二的 `Armor`、奥义 / 通灵的 `Unbreakable`——由动画通知 `AN_ChangeState` 在两端各自写入，**不是**输入点的本地先行，见 5.9a。）
3. 动画状态机读取后立即播放技能动画。
4. 服务器处理 `Server_ChangeSkillState` 后，**权威值表到达** → 更新权威值表、清除预测标记、采用权威值（**不再依赖 `REPNOTIFY_Always`**，见 2.7.1）。
5. `Client_ResolvePrediction` 回执到达 → 结算 `PK_001`：确认则执行跟进委托；拒绝则执行回滚委托，**非复制状态变量按变化记录恢复旧值、可复制属性从权威值表恢复**，重新按权威值驱动动画。

#### 2.4.3 关键设计点

- 可复制状态变量：本地先行修改，**只写标记**，不记录旧值、增量、委托；结算由权威值表兜底。
- 状态粒度：**一次可被服务器独立裁决的输入 = 一个状态 = 一个预测键**。普攻按段（每次输入一段）、技能按取值（`1`/`2`/`4`/`5`）各自成键，其中 `4` 同时代表秘卷与通灵（动画相同，不额外区分）；技能**内部**的动画段落不是状态，不建键（见 2.4.4）。
- 非复制状态变量：本地先行修改，记录变化记录 + 委托，回执确认 → 跟进，拒绝 → 回滚。
- 状态生命周期与预测键绑定：状态开始生成键，状态结束冻结键；冻结不结算。
- 职责边界：状态预测只预测状态变量本身；衍生的属性、锁、表现由其它模块在同一预测键下处理。
- 回滚策略：非复制状态变量恢复旧值并重新触发动画状态机切换；可复制状态变量从权威值表恢复（不是"等属性复制覆盖"，理由见 2.7）。

#### 2.4.4 普攻段预测（输入即键）

**粒度：一次普攻输入 = 一段 = 一个预测键。**

##### 为什么键建在输入点，而不是动画推进点

普攻的段与段之间存在**裁决点**：每一段都必须由一次独立的普攻输入发起，服务器对每一次输入独立裁决——`AC_Character::Server_Attack_Implementation` 每次执行都做一次 `PS->Attack = PS->Attack + 1`（`C_Character.cpp:412-424`）。因此「段」天然就是最小可裁决单位：一次输入、一段、一个键。

动画推进点（`AN_ChangeAttack`）不是输入点，只是表现时序信号：它由动画播到某帧触发，而不是由玩家触发。在推进点建键有两个问题——键的创建晚于输入所请求的那一段；且上一段被服务器拒绝时推进点根本不会发生，该次输入就没有任何记录可以回滚。

##### 段号的本地写入点前移

| | 现行 | 方案 B（本文档采用） |
| --- | --- | --- |
| 段号本地写入点 | `AN_ChangeAttack`（动画推进点，两端执行，`C_Character.cpp:185-205` 内 `PS->Attack = attack; MyAttack = attack;`） | **普攻输入点**（`AC_Character::Attack`，`C_Character.cpp:274-277`） |
| 建键时机 | 无（现行无预测） | 与段号写入同一瞬间 |
| `AN_ChangeAttack` 职责 | 既写段号又切表现 | **只切表现**：按本地 `PS->Attack` 驱动动画切换，不再写 `PS->Attack` / `MyAttack` |

前移的理由：客户端段号的**语义**必须与服务器一致。服务器写的是「已请求的段数」（每次输入 +1，`C_Character.cpp:421`）；客户端只有在输入点 +1 才是同一个语义。若仍在推进点写，客户端段号的语义会变成「已播放的段数」，在「输入已发出、服务器尚未接受」的窗口里两端相差一段，回滚基准随之出错。

`AN_ChangeAttack` 的 `attack == 0`（收招）分支**保留**，但要按端分清：

| 端 | `AN_ChangeAttack(0)` 的行为 |
| --- | --- |
| 服务器 | 写 `PS->Attack = 0`（权威，并经权威值表下发） |
| 拥有者客户端（可预测） | 写 `PS->Attack = 0` 并置**预测标记**（与输入点写段号同构的本地先行，见下方说明） |
| 其它客户端 / 降级路径 | 不写，等权威值表到达 |

##### 输入点的两道闸门

现行 `AC_Character::Attack` 只有一行 `Server_Attack()`，没有任何本地检查（`C_Character.cpp:274-277`）。照搬的话，本地会对每一次按键都建键并发 RPC，被服务器拒绝的输入会在缓冲池里堆积垃圾键。因此输入点必须先过两道**与服务器同源**的闸门：

```cpp
void AC_Character::Attack(const FInputActionValue& Value)
{
    // 闸门 1：本地 bAttackInputLock == false      —— 镜像服务器 Server_Attack 里的同一判据（C_Character.cpp:417）
    // 闸门 2：CharacterState ∈ {Normal, Protected} —— 镜像服务器的状态闸门（C_Character.cpp:416）
    // 通过后（仅本地控制端）：
    //   PS->Attack = PS->Attack + 1;      // 本地先行写段号
    //   bAttackInputLock = true;          // 本地先行置锁（不建快照、不随段键回滚；由服务器补发的 Client_CorrectLocks 解除，见 2.6.1）
    //   CreatePredictionKey(EPredictionType::Attack) → PK
    //   Server_Attack(PK)                 // 键随 RPC 发出
    // 不通过：不建键、不发 RPC（等价于服务器拒绝，且省下一次无谓往返）
}
```

两个闸门都取自服务器的同名判据，所以「本地放行」≈「服务器接受」——这也让被拒绝的一段成为真正的例外，而不是常态。

##### 冻结与兜底

- **主路径**：下一次普攻输入创建新键时先冻结旧键（2.3.2）；技能取值改变同理。
- **兜底**：`BindStateLifecycle(PK, "Self.PS.Attack", 0)`，只覆盖「没有下一次预测」的结束路径——收招、被打断（`PlayerStateReset`）。

##### `Attack` 不设专属规则

段粒度带来的唯一额外要求：客户端在输入点把段号写成 N 并立刻发 RPC，服务器要一个 RTT 之后才把 N 写进权威值、再经权威值表传回。在这个窗口里表里可能还是上一段的值——**不能采用**。这一点由 2.7.2 的统一采用规则覆盖（该字段有预测标记 → 只更新权威值表、不写回），`Attack` 与 `MySkill`、`Chakra` 完全同规则，**不设专属合并规则**。

唯一需要点名的是**本地把段号写成 `0` 的收招点**（拥有者客户端的 `AN_ChangeAttack(0)`，见上表）：它同样是一次本地先行，因此**同样置预测标记**——否则一张「服务器还停在最后一段」的表会把本地刚收的招拽回连段。该标记由「表里这个字段的值已与本地值相等」（服务器也收招归零）清除，见 2.7.2 采用规则的最后一行。

> **刻意不做的事：不给「表里的 `Attack == 0`」任何立即采纳的特权。** 服务器把段号归 0 是**事件**（收招 / 受击打断 / 换人重置），而表里只有值——本地已经先行到下一段时收到一张「收招前」的 `0`，立即采纳会把刚起的一段撤销，服务器随后接受该段又把它写回 N，表现上是「按了没反应、随后突然挥出」，正是预测要消除的东西。判别所需的信息在表里根本不存在，所以不能用值去猜事件。
>
> 被打断（`AC_PlayerController::PlayerStateReset`，`C_PlayerController.cpp:123-131`，一次写 `Attack = 0` / `MySkill = 0` / `bAttackInputLock = false`）看起来像反例，其实不是：那段代码运行在 `HasAuthority()` 分支内（`C_Character.cpp:436` → `:444` → `:115` 的 `PlayerGetDamage`），**本地客户端并不写 0**，只是等表到达才知道被打断；而表里的 `CharacterState`（本地无标记）会同时被采纳为 `Grabbed` / `Launched` / `Staggered`，`Move()` 早在 `C_Character.cpp:261-263` 就被这三个状态拦住了，不依赖 `Attack` 归零。
>
> 代价与「被拒绝的一段」相同：本地有在飞段键时被打断，本地会把自己那一段演完再回滚。这段时间里移动本就被本地的段预测占着（`PS->Attack != 0` 是本地自己写的），不构成额外的输入卡顿。

##### 被拒绝的一段长什么样（已接受的代价）

拒绝发生在「本地认为可以连段、服务器认为不可以」时（两道闸门不可能与服务器判据完全同源，例如服务器侧刚被受击打断）。此时本段键回滚：`Attack` 从权威值表恢复到服务器值（通常是上一段或 0），表现记录重新按权威段号驱动动画状态机。**`bAttackInputLock` 的本地置位不随本段键回滚**——服务器在同一次处理里会补发一条 `Client_CorrectLocks`，本地锁由它解除（见 2.6.1）。观感上是「多挥了一下，随即弹回」。

这是段粒度方案的固有代价——段间存在裁决点，就不存在零感知的预测。降低它靠的是把本地闸门做得与服务器判据一致，而不是靠事后补偿。

##### 技能：按取值成键，内部段落不建键

技能的「多段」不是玩家可裁决的：`Server_ChangeSkillState` 只接收技能编号（`1` / `2` / `4` / `5`；五个输入点的取值见 `C_Character.cpp:298/316/333/351/369`，函数实现体在 `C_PlayerController.cpp:149`），技能动画内部的后续段落由动画状态机自行推进，既没有对应的 C++ 状态变量，也不存在「服务器接受第一段、拒绝第二段」的情形。因此：

- **每个技能取值 = 一个状态 = 一个预测键**：技能一 / 技能二 / 奥义 / 秘卷与通灵分别成键（秘卷与通灵同为 `4`：两者动画相同，**按同一状态处理**、不额外区分，因此不需要把 `SummonIndex` 加进键判据），`MySkill` 取值改变即冻结旧键、创建新键；
- 技能动画内部的段落**不建键、不单独回滚**，一次技能要么被整体接受、要么被整体拒绝；
- 若某个技能后续需要「可裁决的多段」（每段各有独立输入与独立服务器裁决），届时按本节普攻的方式为每段建键。

### 2.5 属性预测

属性预测处理客户端输入后本地立即修改的数值、位置、朝向、CD 时间戳，以及**对敌方本地代理的影响**。它**不创建预测键**，共享状态预测创建的预测键。

#### 2.5.1 可复制属性

可预测的可复制属性分三类，**记录方式与回滚依据各不相同**：

**A. 己方角色可预测属性**（由客户端输入直接触发）

| 来源 | 属性 | 回滚依据 |
| --- | --- | --- |
| `AC_PlayerState` | `Chakra`、`MySkill`、`CharacterState` | 权威值表（表到达时**无标记则采用**，见 2.7.2） |
| `AC_PlayerState` | `Attack` | 权威值表（本地写入点在普攻输入点，收招的本地写 `0` 同样置标记，见 2.4.4） |
| `AC_Character` | `Toward`、`LastEscapeTime`、`LastFirstSkillTime`、`LastSecondSkillTime`、`LastScrollTime`、`LastSummonTime`（四个技能 CD 时间戳见 5.3 待实现项） | 权威值表（表到达时**无标记则采用**，见 2.7.2） |

> `Self.PS.HealthValue` **不在**其中：血量不由本人预测，只由攻击方预测敌方代理（见 2.5.2）。
> `Team` **不**纳入属性预测：它只在服务器 `AssignTeams` 中赋值，客户端不存在本地先行修改队伍的场景。

**B. 敌方本地代理可预测属性**（由攻击方预测，见 2.5.2）

| 属性 | 回滚依据 |
| --- | --- |
| `Enemy.PS.HealthValue`、`Enemy.PS.CharacterState` | 权威值表（敌方 PlayerState 的那张表复制到本地，见 2.7.5；**不需要权限于拥有者的通道**——PS 对所有连接相关） |

**C. 位置类属性**（技能位移、替身瞬移）

位置**用一次性基线回滚，不用权威值表**。原因：本地控制角色的自身位置不作为常规复制属性下发——`ACharacter` 的移动同步属性全部标 `COND_SimulatedOnly`（`Character.cpp:1620-1627`），`AActor::ReplicatedMovement` 用 `COND_SimulatedOrPhysics`（`ActorReplication.cpp:490`），而条件判定取 `bIsSimulated = (RemoteRole == ROLE_SimulatedProxy)`（`DataChannel.cpp:3511` → `RepLayout.cpp:7142/7152/7156`），**拥有该 Actor 的连接 `RemoteRole` 是 `AutonomousProxy`，因此收不到这些属性**。所以"维护服务器权威值"在位置上无从做起，也无需做：位移是每个预测键一次性的离散跳变，每个键在自己开始时重新捕获基线即可，天然不存在"同属性多次预测"的级联问题。完整推导见 2.9。

**记录方式**：

| 类 | 记录内容 |
| --- | --- |
| 数值 / 朝向 / 时间戳 | 只在预测标记表写入「属性名 → 预测键 ID」，并在预测记录的 `ReplicatedAttributes` 中登记本键涉及的属性名。**不记录旧值、增量、回滚委托**——旧值由权威值表统一维护（见 2.7）。 |
| 位置 | 不写预测标记，只在预测记录的 `MoveBaseline`（`FVector`）中记录**位移执行前的位置**，并置 `bHasMoveBaseline = true`。 |

**结算方式**：

| 结果 | 数值 / 朝向 / 时间戳 | 位置 |
| --- | --- | --- |
| 确认 | 权威值表到达 → 权威值已更新、标记已清除，无需额外处理 | 无需处理（服务器采纳后位置一致） |
| 拒绝 / 超时 | **从权威值表恢复本地值**，清除标记 | **从 `MoveBaseline` 恢复位置** |

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
4. 服务器权威判定后：命中成立 → 敌方权威值表到达 → 权威值更新、标记清除；命中不成立 → 回执拒绝 → 从权威值表恢复敌方血量与状态，撤销本地表现。

#### 2.5.3 非复制数据

非复制数据包括：

- 本地状态变量：`LaunchState`、`bInProtectAnim`（`MyAttack` / `MySkill` / `MyCState` 是派生镜像，不计入）；
- 本地表现：动画状态机切换、Flipbook 翻转/透明度、特效、音效、UI 立即更新；
- 本地生成物：本地预测生成的攻击体、特效。

这些数据的权威值不来自属性复制，需要**完整变化记录 + 回滚/跟进委托**：确认 → 执行跟进委托；拒绝 → 执行回滚委托，恢复旧值。

#### 2.5.4 关键设计点

- 可复制属性：数值 / 朝向 / 时间戳只记预测标记，回滚依据是权威值表（见 2.7）；**位置不写标记，只记 `MoveBaseline`**（见 2.5.1-C）。
- 非复制数据：记录完整变化记录 + 委托。
- 敌方代理属性：只有攻击方预测，受击方不预测；且只预测**血量与状态**，不预测敌方位移（见 2.5.2 范围界定）。
- CD 时间戳：属于可复制属性，本地先行记录，服务器权威值经权威值表到达后采用。
- **采用规则以"字段有无预测标记"为准**（见 2.7.2）：有标记 → 只更新权威值表；无标记 → 写回本地；**有标记但值已相等 → 只更新权威值表并清标记**。所有属性同一张规则表，无专属分支（见 2.4.4）。
- 伤害数值：`Enemy.PS.HealthValue` 属于可复制属性，由攻击方本地先行预判。
- 不创建预测键：属性预测共享状态预测创建的预测键。
- 命中预测有前置：必须先完成碰撞框本地先行（见 5.9），否则整体降级为服务器判定。**分期（定案）**：碰撞框本地先行与 5.1 的服务器校验同属第一批，命中预测（攻方对敌方代理血量 / 状态的预测）进第二批——命中预测不能拖着碰撞框一起上。

### 2.6 锁预测

锁预测处理客户端输入后本地立即置位的输入控制开关，包括 `bAttackInputLock`、`bPreInputLock`、`bSuccessHit`。锁属于**非复制数据**，处于状态和属性的上游，用于判断「状态是否可变化」。

**锁预测不创建预测键，也不记录变化，更不参与跟进/回滚结算**。理由是锁的语义为「输入控制开关」：最终控制权在服务器，客户端只做本地预判；锁的置位/复位与状态变化同步发生，不需要生命周期管理，也不需要旧值。唯一需要额外照顾的是普攻输入点的本地置位（2.6.1）——它要的不是回滚，而是一次**与拒绝成对的下发**。

三个锁的具体规则：

| 锁 | 本地先行 | 服务器更正 |
| --- | --- | --- |
| `bAttackInputLock` | 普攻输入瞬间本地置位（本地闸门 1 的实际依据，见 2.4.4）；**不建快照、不随段键回滚**，只由 `Client_CorrectLocks` 解除（2.6.1） | 服务器 `Server_Attack` / `ChangeAttack` / `PlayerStateReset` 写入权威值后，通过 `Client_CorrectLocks` 下发；**`Server_Attack` 的拒绝路径另补发一次**（2.6.1） |
| `bPreInputLock` | 连段窗口的消费标志：服务器在 `Server_Attack_Implementation` 置 `true`（`C_Character.cpp:414`），由动画推进点消费（`C_Character.cpp:198-204`）。**它不是输入点的闸门** | 同上。**当前 C++ 中没有任何把它置回 `false` 的位置**（全项目仅 `:414` 写入、`:198` 读取），`ChangeAttack` 的 `else bAttackInputLock = false` 分支实际依赖动画蓝图复位它——接入前必须钉死复位点（见 5.8） |
| `bSuccessHit` | 命中判定瞬间本地置位，用于提前触发命中派生动画 | 服务器 `OnAttackBoxOverlap` 写入权威判定后下发；攻击结束时的复位点见 5.6 |

锁预测的接入点主要在 `AC_Character` 的攻击、技能、动画通知等函数中：原有代码只需在本地置位/复位锁处保持原样，并在**服务器每一处写锁的位置**调用 `Client_CorrectLocks` 下发权威值（见 2.8）。**普攻被拒绝时服务器并没有写锁**，所以 `Server_Attack_Implementation` 末尾还要补发一次——那一次不是"写锁"，而是让客户端的本地置位与拒绝成对（见 2.6.1）。

#### 2.6.1 普攻输入点的本地置位：不记录，靠服务器补发的更正解除

普攻输入点的 `bAttackInputLock = true` 是三个锁里唯一一个由"客户端输入瞬间直接置位"的，也是本地闸门 1 的实际依据——本地靠它拦住越界的连段输入（2.4.4）。它**不建快照、不随段键回滚、不进预测记录**：客户端对锁没有任何回滚语义，本地锁的复位**只有一个来源**——`Client_CorrectLocks`。

| 项 | 规则 |
| --- | --- |
| 置位 | 普攻输入点本地先行置 `true`（仅本地控制端，且已过两道闸门） |
| 解除 | **只由 `Client_CorrectLocks` 解除**，与预测键结算无关 |
| 记录 | 不写快照、不写预测标记、不注册委托 |
| 与段键的关系 | 段键被拒绝时，`Attack` 从权威值表恢复、表现按权威段号重驱；**回滚不改写任何锁值** |
| 其它锁 | 同规则：不记录、不回滚 |

**为什么服务器要在拒绝路径上补发一次更正。** `Client_CorrectLocks` 原本只在服务器**写锁**的位置下发（2.8），而"这次普攻被拒绝"恰恰意味着服务器**没有写锁**——两道闸门都没通过时，整条 `Server_Attack_Implementation` 不碰锁。于是本地那次置位的解除就只剩"等服务器下一次与之无关的写锁"这一条路，而这条路并不必然存在。后果不是多挥一下，而是**本地闸门 1 自锁**：

1. 本地锁卡在 `true` → 闸门 1 拦住本机后续每一次普攻输入；
2. 被拦住的恰恰是**唯一能让服务器再写一次锁的输入**——服务器写锁只在 `Server_Attack` / `ChangeAttack` / `PlayerStateReset` 三处，而 `Server_Attack` 要收到 RPC 才会执行；
3. 结果是本地普攻一直没反应，直到玩家再次被击中（`PlayerStateReset` 写锁）。

因此在 `Server_Attack_Implementation` 末尾补一次下发：**无论这次请求被接受还是被拒绝，函数返回前都按当时的最新锁值调用一次 `Client_CorrectLocks`**（见 2.8 的服务器调用点表）。接受路径的写锁本来就要下发，由这一次一并覆盖；拒绝路径由此与更正成对——"本地置了真、服务器说不行 → 假立刻被送回来"，客户端因此不需要任何快照、写者标记或回滚。

这样做的另一个好处是 2.6.2 的「独立性」得以成立：**锁的更正通道不参与预测键结算**。也不需要 `LastAttackLockWriterKey` 之类的写者标记——客户端不再有任何回滚会去改写 `bAttackInputLock`，也就没有"迟到的回滚覆盖刚收到的权威值"这回事。

> 另一条拒绝路径（服务器侧 `bAttackInputLock` 已为真，即服务器还在连段）本就不需要额外处理：此时服务器锁为真，本地置的真与它一致、不是陈旧值；连段结束时 `ChangeAttack`（`C_Character.cpp:195` / `:204`）写 `false` 并下发，本地随之解除。
>
> 而"服务器改了 `CharacterState` 却没写锁"这种情况，今天的代码里暂时撞不上：受击打断的三条路径都先调 `PlayerStateReset()`（`C_PlayerController.cpp:88 / 97 / 102`），而写锁就在它里面（`:128`）。但 5.8（`ChangeState` 加权限语义、客户端先行改走预测接口）与 5.9(a)（动画通知切成权威门控）正是朝这个方向改的——**不要把这条配对建立在巧合上**。

#### 2.6.2 关键设计点

- **本地预判**：锁在客户端输入瞬间即置位/复位，用于本地输入控制与表现触发。
- **服务器权威覆盖**：服务器写锁的位置统一通过 `Client_CorrectLocks` 向拥有者客户端下发权威值，客户端直接覆盖；**普攻的拒绝路径也补发一次**（2.6.1）。
- **不建键、不记录、不结算**：锁不占用预测键、变化记录、委托与缓冲池中的任何资源；普攻输入点的本地置位同样不记录（2.6.1）。
- **独立性**：锁的更正通道独立于状态与属性的回滚路径，避免与预测键结算耦合。
- **不进权威值表**（已定）：锁**不并入** 2.7 的权威值表、不随表同步。锁都是 `bool`，语义是「服务器在哪儿改就在哪儿就地置/清」，与客户端侧的本地置位/复位成对出现，独立的 `Client_CorrectLocks` 通道已经够用；并入表会让同一张表同时承载「权威值的副本」与「本地先行」，还得在表里再区分哪些字段允许回写，得不偿失。

### 2.7 权威值表（Authority Value Table）

#### 2.7.1 为什么需要它

UE 的属性复制**只在服务器侧属性值发生变化时下发**：每个连接对每个属性维护"上次发出的值"，值没变就不发包。这带来两个后果：

1. **服务器接受预测**：服务器修改了属性 → 值变化 → 复制必然到达 → 客户端采用权威值。此路径无需额外机制。
2. **服务器拒绝预测**：服务器不修改属性 → 值没变 → **不会有任何属性复制到达** → 如果客户端把回滚"交给属性复制"，本地预测值将**永久残留**。

> 当前服务器侧的 `Server_ChangeSkillState` / `Server_ChangeChakra` 等是裸赋值、无校验，因而"永远会改值、永远会复制"，掩盖了这个洞。一旦按本文档补齐服务器校验（见 5.1），拒绝路径就会出现，必须由客户端本地回滚兜底。

因此，**可复制属性的回滚依据不是属性复制，而是权威值表**。

**还有一个更隐蔽的洞**：默认的 `ReplicatedUsing` 语义下，接收端是否触发 `OnRep`，取决于「刚收到的值」与「客户端当前本地值」是否相同，而不是与「上一次收到的值」是否相同：

```cpp
// RepLayout.cpp:3335-3349（接收端）
// 引擎：先把【当前本地值】存进它自己的影子缓冲（ShadowData），再反序列化收到的值
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

1. 服务器接受预测 → 复制到达 → 收到的值与本地值相同 → **`OnRep` 不触发** → 客户端手上的权威值保持旧值；
2. 之后某次预测被拒绝、需要回滚时，这个**过期的旧值**会被写回本地，凭空制造一次错值。

这不是偶发边界，而是"接受路径"上的必然。

**同一个代码块还说明了第三个洞，而且是最关键的一个：逐属性复制没有客户端拦截点。** 那两行是"先把收到的值写进对象、之后才决定要不要 `OnRep`"——`NetSerializeItem` 返回时，值已经躺在 `PS->Chakra` 里了，`OnRep` 只是事后通知。所以**任何"收到权威值但先不采用"的采用规则，都无法建立在逐属性复制之上**；权威值表用整结构体下发，正是为了让这条规则有一个可拦截的落点（见 2.7.2）。

因此本设计不去碰 `REPNOTIFY_Always`——它只能"让 `OnRep` 多触发几次"，既解决不了第三个洞，还要**为每个被预测属性各写一个回调**（这正是被否掉的方案）。改用一条统一通道：**复制的权威值表**（形态 1，见 2.7.2）。

| 结论 | 内容 |
| --- | --- |
| 一 | **被预测的复制属性从逐属性复制中摘除**，改由**权威值表**下发：保留 `UPROPERTY` 声明与本地读写，删除对应的 `DOREPLIFETIME`。表的 `OnRep` 只有一个 `OnRep_AuthorityValueTable`，**不再需要逐属性回调，也不再需要 `REPNOTIFY_Always`**。 |
| 二 | 表的**变更判定在服务器侧**（表变 ⟺ 服务器写值），与客户端本地值无关，于是 2.7.1 第 2 条"接受路径 `OnRep` 不触发"的洞被结构性消除。 |
| 三 | 表是**整块结构体**，客户端在 `OnRep_AuthorityValueTable` 里先做采用判定、再决定"要不要把值写回本地属性"，**拦截点天然存在**，第三个洞随之消除。 |
| 四 | **位置不进表**，走一次性基线（见 2.5.1-C / 2.9）。 |

> **不是"逐属性复制 + 表"的双通道**：被预测的属性只有表这一条通道。双通道下引擎会在表到达之前就把值直接写进本地属性（第三个洞），于是采用规则里两条以"本地值"为判据的行（行 1「不动本地」、行 3「表里的值已与本地值相等」）失去判据——本地值到底是预测值还是权威值，没有定义。
>
> **要说清破坏的是什么**：是**「收到权威值但先不采用」（HOLD）这条规则**，不是结算与回滚。结算由回执/超时驱动（`ResolvePrediction`，2.11 / 3.4.5），回滚用的是变化记录、委托、`MoveBaseline` 与表里的值，**都不经过逐属性复制通道**。所以摘除的正当理由是"**让本地值只有一个写者**"，而不是"回滚需要它"：不摘，机制照样跑，坏掉的是 HOLD——例如本地已预测到第 4 段时，属性通道会把服务器的第 3 段值写回本地、拽着表现往回走（2.4.4 的段号窗口）。

**引擎依据（可逐条核对）**：发送侧按对象保存"上次发出的值"（`FRepChangelistState::StaticBuffer`，`RepLayout.h:408-409`），比较只比值（`CompareProperties_r`，`RepLayout.cpp:1651`）；接收侧先 `StoreProperty`、再 `NetSerializeItem`、最后判 `RepNotifyCondition`（`RepLayout.cpp:3330-3351`）；`RepNotifyCondition` 定义在 `FRepParentCmd` 上（`RepLayout.h:764`），由 `FRepLayout` 初始化时按属性设置（`RepLayout.cpp:6243`），读取点见 `RepLayout.cpp:1069` / `:3343` / `:3467` / `:4429` / `:5096`；**发送侧的变更判定不读它**——发送只比值（`CompareProperties_r`，`RepLayout.cpp:1651`）。

#### 2.7.2 定义与使用

**权威值表**（`FAuthorityValueTable`，见 3.3.1）是一个复制结构体，挂在**权威值表组件** `UC_AuthorityValueComponent` 上；组件按"谁承载这些属性"分别挂在 `AC_PlayerState` 与 `AC_Character` 上（见 2.7.5）。表的字段就是被预测属性的**服务器最新权威值**。

**表本身就是权威值**：客户端不另存一份 `TMap<FName, ...>` 的副本，**权威值表组件**里保存的"最后一次收到的表"就是客户端手上的权威值。理由——表由服务器在 `PreReplication` 里从真实属性刷新，客户端从不写它，因此**表变 ⟺ 服务器写值**，二者是同一事实的两种表述，没有第二份状态可存。

| 时机 | 动作 |
| --- | --- |
| 服务器 `PreReplication`（每个要发复制的帧） | 从真实属性刷新表的每个字段（`Actor.cpp:1619-1631`） |
| 客户端收到表（`OnRep_AuthorityValueTable`） | 记录"这是最新权威值"，再按采用规则逐字段决定**写回本地属性**还是**只更新权威值表**（该不该清标记也由规则定） |
| 客户端 `BeginPlay`（初始化，不是权威值到达） | 把"最后一次收到的表"初始化为宿主当前属性值。理由见 2.7.5 的初始复制说明 |
| 预测开始（本地写值前） | 写入预测标记（属性名 → 预测键 ID）；权威值表已就位，无需另行记录旧值 |
| 服务器拒绝 / 预测超时 | **从权威值表恢复到本地**（把表里对应字段的值写回本地属性），并清除预测标记 |

**适用范围**：表只覆盖「服务器权威、会被反复写入、且客户端收得到」的属性，即 2.5.1-A / 2.5.1-B 中的**数值、朝向、时间戳**。**位移（位置）不进表**，改用一次性基线（见 2.5.1-C 与 2.9）——拥有者客户端根本收不到自身位置的复制。

**采用规则（表到达时，逐字段判断）**：

| 字段状态 | 动作 |
| --- | --- |
| 该字段**有预测标记**（本机正在预测它、键尚未结算） | **只更新权威值表**：不动本地值、不清标记。表的这一格可能是"服务器还没处理这次预测请求"的旧值（RPC 尚在途），采用它会把本地预测拽回去 |
| 该字段**无预测标记** | **写回本地属性**（采用权威值） |
| 该字段**有标记，但表里的值已与本地值相等** | **只更新权威值表 + 清标记**：服务器已追平。这一行覆盖两类情况——预测被服务器采纳的那次到达，以及**没有预测键的本地先行**（最典型的是收招在本地写 `0`，见 2.4.4） |

`Attack` 与其它属性**同一张规则表**，没有专属分支（见 2.4.4）。

**结算时的处理**：

| 回执结果 | 动作 |
| --- | --- |
| Confirmed | 清除本键登记的标记，**不写回**。服务器的"接受"意味着它写下了与预测相同的值（5.1 的校验是"把客户端的判据原样搬到服务器"），因此本地值无需改动；之后到达的表会因为无标记而把它再写回一次（幂等） |
| Rejected / 超时 | **从权威值表写回本地**（恢复），清除标记 |

> **两处已知的、有界的竞态**（都在毫秒级、且都会自愈，故接受）：
> 1. **估算值类字段的收敛点**：CD 时间戳的本地预测值是"服务器世界时间估算值"，与服务器精确值不等。若表先于回执到达，该字段在清标记后仍停在估算值上，直到该表下一次发生变化时才被写回精确值。影响仅限 CD 倒计时的起点精度（CD 判定在服务器，不受影响）。要彻底消除，就在回执里带上这几个字段的权威值——当前不做，理由见 2.11.1。
> 2. **跨 ActorChannel 的到达顺序**：表与回执分属 PlayerState / PlayerController 两个 ActorChannel，UE 只保证同一 Actor 上保序。一张"接受之前发出、接受之后才到"的表可能把已清标记的字段短暂写回旧值，随后被下一张表纠正。表自身在同一通道内保序，因此这一竞态只在"回执与表交错"时出现。

**两条防护规则**（回滚时逐项判断）：

1. 该属性**已无预测标记**（说明权威值已到达并在 `OnRep` 中清除）→ **跳过**，不要用旧值覆盖权威值；
2. 该属性的标记**已易主**（`Mark[Name] != 本键 ID`，说明已被新键接管）→ **跳过**，由新键负责。

**接管规则**：新键标记一个已有标记的属性时，旧键记录的 `ReplicatedAttributes` 中应移除该属性名。

#### 2.7.3 命名约定

预测对象的属性名统一带**目标与来源前缀**，避免 `AC_PlayerState::MySkill` 与 `AC_Character::MySkill` 同名冲突：

| 前缀 | 含义 | 示例 |
| --- | --- | --- |
| `Self.PS.*` | 己方 PlayerState 上的可复制属性（走权威值表） | `Self.PS.MySkill`、`Self.PS.CharacterState`、`Self.PS.Chakra`、`Self.PS.Attack` |
| `Self.Char.*` | 己方 Character 上的可复制属性/本地变量 | `Self.Char.Toward`、`Self.Char.LastEscapeTime`、`Self.Char.LaunchState` |
| `Enemy.PS.*` | 敌方本地代理上的可复制属性（走权威值表） | `Enemy.PS.HealthValue`、`Enemy.PS.CharacterState` |
| `Self.Move.*` | 位置类预测（走一次性基线，不入表） | `Self.Move.Displacement` |

> 血量只在 `Enemy.PS.HealthValue` 上预测，**没有** `Self.PS.HealthValue`。

#### 2.7.4 与 COD 式预测的区别

COD / Quake 式客户端预测需要维护**历史状态队列 + 重放**，因为其预测对象是连续物理量（位置、速度需要逐帧重演）。本项目不必如此：

- 预测对象全是**离散写值**（状态机切换、属性赋值、锁置位），不存在需要重演的中间过程，因此**只需要一份"当前权威值"，不需要历史队列与重放**；
- 唯一的连续量是常规移动，而它已由 UE `CharacterMovementComponent` 自带的预测与校正覆盖（见 2.9）。

#### 2.7.5 实现说明

**组件形态与挂载**：

```cpp
// 文件路径：Source/Naruto/C_AuthorityValueComponent.h / .cpp
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class NARUTO_API UC_AuthorityValueComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UC_AuthorityValueComponent();

    // 权威值表：服务器在 PreReplication 里从真实属性刷新，客户端从不写它
    UPROPERTY(ReplicatedUsing = OnRep_AuthorityValueTable)
    FAuthorityValueTable AuthorityValueTable;

    UFUNCTION()
    void OnRep_AuthorityValueTable();

    virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
```

| 挂载点 | 承载的属性 | 理由 |
| --- | --- | --- |
| `AC_PlayerState`（每个玩家一个，含敌方） | `Chakra`、`Attack`、`MySkill`、`CharacterState`、`HealthValue` | 这些属性本来就定义在 PS 上；PS 对每个连接都相关（`bAlwaysRelevant`），因此**敌方的这些值也由敌方 PS 的表带来** |
| `AC_Character`（己方角色） | `Toward`、`LastEscapeTime`、四个技能 CD 时间戳（见 5.3） | 这些属性定义在 Character 上 |

- 组件在各自宿主类的构造函数里用 `CreateDefaultSubobject` 创建，并调用 `SetIsReplicatedByDefault(true)`（`ActorComponent.cpp:2447-2460`：置 `bReplicates`；登记进 `ReplicatedComponents` 由 Actor 初始化阶段完成）。
- **挂在 `APlayerState` 上的组件能正常复制**：`APlayerState` 继承 `AActor` 且未覆写 `ReplicateSubobjects`，走的是 `AActor::ReplicateSubobjects`（`ActorReplication.cpp:502-537`），它遍历 `ReplicatedComponents` 并逐个 `Channel->ReplicateSubobject(...)`。`AInfo` 构造函数虽然把 `bReplicates` 置为 false（`Info.cpp:45`），但 `APlayerState` 构造函数已把它改回 true（`PlayerState.cpp:19-33`）。
- **不需要 `AddReplicatedSubObject`**：UE 的子对象注册表路径默认关闭（`GDefaultUseSubObjectReplicationList = false`，`ActorComponent.cpp:88`），走的是老的 `ReplicatedComponents` 路径，`SetIsReplicatedByDefault(true)` 足够。
- **不把表直接做成 `AC_PlayerState` 上的 `UPROPERTY`**：同一张表在 Character 上也要用，做成组件才能一处实现两处挂载；也避免把预测专用的字段混进 PS 的权威状态定义里。

**表的刷新（服务器）**：

```cpp
void UC_AuthorityValueComponent::PreReplication(IRepChangedPropertyTracker&)
{
    // 组件 PreReplication 只在服务器被调用（Actor.cpp:1598/1619-1631）
    // 从宿主的真实属性逐字段刷新 AuthorityValueTable（字段清单见 3.3.1）
    // 例：AuthorityValueTable.Attack = CastChecked<AC_PlayerState>(GetOwner())->Attack;
}
```

- **刷新点选 `PreReplication` 而不是 `Tick`**：它是引擎在"这个 Actor 本帧确实要发复制"时才调用的钩子，因此天然零纪律——不用自己判断该不该发，也不会因为写了值而把 Actor 唤醒复制。调用链：`UNetDriver::ServerReplicateActors_BuildConsiderList`（`NetDriver.cpp:4469`）→ `AActor::CallPreReplication`（`Actor.cpp:1579-1632`）→ 对每个组件 `Component->PreReplication(...)`（`Actor.cpp:1619-1631`）。
- 组件拿到的是**组件自己的**属性变更追踪器（`Actor.cpp:1627`），刷新表不会污染宿主的复制脏标记。
- `AActor` 构造函数里 `bCallPreReplication = true`（`Actor.cpp:168`），组件路径无需额外开关。

**频率**：表随宿主 Actor 的复制频率下发，因此 `AC_PlayerState` 的 `NetUpdateFrequency` 就是表的下发频率上限。引擎的 `APlayerState` 构造函数把它设成 **1**（`PlayerState.cpp:19-33`），本项目的 `AC_PlayerState` 已在构造函数里显式改回 **100**（`C_PlayerState.cpp:9`）——**这一行必须保留**。`AC_Character` 继承 `AActor` 的默认 100（`Actor.cpp:173`），无需处理。

**初始复制的"相等即不触发"**：Actor 通道打开时引擎会整份下发一次，但接收端的 `OnRep` 判定仍与本地值比较（`RepLayout.cpp:3335-3349`）。刚订阅时客户端 PS / Character 上的属性值恰是类默认值，与服务器的初始值往往相同（`Attack = 0`、`MySkill = 0`、`CharacterState = Normal`），于是 `OnRep_AuthorityValueTable` 会被跳过。这不影响正确性——"被跳过"等价于"服务器的权威值此刻等于本地当前值"，因此**客户端在 `BeginPlay` 时把"最后一次收到的表"初始化为宿主当前属性值**即可；若服务器改过值，初始复制必然与本地不同，`OnRep` 会照常触发并覆盖这次初始化。

**发送与采用**：

- 表是**一个属性**，因此"任一字段变化 → 整表下发"，没有逐字段增量。被预测的属性只有十来个标量，LAN 环境下这个量级可以接受；真正的代价是**整表不能按字段分组做条件复制**。本项目被预测的属性本来就都是 `COND_None`（`C_PlayerState.cpp:22-31`），因此当前不构成限制；若将来出现"只给拥有者"的属性，应另开一张表，而不是给这张表加条件。
- 客户端**从不写表**，所以"收到的表与本地表不同"恒成立，`OnRep_AuthorityValueTable` 每次都会触发，不需要 `REPNOTIFY_Always`。
- 表是 `USTRUCT`，接收端做 member-wise 比较（`FStructProperty::Identical` → `UScriptStruct::CompareScriptStruct`，`PropertyStruct.cpp:146-148` → `Class.cpp:3049-3080`），不需要额外的 `WithIdentical`，也不受结构体填充字节影响。
- **Push Model 注意**：若将来开启 Push Model（当前默认关闭，`PushModel.cpp:415`），`PreReplication` 里刷新后必须补 `MARK_PROPERTY_DIRTY_FROM_NAME`，否则表刷新了也不会发。本文档不开启 Push Model。
- **可靠性不在本设计范围内**：表的发送判定与逐属性复制是同一套机制（"服务器侧值变了才发"），不引入新的可靠性特性——某次表更新若在传输层丢失，后果与逐属性复制的同值更新丢失完全一致，属既有网络层特性（超时兜底见 2.10.1 / 3.4.5）。

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
| `AC_Character::Server_Attack_Implementation`（被接受时） | `bPreInputLock = true`、`bAttackInputLock = true` |
| `AC_Character::Server_Attack_Implementation`（**函数末尾，含拒绝路径**） | **不下发新值：按当时的最新锁值补发一次**。被拒绝时服务器没有写锁，靠这一次让客户端的本地置位与拒绝成对解除（见 2.6.1）；被接受时的写锁下发也由这一次覆盖 |
| `AC_Character::ChangeAttack` | 连段结束 `bAttackInputLock = false`；预输入被消费分支 |
| `AC_PlayerController::PlayerStateReset` | 受击/抓取/击飞打断时 `bAttackInputLock = false` |
| `AC_Character::OnAttackBoxOverlap` | `bSuccessHit = true` |
| 新增复位点 | `bSuccessHit` 的复位（见 5.6） |

**约定**：

- 使用 `Reliable`，与项目现有 RPC 风格一致；同一 Actor 的可靠 RPC 保序。
- 客户端收到后**直接覆盖**本地锁值，不检查预测键（锁不参与结算，也没有任何回滚会改写锁值——见 2.6.1）。
- 迟到覆盖：若服务器已决定 `false` 并发出，而客户端本地又置了 `true`，更正到达后会覆盖较新的本地预测（表现为一帧闪烁，下一次服务器写锁会再次纠正）。**要消除它，就得让这条 RPC 携带 `FPredictionKey`**（裸 `KeyID` 不可跨机传，见 5.10），但那会把锁重新拉进预测键机制，与 2.6「锁不建键、不结算、独立通道」的定案冲突——**因此不做**，这一帧闪烁按已知竞态接受（与 2.7.2 记录的两条同级）。
- 若服务器是**主动改锁**（如 `PlayerStateReset` 把 `bAttackInputLock` 由 true 改为 false），本次写锁本身也会触发下发，不存在"值没变不下发"的问题——该问题只出现在拒绝路径，而拒绝路径正是本 RPC 覆盖的场景。

### 2.9 移动、位移与位置

| 对象 | 归属 | 机制 |
| --- | --- | --- |
| 常规移动（WASD） | **不进入预测系统** | UE `CharacterMovementComponent` 自带客户端预测与服务器校正；输入意图经 `Server_SetTryTargetToward` 同步 |
| 移动范围限制 | 不进入预测系统 | 由原有 Tick 逻辑在两端各自执行 |
| 技能位移（`AN_MakeMove`） | 属性预测（位置，**一次性基线回滚**） | 动画通知在两端执行，本地先行位移（`AddActorLocalOffset`）；服务器位置经移动校正通道兜底 |
| 替身瞬移 | 属性预测（位置，**一次性基线回滚**） | 客户端本地先行（见 5.4 待实现项）；服务器 `SetActorLocation` 权威 |

**为什么位置不能用权威值表**（这也是"位置只能走基线"的根据）：

`ACharacter` 的移动同步属性全部标 `COND_SimulatedOnly`（`Character.cpp:1620-1627`：`RepRootMotion`、`ReplicatedBasedMovement`、`ReplicatedMovementMode`、`bIsCrouched` 等）；而 `AActor::ReplicatedMovement` 用的是 `COND_SimulatedOrPhysics`（`ActorReplication.cpp:490`）。两者的条件判定都取 `bIsSimulated`，它来自 `RepFlags.bNetSimulated = (Actor->GetRemoteRole() == ROLE_SimulatedProxy)`（`DataChannel.cpp:3511`，在其上一行 `FScopedRoleDowngrade` 完成"非拥有连接降级"之后）。条件映射见 `RepLayout.cpp:7142/7152/7156`。

结论：**拥有该角色的连接，其 `RemoteRole` 是 `AutonomousProxy` 而非 `SimulatedProxy`，因此收不到自身角色的这些移动属性**（`bRepPhysics` 为假时 `COND_SimulatedOrPhysics` 同样为假）。所以客户端无法从属性复制中得知"服务器认为我在哪"，权威值表在位置上根本无从维护；自身位置的一致性由移动组件自带的预测校正通道保证（`ServerMove` → 服务器 `ServerCheckClientError` 判定超差 → `ClientAdjustPosition`，`CharacterMovementComponent.cpp:9808/9914/10551`）。反过来，**敌方角色在本机是模拟代理**，`bNetSimulated` 为真，其位置复制正常到达——所以观感上"敌人在动"，而"自己瞬移后服务器没动"却不会自动弹回来。

因此位置的记录与回滚改用**一次性基线**：

| 时机 | 动作 |
| --- | --- |
| 预测键创建后、执行位移前 | 捕获当前位置到 `FPredictionRecord::MoveBaseline`，置 `bHasMoveBaseline = true` |
| 服务器接受 | 无需处理：服务器执行同样的位移，位置自然一致 |
| 服务器拒绝 / 超时 | `SetActorLocation(MoveBaseline, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics)` 恢复 |

- 基线**每键独立**：位移是离散的一次性跳变，一个键只发生一次位移（多段位移拆成多个键），因此不存在"同一属性被反复预测"的级联问题，也不需要权威值表那种"服务器最新值"的语义。
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

预测场景下：客户端本地预测释放技能时，本地记录 `LastXXXTime = 服务器世界时间估算值`，UI 立即开始倒计时；服务器确认后由**权威值表**带来的权威值覆盖；若服务器拒绝，由权威值表恢复本地值，UI 随之恢复。

CD 时间戳属于可复制属性，必须是**服务器世界时间**，不能是客户端本地时间；其回滚由权威值表兜底，预测系统只需记录预测标记。

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

**属性值不塞进 RPC，交给权威值表同步**（见 2.7）。原因：表的职责就是承载权威值，RPC 再传会重复；避免"RPC 值"和"表值"时序不一致；RPC 保持精简。

> **唯一例外**：锁的 3 个布尔量由 `Client_CorrectLocks` 携带（锁不复制、不记录，且拒绝路径下没有属性复制可依赖）。

**为什么不需要额外的"键所有者"字段**：本 RPC 由服务器**定向发给某一个连接**，天然只有该连接的客户端会收到；再加上 `FPredictionKey` 的自定义 `NetSerialize` 只对发起连接写真实值（见 3.3.1），键不可能在错误的客户端上被当成有效键。因此参数里只需要裸 `KeyID`（回收端先用 `IsValidKey` 语义找键，找不到即忽略），不需要携带连接标识。

#### 2.11.2 回滚边界

若服务器拒绝状态变化，客户端回滚的边界由**预测键下的记录**决定——即：预测键下登记了哪些内容，就回滚哪些。回滚数据只来自两处：**权威值表**（跨键共享）与**预测记录**（键下，见 2.2）。

| 回滚项 | 依据（存放地） |
| --- | --- |
| 非复制状态变量 | `LaunchState`、`bInProtectAnim` 恢复旧值（**预测记录**①：变化记录 + 回滚委托） |
| 本地表现 | 重新按权威状态驱动动画状态机、UI（**预测记录**①：表现记录委托） |
| 本地生成物 | 销毁本地预测生成的攻击体、特效，或标记为无效（**预测记录**①：回滚委托） |
| 可复制属性（数值 / 朝向 / 时间戳，含敌方代理） | **权威值表**：把对应字段写回本地属性（`Self.PS.*`、`Self.Char.*`、`Enemy.PS.*`） |
| 位置（位移 / 瞬移） | **预测记录**②：`MoveBaseline`（`Self.Move.*`），判据是 `bHasMoveBaseline`，从不写标记 |
| 锁 | 不在回滚边界内；全部由 `Client_CorrectLocks` 直接覆盖（含普攻输入点的本地置位，见 2.6.1）。**回滚不写任何锁值** |

> 回滚逐项判断，遵循 2.7.2 的两条防护规则（已无标记 → 跳过；标记已易主 → 跳过）。位置项额外判断 `bHasMoveBaseline`，未捕获过基线的键不恢复位置。

#### 2.11.3 结算流程

1. **客户端预测**：状态预测创建预测键；属性预测写预测标记、记录非复制数据变化；锁预测本地置位；表现记录注册回调。
2. **服务器校验**：只校验状态变化是否可行（含 CD、当前状态是否允许等）。
3. **服务器回执**：`Client_ResolvePrediction(KeyID, Result, ConfirmedStatePacked)`。
4. **客户端结算**（`ResolvePrediction`，唯一入口）：
   - **确认** → 执行跟进委托；清除非复制数据预测标记；**清除本键登记的可复制属性标记、不写回本地值**——服务器"接受"意味着它写下了与预测相同的值（见 2.7.2）。
   - **拒绝** → 执行回滚委托，恢复非复制数据旧值；**从权威值表恢复本键登记的数值/朝向/时间戳属性**；**从 `MoveBaseline` 恢复位置**；清除对应标记。
5. **权威值表到达**：`OnRep_AuthorityValueTable` → 更新权威值表、按采用规则写回本地属性、清除对应预测标记。该路径**不依赖 `REPNOTIFY_Always`**：表的变更判定在服务器侧（见 2.7.1），"预测被采纳"时表照样会变、照样会发。
6. **多播 / 客户端 RPC 到达**：按 `MulticastName` 从标记表移除对应标记（语义：该数据的权威值已到，回滚时跳过它）；不触发委托。
7. **后续同步**：服务器多播、RPC 到达后，按原有逻辑修正客户端表现。

**约定**：

- 服务器只确认状态变化；后续变化由网络同步修正。
- **整体确认与整体回滚**：预测键结算只有两种结局，不做部分回滚。理由：服务器校验粒度本来就是状态级；可复制属性差异由权威值表修正；逐条确认的复杂度与收益不成正比。若未来出现"状态确认了但非复制数据无法自动修正"的场景，再考虑引入部分回滚。
- `ConfirmPrediction` / `RollbackPrediction` 为 private，外部只能通过 `ResolvePrediction` 触发结算。
- 结算后再次调用 `ResolvePrediction` 忽略。**不记 Warning**——结算已把键移出键表，重复回执与迟到回执同属正常竞态（与下条一致）。
- 结算后本键登记的标记**应已全部清除**（Confirmed：只清标记；Rejected / 超时：从表恢复 + 清标记，见 2.7.2 结算表）。标记已易主的字段由新键负责，本键不回写。唯一的残留差异是 CD 时间戳：本地预测值是服务器时间估算值，与精确值不等，等下一次表变化时写回（2.7.2 竞态 1）。

#### 2.11.4 服务器端调用时机

服务器在以下时机调用 `Client_ResolvePrediction`：

| 时机 | 说明 | 是否需要回执 |
| --- | --- | --- |
| **带键的 Server RPC 到达** | 客户端发送 `Server_Attack`、`Server_Escape`、`Server_ChangeSkillState` 时携带 `FPredictionKey`；服务器校验后回执 | 需要（`KeyID > 0`） |
| **不带键的同名 Server RPC 到达** | 客户端处于降级路径（`CanPredict() == false`）时直接发原 RPC，`KeyID == 0` | **不需要**：没有键可回执，服务器照常校验执行 |
| **服务器主动打断** | 服务器因受击、暂停、时停等**自行**改变客户端状态 | **不需要回执**，见下 |

**服务器主动打断为什么不回执**：这类打断的权威结果**本来就是通过权威值表与多播到达客户端的**（服务器改了 `CharacterState` → 经权威值表；改了锁 → `Client_CorrectLocks`；踢飞 → 多播）。**无标记**的字段（如被打断时的 `CharacterState`）在表到达时直接采用，本地立刻切到被打断的表现；**有标记**的字段由**本键的回执**收敛——本地那次预测发出的带键 RPC 仍会到达服务器，按 5.1 的判据必然被拒绝并回执 `Rejected`，该键随即按表值回滚（回执丢失时由超时兜底，见 2.10.1）。因此**不需要服务器专门为打断发一条回执**去"否认"一个它从未见过的键。

> 预测键不跨客户端同步：服务器只对**随 RPC 到达过的键**有认知（`FPredictionKey::PredictiveConnectionKey` 也正是这么记下来的），因此"服务器对未知键主动回执"在机制上就不可行，在设计上也不需要。

**RPC 归属带来的顺序问题（已接受）**：三个可带键的 RPC 并不在同一个 Actor 上——`Server_Attack` / `Server_Escape` 定义在 `AC_Character` 上，`Server_ChangeSkillState` 定义在 `AC_PlayerController` 上；而同一处输入还会伴随 `AC_Character` 上的 `Server_ChangeBox` / `Server_ChangeToward`。它们分属**不同的 ActorChannel**，UE 只保证"同一 Actor 上的可靠 RPC 保序"，跨 Actor 不保证。因此服务器处理 `Server_ChangeSkillState` 时，**不能假设**同一次输入里的 `Server_ChangeBox` 已经先到。

处理办法是让它们不互相依赖：**键只挂在状态变更请求上**（见 3.4.2 的挂载约束），碰撞框等同步自身携带完整参数（尺寸 / 偏移 / 翻转），服务器按"最后到达者为准"处理即可，不需要与键建立顺序关系。

### 2.12 预测系统实现方式

预测系统并非对原有网络层、角色逻辑或动画系统的重写，而是在现有项目之上叠加的一层**外置预测模块**。原有架构仍保持服务器权威：属性复制、Server RPC、Client RPC、NetMulticast 继续负责权威同步与表现分发；预测系统只负责在客户端输入瞬间先行产生本地结果，并在服务器同步到达后决定「跟进」还是「回滚」。因此实现原则是：**尽量不影响项目原先架构和代码，尽量以增量方式接入**。

基于这一原则，预测系统采用**组件（Component）形式**实现：预测组件挂载到角色上，随角色一同生成、复制和销毁，持有该角色的预测上下文；对外暴露预测键创建、预测标记、变化记录、委托注册、跟进/回滚结算等接口。角色原有逻辑不需要了解预测系统的内部结构，只需要在「需要预测的功能函数」中调用组件接口，并在适当位置注册少量回调。权威值表由**另一类组件**（权威值表组件）承载，它挂在承载被预测属性的宿主 Actor 上（PlayerState / Character），与预测组件各司其职（见 2.7.5）。

具体接入方式：

- **组件挂载**：在角色初始化时创建并挂载预测组件，与角色生命周期一致，服务器与客户端均可持有；预测逻辑只在客户端生效。
- **预测键创建与使用**：客户端输入触发本地先行逻辑时，由角色调用组件接口创建预测键；键下挂载本次预测衍生的状态变化、属性标记、位置变化、表现触发以及回滚/跟进委托。
- **原有代码的增量修改**：原有功能函数不需要被替换，只需在关键位置插入组件调用——状态本地置位处调用 `CreatePredictionKey`；可复制属性本地修改处调用 `MarkReplicatedAttribute`；非复制数据本地修改处调用 `RecordStateChange` / `RecordPresentation`；服务器同步到达处调用 `ResolvePrediction` / `ApplyAuthorityValueTable` / `OnMulticastArrived`。
- **委托回调的增量添加**：原有代码只需为需要跟进或回滚的非复制数据增加少量委托回调，回调内容通常是「恢复旧值」「确认新值」「重新按权威值驱动动画/UI」。可复制属性不需要委托——由权威值表统一负责。
- **锁的接入**：保持本地置位/复位不变，在服务器写锁处调用 `Client_CorrectLocks`，并在 `Server_Attack` 的拒绝路径补发一次（见 2.6.1）；不涉及预测键、记录与委托。
- **动画通知的两类分法**：接入时必须先把现有动画通知切成两类（见 3.8 与 5.9）。**时机类**（连段推进 `AN_ChangeAttack`（只切表现、不写段号，见 2.4.4）、位移 `AN_MakeMove`、碰撞框变更）承载的是时序信号，两端都要执行，且是"本地先行 + 记录"的首写点；**纯权威类**（特效、音效、纯表现开关）应改为 `HasAuthority()` 门控，只由服务器触发、再经多播分发。当前项目中两类混写（`HasAuthority()` 判断与两端执行交错），需要一并整理——否则预测系统会把"两端各播一次"的问题放大成"回滚后又播一次"。
- **降级策略（重要）**：预测系统不是"必须存在"的。若 `AC_Character` 上找不到预测组件，或组件判定当前不可预测（`CanPredict() == false`：组件未初始化、角色 / PlayerState / Controller 任一缺失、调试开关关闭；**本地控制判断由调用方在输入入口完成**，见 3.4.1），各处接入点**回退到原有路径**——直接发原 Server RPC，不建键、不写标记、不记变化。这既保证"未接入 / 被关闭"时游戏仍按原逻辑跑，也是逐功能增量接入的基础。

因此，预测系统的实现可以概括为：**一个挂载在角色上的预测组件，一张挂在各宿主 Actor 上的复制权威值表（表本身就是权威值），一套预测键与预测标记机制，一组由原代码注册的跟进/回滚委托**。

---

## 3. 预测系统组件

预测系统组件（后文简称预测组件）是预测系统的**唯一入口与运行时容器**。它挂载在 `AC_Character` 上，与角色生命周期一致，负责：

- **预测上下文管理**：保存当前客户端是否处于可预测状态、当前活跃预测键、历史预测记录等。
- **预测键生命周期管理**：创建、冻结、结算、销毁预测键。
- **预测标记管理**：为可复制属性记录「预测标记 + 预测键 ID」，在权威值表到达时更新权威值并清除标记。
- **权威值管理**：权威值由宿主的**权威值表组件**承载（见 2.7.5）。**预测组件不保存表的副本**——它只持有组件引用，读组件上那份"最后一次收到的表"（表本身就是客户端手上的权威值，见 2.7.2），并按采用规则决定是否写回本地属性。
- **非复制数据变化记录管理**：为非复制数据提供变化记录的写入、查询、回滚、确认接口。
- **委托注册与结算**：集中保存每个预测键的跟进/回滚委托，并在服务器同步到达时统一触发。
- **锁更正接收**：接收 `Client_CorrectLocks` 下发的权威锁值并覆盖本地锁（不建键、不记录）。
- **服务器同步配对**：在服务器回执 / 属性复制 / 多播到达时，根据预测键 ID 找到对应记录，决定跟进还是回滚。

### 3.1 挂载位置与生命周期

预测组件挂载在 `AC_Character` 上，因为预测系统本地先行修改的对象主要是 Character 上的状态、位置、朝向、锁，以及 PlayerState 上的权威属性；组件挂 Character 可以直接访问二者。**权威值表组件不在这里挂载**：它按"属性定义在哪个类上"分别挂在 `AC_PlayerState`（己方 + 敌方各一个）与 `AC_Character`（己方）上，见 2.7.5。

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

**第二个组件类：权威值表组件**（职责与挂载见 2.7.5，表结构见 3.3.1）

| 项 | 规范 |
| --- | --- |
| 类名 | `UC_AuthorityValueComponent` |
| 父类 | `UActorComponent` |
| UCLASS 宏 | `UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))` |
| 头文件路径 | `Source/Naruto/C_AuthorityValueComponent.h`（`FAuthorityValueTable` 也定义在此，预测组件包含它） |
| 源文件路径 | `Source/Naruto/C_AuthorityValueComponent.cpp` |
| API 宏 | `NARUTO_API` |
| 复制 | 构造函数中 `SetIsReplicatedByDefault(true)`；只复制一个属性 `AuthorityValueTable`（`ReplicatedUsing = OnRep_AuthorityValueTable`），服务器在 `PreReplication` 里刷新 |
| 与预测组件的关系 | **预测组件单向持有本组件的引用**（见 3.2 引用表），反向不持有——本组件只负责复制与刷新表，不调用预测逻辑。预测组件在客户端用 `FindComponentByClass` 绑定宿主上的它（做法与绑定 PlayerState 一致），在 `OnRep_AuthorityValueTable` 的委托上调用 `ApplyAuthorityValueTable` |

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
| 己方 `UC_AuthorityValueComponent`（PS 上、Character 上各一个） | 读取权威值表（`AuthorityValueTable`），并在 `OnRep_AuthorityValueTable` 时触发 `ApplyAuthorityValueTable`。 |
| 敌方 `UC_AuthorityValueComponent`（敌方 PS 上） | 同上，供 `Enemy.PS.*` 的采用与回滚使用。 |

- 服务器端同样挂载预测组件，但**只**用于：读取随 RPC 到达的 `FPredictionKey`（其 `PredictiveConnectionKey` 由 `NetSerialize` 记下）、在回执时把键原样带回、执行服务器校验。**不执行任何本地先行逻辑**。注意 `CanPredict()` 本身是纯上下文判断（见 3.4.1），服务器上它同样返回 true；服务器不先行，靠的是**服务器侧没有任何建键调用**，而不是靠这个返回值。

### 3.3 存储结构

组件内部维护以下容器：

| 容器 | 类型 | 用途 |
| --- | --- | --- |
| 预测键表 | `TMap<uint32, FPredictionKey>` | 存储**活跃与未结算**的预测键，键为预测键 ID，值为轻量预测键结构。结算后移除。 |
| 预测记录表 | `TMap<uint32, FPredictionRecord>` | 存储每个预测键对应的完整本地记录。 |
| 预测标记表 | `TMap<FName, uint32>` | 键为属性名（含 `Self.` / `Enemy.` 前缀），值为预测键 ID，表示该属性当前正在被哪个预测键预测。 |
| 权威值表引用（引用，非副本） | `UC_AuthorityValueComponent*` | 表**不在**本组件里：正文在权威值表组件上，本组件只持有该组件的引用（见 3.2）。客户端手上只有那一份，它同时就是权威值（见 2.7.2）；服务器侧也不另存副本，直接读真实属性。 |
| 委托表 | `TMap<uint32, FPredictionDelegates>` | 每个预测键的跟进/回滚委托列表，按注册顺序执行。 |
| 未结算缓冲池 | `TArray<uint32>` | 已冻结、待结算的预测键 ID；结算或超时回滚后出池。 |

**约定**：

- 预测记录保留上限：`MaxPredictionRecords`（默认 64，可配置）。**只淘汰已结算记录**（按时间淘汰最旧）；未结算记录不得淘汰。
- 预测键表保存「活跃键 + 未结算键」；结算后从表中移除。
- 预测标记表只保留"尚未收到权威值"的属性；权威值表带来该字段的权威值时清除。**位置类属性从不进入该表**（只记 `MoveBaseline`，见 2.5.1-C）。

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

// 权威值表：服务器权威值的统一通道（定义在 C_AuthorityValueComponent.h，见 2.7.5）
// 用带类型字段取代原来的 TMap<FName, FString>：不再需要字符串化与解析，
// 比较交由引擎的 member-wise 比较完成（见 2.7.5）
// 位置不入表（见 2.5.1-C）
//
// 本结构体被两处宿主复用：挂在 PlayerState 上的实例只填 PS 段字段，
// 挂在 Character 上的实例只填 Character 段字段。未使用的字段恒为默认值，
// 不参与任何判定，也不会产生歧义。
USTRUCT()
struct FAuthorityValueTable
{
    GENERATED_BODY()

    // ---- AC_PlayerState 段（含敌方 PS）----
    UPROPERTY() float HealthValue = 0.f;     // 己方不预测血量，仅敌方代理用（2.5.2）
    UPROPERTY() int32 Chakra = 0;
    UPROPERTY() int32 Attack = 0;
    UPROPERTY() int32 MySkill = 0;
    UPROPERTY() uint8 CharacterState = 0;    // ECharacterStateType；用 uint8 避免头文件循环包含

    // ---- AC_Character 段 ----
    UPROPERTY() bool  Toward = true;
    UPROPERTY() float LastEscapeTime = 0.f;
    UPROPERTY() float LastFirstSkillTime = 0.f;   // 这四个时间戳需先补复制，见 5.3
    UPROPERTY() float LastSecondSkillTime = 0.f;
    UPROPERTY() float LastScrollTime = 0.f;
    UPROPERTY() float LastSummonTime = 0.f;
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
| `FAuthorityValueTable` | 权威值表（**USTRUCT，会复制**）：被预测属性的服务器权威值。客户端保存的"最后一次收到的表"即客户端手上的权威值。位置不入表。 |
| `FPredictionDelegates` | 每个预测键的跟进/回滚委托列表。 |

**约定**：

- `FPredictionKey` 必须轻量，不存储变化记录、委托列表、旧值/新值。
- `FPredictionKey` 是唯一带自定义 `NetSerialize` 的结构体；`PredictiveConnectionKey` 是它唯一的非 `UPROPERTY` 成员，由 `NetSerialize` 手工维护。
- `FPredictionKey::KeyID > 0` 即"有效键"。**收到 `KeyID == 0` 时不要记 Warning**：这是"该键不属于本连接"的正常表示。
- `FPredictionRecord` 只存客户端，不跨 RPC。
- 委托使用 `DECLARE_DELEGATE` 而非动态多播，保证性能；绑定对象生命周期由原代码保证。
- `FStateChangeRecord::OldValue / NewValue` 使用 `FString`，仅支持可字符串化的值（枚举、int、float、bool、FName）。**权威值表不使用 `FString`**：它要参与整表复制与 member-wise 比较，必须用带类型字段。二者的差别是"非复制数据（只给回调看）"与"复制数据（要过网络与比较）"。
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
| `Skill` | 技能预测，对应 `Self.PS.MySkill`（技能的霸体状态由动画通知写入、不进技能键，见 5.9a）。 |
| `Escape` | 替身预测，对应 `MySkill`、`Chakra`、`LastEscapeTime`、位置瞬移。 |
| `Scroll` | 秘卷预测——与 `Summon` 同属 `MySkill = 4` 这**一个**状态，**键判据相同**（`MySkill` 不变即同一状态），类型只记录输入来源（`SummonIndex` 0 / 1），不产生两个键（见 2.4.4）。 |
| `Summon` | 通灵预测——同上；两者的动画相同，按同一状态处理。 |

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
| `FindPredictionRecord` | 服务器回执到达、权威值表到达、多播到达 | 按 ID 定位预测记录。非 const 版本用于结算时修改；const 版本用于只读查询。 |
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
// 首次标记时确保该属性的权威值表已存在
// 若该属性已有预测标记，更新为新 KeyID，从旧键记录的 ReplicatedAttributes 中移除该属性名，并记录 Warning
// 若 KeyID == 0 或 AttributeName 为空，忽略并记录 Warning
void MarkReplicatedAttribute(FName AttributeName, uint32 KeyID);

// 权威值表到达（OnRep_AuthorityValueTable）时调用：保存本表为权威值表，并逐字段套用采用规则
// 逐字段：有预测标记 → 只更新权威值表；无标记 → 写回本地属性；有标记但值已相等 → 只更新权威值表并清标记
// 所有字段同规则，Attack 没有专属分支（见 2.4.4）
// 表内未被本机标记过的字段照常更新权威值，只是没有标记可清
void ApplyAuthorityValueTable(const FAuthorityValueTable& Table);

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
| `ApplyAuthorityValueTable` | 客户端 `OnRep_AuthorityValueTable` 中 | 保存本表为权威值；逐字段套用采用规则（有标记 → 只更新权威值表；无标记 → 写回本地；有标记但值已相等 → 只更新权威值表并清标记）；清除已可确认字段的预测标记。 |
| `IsReplicatedAttributePredicted` | `OnRep` 中判断是否需要清除标记 | 避免重复清除。 |
| `RecordMoveBaseline` | 位移（技能位移 / 替身瞬移）执行前 | 捕获一次性位置基线，供拒绝/超时时恢复。位置**不走权威值表**（见 2.5.1-C / 2.9）。 |

**约定**：

- 可复制属性的预测标记由**权威值表到达**或**回执结算**清除（前者走"无标记则采用"路径；后者：Confirmed 清标记、Rejected / 超时清标记并恢复）。
- 若 RPC 回执先到，照常按 2.7.2 结算，**不等表到达**：Confirmed → 清除本键登记的可复制属性标记、本地值不动（服务器写下的就是同一个值）；Rejected / 超时 → 从权威值表恢复本地值并清标记。
- 标记**按字段清除**：`ApplyAuthorityValueTable` 只对"本表已带来该字段权威值"的字段清标记，不整表横扫。
- **位置类属性不调用 `MarkReplicatedAttribute`**：位置没有权威值可等、也不需要标记（标记的语义是"等权威值到达后清除"，而拥有者客户端根本等不到自身位置的复制）。位置只调用 `RecordMoveBaseline`。

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
- 变化记录只针对**非复制数据**；可复制属性不写入变化记录，只写预测标记（回滚依据是权威值表）。
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

// 权威值表到达时调用，更新权威值表、按采用规则写回本地属性、清除预测标记
void ApplyAuthorityValueTable(const FAuthorityValueTable& Table);

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
//   —— 回滚内容与 Rejected 完全一致：非复制数据恢复旧值、可复制属性从权威值表恢复、位置从 MoveBaseline 恢复
//   超时按 Rejected 处理：可复制属性从权威值表恢复并清标记（见 2.7.2 结算表）
void TickPredictionTimeout(float DeltaTime);

// 只读查询权威值表（调试用）
bool GetAuthorityValue(FName AttributeName, FString& OutValue) const;

private:
    // 服务器回执确认时执行跟进委托，清除非复制数据预测标记
    void ConfirmPrediction(uint32 KeyID, uint8 ConfirmedStatePacked);
    // 服务器回执拒绝/超时时执行回滚委托，恢复非复制数据旧值
    // 数值/朝向/时间戳从权威值表恢复；位置从 MoveBaseline 恢复（仅当 bHasMoveBaseline）
    void RollbackPrediction(uint32 KeyID);
    // 内部：套用权威值表的采用规则（逐字段，见 2.7.2 与 2.4.4）
    void ApplyAuthorityValueTableInternal(const FAuthorityValueTable& Table);
    // 内部：从权威值表恢复某个属性的本地值
    void RestoreFromAuthorityValue(FName AttributeName);
```

| 接口 | 调用时机 | 说明 |
| --- | --- | --- |
| `ResolvePrediction` | 服务器 `Client_ResolvePrediction` 回执 | 统一入口，根据 Result 自动调用 Confirm 或 Rollback。**键不存在时静默返回**（迟到回执属正常竞态）。结算后预测键移出预测键表与缓冲池；预测记录保留（`bResolved = true`），供调试与历史查询。 |
| `ApplyAuthorityValueTable` | `OnRep_AuthorityValueTable` 中 | 更新权威值；逐字段采用（有标记 → 只更新权威值表；无标记 → 写回本地；有标记但值已相等 → 只更新权威值表并清标记）；清除已确认字段的标记。**不依赖 `REPNOTIFY_Always`**（见 2.7.1）。 |
| `OnMulticastArrived` | 多播 / 客户端 RPC 到达时 | 按 `MulticastName` 从预测标记表移除对应标记；不触发委托；无对应标记时静默返回。 |
| `TickPredictionTimeout` | `AC_Character::Tick` 中调用 | 检查**所有未结算记录**是否超时；超时则强制回滚，避免本地预测状态永久残留。必须使用世界时间判定。 |
| `ConfirmPrediction` | `ResolvePrediction` 内部调用 | 执行跟进委托；清除非复制数据预测标记；**清除本键登记的可复制属性标记、不写回本地值**（见 2.7.2 结算表：服务器写下的就是同一个值，之后到达的表按无标记路径再写一次，幂等）。 |
| `RollbackPrediction` | `ResolvePrediction` / `TickPredictionTimeout` 内部调用 | 按变化记录逐项执行回滚委托，恢复非复制数据旧值；**按权威值表恢复本键登记的可复制属性**（跳过已无标记或已易主的属性）；**位置按 `MoveBaseline` 恢复**。 |

**约定**：

- 服务器只确认状态变化；后续变化由网络同步修正。
- 整体确认与整体回滚：预测键结算只有两种结局，不做部分回滚。
- 可复制属性的预测标记由权威值表到达或回执结算清除；**回滚不依赖任何网络到达**——可复制属性的恢复由**权威值表组件上那份表**完成（预测组件只读它、不存副本，见 2.7.2 / 3.3）。
- 回滚边界由预测键下登记的内容决定：记录了哪些非复制变化、标记了哪些可复制属性，就回滚哪些。
- `ConfirmPrediction` 与 `RollbackPrediction` 为 private，外部只能通过 `ResolvePrediction` 触发结算。
- 结算后再次调用 `ResolvePrediction` 忽略。**不记 Warning**——结算已把键移出键表，重复回执与迟到回执同属正常竞态（与下条一致）。
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
    │       Self.PS.MySkill = 1
    │       MarkReplicatedAttribute("Self.PS.MySkill", PK_001)
    │
    ├─ 属性预测：预扣 Chakra、记录 CD 时间戳
    │       MarkReplicatedAttribute("Self.PS.Chakra", PK_001)
    │       MarkReplicatedAttribute("Self.Char.LastFirstSkillTime", PK_001)
    │
    ├─ 属性预测：位置本地先行（由动画通知 AN_MakeMove 在两端执行）
    │       RecordMoveBaseline()                // 捕获位移前位置（一次性基线，不入表）
    │       AddActorLocalOffset(Offset)         // 位移本体
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
            │       └─ 清除本键登记的可复制属性标记（不写回本地值）
            │
            └─ Result == Rejected → RollbackPrediction(PK_001)
                    ├─ 执行回滚委托：恢复非复制状态变量旧值
                    ├─ 从权威值表恢复 Self.PS.MySkill / CharacterState / Chakra
                    ├─ 从 MoveBaseline 恢复位置（若 bHasMoveBaseline）
                    └─ 重新按权威值驱动动画

权威值表到达（OnRep_AuthorityValueTable）
    │
    └─ ApplyAuthorityValueTable(Table)
            ├─ 保存本表为权威值表
            ├─ 逐字段采用：无标记 → 写回本地属性；有标记 → 只更新权威值表
            │   （有标记但值已相等 → 只更新权威值表并清标记；所有字段同规则）
            └─ 清除已确认字段的预测标记

多播到达（如 Mult_ChangeGrabLocation / Mult_ChangeProtectedAnim）
    │
    └─ OnMulticastArrived("GrabLocation")
            └─ 按名字从预测标记表移除对应标记（该数据权威值已到，回滚时跳过）；
               无对应标记则静默返回（多播不带键，见 5.10）

锁更正到达
    │
    └─ Client_CorrectLocks(Mask, Values)   // 含 Server_Attack 拒绝路径的补发（2.6.1）
            └─ 直接覆盖本地锁值（不回滚、不查预测键）
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
    │       记录权威值基线由组件自动维护
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
    ├─ 权威值表到达 → ApplyAuthorityValueTable → 更新权威值 + 按采用规则写回 + 清标记
    ├─ 位置【不经属性复制回到本机】：拥有者收不到自身 ReplicatedMovement（见 2.9）；
    │   对手看到的瞬移由其模拟代理的位置复制体现，自身一致性由移动校正通道兜底
    └─ Client_ResolvePrediction(PK_002, Result, ...) → 结算非复制数据

若结果为 Rejected
    └─ RollbackPrediction：Chakra / LastEscapeTime 从权威值表恢复；位置从 MoveBaseline 恢复
```

### 3.7 接口冻结说明

以下接口规范自本文档发布起冻结，后续开发不得随意更改签名与语义：

| 类别 | 冻结内容 |
| --- | --- |
| 组件类 | `UC_PredictionComponent : UActorComponent`，`UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))`，`NARUTO_API`，路径 `Source/Naruto/C_PredictionComponent.h`；**权威值表组件** `UC_AuthorityValueComponent`（同为 `UActorComponent`），路径 `Source/Naruto/C_AuthorityValueComponent.h`（`FAuthorityValueTable` 定义在此），复制属性 `AuthorityValueTable`（`ReplicatedUsing = OnRep_AuthorityValueTable`） |
| 生命周期 | `InitializePredictionContext`（返回 bool）、`CanPredict`、`GetActivePredictionKeyID`、`GetActivePredictionKey` |
| 预测键 | `CreatePredictionKey`、`EndPredictionKey`、`BindStateLifecycle`、`FindPredictionRecord`（const / 非 const）、`IsPredictionKeyActive` |
| 预测标记 | `MarkReplicatedAttribute`、`ApplyAuthorityValueTable`、`IsReplicatedAttributePredicted`、`RecordMoveBaseline`（位置一次性基线） |
| 非复制数据 | `RecordStateChange`、`RecordPresentation`、`GetStateChangeRecords`、`GetPresentationRecords` |
| 结算 | `ResolvePrediction`、`OnMulticastArrived(FName)`、`TickPredictionTimeout`、`GetAuthorityValue` |
| 回执宿主 | `AC_PlayerController::Client_ResolvePrediction`（Client, Reliable；宿主为 PlayerController 而非 Character，理由见 2.11.1） |
| 锁更正 | `AC_Character::Client_CorrectLocks`（Client, Reliable，位域约定见 2.8） |
| 枚举 | `EPredictionType` |
| 结构体 | `FPredictionKey`（USTRUCT + UPROPERTY + **自定义 `NetSerialize`**，只对发起连接有效）、`FPredictionRecord`（含 `MoveBaseline` / `bHasMoveBaseline`）、`FStateLifecycleBinding`、`FStateChangeRecord`、`FPresentationRecord`、`FAuthorityValueTable`（USTRUCT，**会复制**，带类型字段）、`FPredictionDelegates` |
| 委托类型 | `FConfirmDelegate`、`FRollbackDelegate` |
| 前提（非接口，同样冻结） | 被预测的复制属性**从逐属性复制中摘除**、改由权威值表下发（2.7.1）；其中 `Attack` 必须摘除；位置类属性**不入表**、只记一次性基线（2.5.1-C / 2.9） |

**扩展原则**：

- 新增预测功能时，优先复用现有接口，不新增接口。
- 若必须新增接口，需在本文档追加版本号与变更说明。
- 接口内部实现可优化，但签名与语义保持稳定。

### 3.8 与原有系统的协作边界

预测组件不直接修改角色逻辑，也不直接驱动动画/UI。它只负责「标记、记录、配对、结算」，具体恢复/确认逻辑由原代码注册的委托完成。这样，预测组件与角色逻辑、动画系统、UI 系统之间保持松耦合。

| 原有系统 | 预测组件的协作方式 |
| --- | --- |
| 网络层 | 不替代原有网络层；Server RPC、Client RPC、NetMulticast 仍按原方式工作。预测组件在 **3 个状态变更请求**（`Server_Attack` / `Server_Escape` / `Server_ChangeSkillState`）的参数中附加 `FPredictionKey`，并在回执到达时配对；额外新增一条锁更正 RPC `Client_CorrectLocks` 与一条回执 RPC `AC_PlayerController::Client_ResolvePrediction`。**现有 4 条 `Mult_*` 的签名不变**（见 5.10）。 |
| 属性复制 | **被预测的属性改走权威值表**（见 2.7）：在 `GetLifetimeReplicatedProps` 中删除对应的 `DOREPLIFETIME`（`AC_PlayerState` 的 `Attack`、`Chakra`、`MySkill`、`CharacterState`、`HealthValue`；`AC_Character` 的 `Toward`、`LastEscapeTime` 与 5.3 的四个 CD 时间戳），改由该宿主上的 `UC_AuthorityValueComponent::AuthorityValueTable` 统一下发。其余属性（如 `Team`）照旧走逐属性复制。 |
| 角色逻辑 | 不替代角色逻辑；原有移动、攻击、技能、受击、替身等核心流程仍由原代码驱动。预测组件只在关键位置插入组件调用。 |
| 移动系统 | 不介入常规移动；移动由 UE `CharacterMovementComponent` 自带客户端预测与服务器校正负责。技能位移与替身瞬移做位置预测，回滚用**一次性基线**（`RecordMoveBaseline`），**不**依赖 `SetReplicateMovement`——拥有者收不到自身移动复制（见 2.9）。 |
| 动画系统 | 不替代动画系统；动画状态机仍读取 `MyAttack`、`MyCState`、`MySkill`、`MySpeed` 等变量。预测组件只让这些变量（经由 PlayerState）在客户端更早进入预测值。**接入前必须先把动画通知切成两类**（时机类 / 纯权威类，见 2.12 与 5.9）。 |
| UI 系统 | 不替代 UI 系统；UI 仍读取 `HealthValue`、`Chakra`、CD 状态等变量。预测组件只让这些变量在客户端更早进入预测值。 |
| 摄像机 | 不纳入预测；摄像机读取（含预测值在内的）最终朝向与位置，被动跟随，无需预测支持。 |
| 锁 | 不进入预测键机制：本地先行置位，服务器写锁处（外加 `Server_Attack` 的拒绝路径）通过 `Client_CorrectLocks` 下发权威值；客户端不做锁的回滚（见 2.6.1）。 |
| 服务器 | 服务器端同样挂载预测组件，但只作为预测键回传与校验结果的参照，不执行本地先行逻辑。 |

---

## 4. 预测系统的扩展方式

后续扩展新预测功能时，只需要重复以下固定模式：

0. **判定通知类别（先决）**：确认本功能涉及的动画通知属于「时机类」还是「纯权威类」（见 2.12 / 5.9）。只有时机类才能在客户端作为本地首写点；纯权威类必须改为 `HasAuthority()` 门控。
1. **创建预测键**：在客户端输入触发本地先行逻辑时，由状态预测调用 `CreatePredictionKey`，并按需 `BindStateLifecycle`。
2. **标记可复制属性**：在本地修改可复制属性处调用 `MarkReplicatedAttribute`（含敌方代理属性）。**位置例外**：位移 / 瞬移改调 `RecordMoveBaseline()`，不写标记。
3. **记录非复制变化**：在本地修改非复制数据处调用 `RecordStateChange` / `RecordPresentation`，并注册跟进/回滚委托（先状态、后表现）。
4. **服务器同步后结算**：在服务器回执到达处调用 `ResolvePrediction`；在权威值表到达处调用 `ApplyAuthorityValueTable`；在多播到达处调用 `OnMulticastArrived`。
5. **超时兜底**：由 `AC_Character::Tick` 调用 `TickPredictionTimeout`，无需逐功能实现。
6. **降级**：每个接入点都以 `CanPredict()` 为前置判断，为 false 时走原有路径。**不允许出现"只有预测路径、没有原始路径"的接入**——这是"预测系统可整体关闭"的保证（见 2.12）。

**锁预测的扩展方式不同**：只需要在服务器每一处写锁的位置调用 `Client_CorrectLocks` 下发权威值，客户端本地置位/复位保持不变；不建键、不记录、不注册委托。**普攻的拒绝路径也要补发一次**——服务器拒绝普攻时并没有写锁，那一次补发是客户端本地置位与拒绝成对的唯一时机（见 2.6.1）。

---

## 5. 接入前置条件（待实现项）

以下事项是预测系统能够按本文档工作的**前提**，当前代码尚未具备，需在接入阶段完成。

### 5.1 服务器校验（P3）

当前 `Server_ChangeSkillState` / `Server_ChangeChakra` / `Server_ChangeAttackState` / `Server_ChangeCharacterState` 均为**裸赋值，无任何校验**；客户端已在输入函数中做了可行性判断（状态是否为 `Normal`/`Protected`、CD 是否为 0 等）。

需把这些校验**原样搬到服务器**（客户端保留同样的校验作为本地先行判定）。补齐后，拒绝路径才真正存在，权威值表与回滚机制才成为必需。

> **定案：本节进第一批**。它是"拒绝路径"存在的前提——没有它，回滚分支在实机上根本触发不到，也就无从验证；因此不能与预测主体分期。

### 5.2 被预测属性改走权威值表（前提）

**权威值表不再依赖逐属性复制的 `OnRep`**（理由见 2.7.1 的三个洞）。要做的是三件事。

**(a) 新增权威值表组件并挂载**：`UC_AuthorityValueComponent`（见 2.7.5）挂到 `AC_PlayerState` 与 `AC_Character` 上——构造函数里 `CreateDefaultSubobject`，并 `SetIsReplicatedByDefault(true)`。

**(b) 把被预测的属性从逐属性复制中摘除**：

| 类 | 属性 | 处理 |
| --- | --- | --- |
| `AC_PlayerState` | `Attack`、`Chakra`、`MySkill`、`CharacterState`、`HealthValue` | 从 `GetLifetimeReplicatedProps`（`C_PlayerState.cpp:22-31`）中删除对应的 `DOREPLIFETIME`；`UPROPERTY` 声明保留（本地读写照旧），改由该 PS 上的 `AuthorityValueTable` 下发 |
| `AC_Character` | `Toward`（`C_Character.h:89-90`）、`LastEscapeTime`（`C_Character.h:339-340`） | 同上，改由该 Character 上的 `AuthorityValueTable` 下发；`Replicated` 说明符可一并去掉，保留 `UPROPERTY` |
| 其它 | `Team`（`ReplicatedUsing = OnRep_Team`，`C_PlayerState.h:60-61`）等 | **不动**：`Team` 不参与预测，继续走逐属性复制 |

> `Attack` 是**最不能保留复制**的那一个：每按一次普攻都制造一次"本地已写下、服务器还没收到"的窗口，比别的属性密集得多。但规则上它与别的被预测属性没有区别——**需要"收到权威值但先不采用"的是所有被本地先行写入的属性**（`MySkill`、`Chakra`、`CharacterState`、`HealthValue`、`Toward`、CD 时间戳），不是 `Attack` 独有。摘除之所以一体适用，是因为逐属性复制在反序列化时就把值写进了对象内存（`RepLayout.cpp:3330-3351`），没有拦截点。
>
> 再说一遍这里的因果：摘除属于 **HOLD 规则**的要求（本地值要有唯一写者），**不是回滚机制的要求**——回滚由回执/超时驱动，与逐属性复制无关（见 2.7.1 末）。换句话说，"保留复制也能回滚，但本地值会被引擎提前改掉"是准确的，"保留复制回滚就跑不起来"是不准确的。

**(c) 不要做的事**（早期版本的本节要求已废弃）：

- **不需要**给这些属性补 `ReplicatedUsing`；
- **不需要** `DOREPLIFETIME_CONDITION_NOTIFY(..., REPNOTIFY_Always)`；
- **不需要**为权威值写任何 `OnRep_*` 回调——表的 `OnRep` 只有一个 `OnRep_AuthorityValueTable`。

> 记一笔：`REPNOTIFY_Always` 只在**接收侧**生效（`FRepParentCmd::RepNotifyCondition`，`RepLayout.h:764`；设置点 `RepLayout.cpp:6243`），它能"让 `OnRep` 多触发几次"，但既解决不了"逐属性复制没有拦截点"（第三个洞），也仍需为每个属性各写一个回调。因此它对本设计不再有任何位置。

### 5.3 CD 时间戳补复制

`LastFirstSkillTime` / `LastSecondSkillTime` / `LastScrollTime` / `LastSummonTime` 当前是**未复制的普通成员**（`C_Character.h:342-348`，连 `UPROPERTY` 都没有；只有 `LastEscapeTime` 是 `Replicated`）。它们必须能被客户端拿到、且其权威值由服务器决定，否则"CD 时间戳本地先行 + 服务器权威校验"不成立。

**这正是权威值表相对逐属性复制省事的地方**：这四个变量**保持普通成员即可**——不需要加 `Replicated`，不需要 `UPROPERTY`，也不需要 `REPNOTIFY_Always`。只要把字段加进 `FAuthorityValueTable`（已列在 3.3.1），服务器在 `PreReplication` 里从这些成员读值填表即可。相比之下，走逐属性复制就必须先把它们提升为反射属性并注册复制。

> 位置类**不进表**（见 2.5.1-C / 2.9），因此不需要为位置做任何复制登记。

### 5.4 替身瞬移的客户端本地先行

`Server_Escape_Implementation` 中的瞬移（`SetActorLocation`，`C_Character.cpp:402`）目前**仅在服务器执行**，客户端没有本地先行路径。需要在客户端 `Escape()` 中加入本地判定与本地瞬移，并**在瞬移前调用 `RecordMoveBaseline()`** 捕获一次性基线（见 2.9）。

> 客户端本地判定所需的数据当前都已在客户端可得：`LastEscapeTime` 已复制（`C_Character.h:339-340`），落点可用本机的 `PlaceMark` 计算。若某个必需量最终仍不可得，则本项**降级为不预测瞬移**，只保留服务器权威路径（见 2.12 降级策略）——不要为了预测而临时新增一条数据通道。

### 5.5 锁更正通道

新增 `AC_Character::Client_CorrectLocks`，在 2.8 列出的每一处服务器写锁位置调用，**外加 `Server_Attack_Implementation` 末尾的一次补发**（含拒绝路径，见 2.6.1）。锁**不**加 `Replicated`；客户端收到后直接覆盖本地锁值，不做任何记录。

### 5.6 `bSuccessHit` 复位点

`bSuccessHit` 目前只在服务器命中判定时被置 `true`（`C_Character.cpp:441`），**C++ 里没有任何复位路径**。**定案：它是纯服务器侧的值，置位与复位都归服务器**——全项目只有一处使用（某一个技能用它判断是否进入技能的下一段），因此复位点与读取点同处：那门技能的服务器判定逻辑消费掉它之后随即复位，不引入新的复位时机。客户端只经 `Client_CorrectLocks` 接收。该锁不参与段键回滚，其权威值只来自这条更正通道（与 2.6.1 对 `bAttackInputLock` 的规则一致）。

### 5.7 超时检查接入

在 `AC_Character::Tick` 中调用组件 `TickPredictionTimeout`，且必须使用世界时间判定（见 2.3.1 实现约束）。

放置位置有讲究：`AC_Character::Tick` 在 `GetInformation()` 之后有 PS / GameState 的空指针早退（`C_Character.cpp:456-459`），超时检查必须放在该早退**之后**，否则角色数据未就绪期间超时检查会静默停摆。同时 `Tick` 在服务器与客户端都会执行：`TickPredictionTimeout` 内部首先判断 `CanPredict()`；服务器与模拟代理上从不建键、记录表为空，因此这次调用天然是空转（`CanPredict()` 本身不判角色，见 3.4.1）。

### 5.8 顺带修复（重构时一并处理）

接入预测会把下面这些问题从"平时不发作"放大成"必然发作"，需一并处理：

| 位置 | 问题 | 处理 |
| --- | --- | --- |
| `AC_Character::AddChakra`（`C_Character.cpp:154-159`） | 直接调用 `Server_ChangeChakra_Implementation` 而非走 RPC，且 `Cast<AC_PlayerController>(Controller)` 结果未判空 | 改为调用 RPC，并判空 |
| `AC_Character::MyInitialize` | 同样直接调用 `Server_ChangeToward_Implementation` | 改为走 RPC |
| `AC_Character::ChangeAttack`（`C_Character.cpp:185-205`） | 写 `PS->Attack` / `PS->MySkill` / `PS->CharacterState` / 锁，并在第 199-200 行直接调用 `Server_ChangeToward_Implementation`；`PS` 未判空 | 判空；**段号写入点按 2.4.4 移到普攻输入点**，本函数只切表现；`attack == 0` 分支按端区分（服务器写 0 / 拥有者客户端写 0 并置预测标记 / 其它端不写） |
| `bPreInputLock` 的复位点（声明 `C_Character.h:170`；使用 `C_Character.cpp:198` 读、`:414` 置 `true`） | 全项目只有 `Server_Attack_Implementation` 把它置 `true`、`ChangeAttack` 读它；**C++ 中没有任何置 `false` 的位置**，实际依赖动画蓝图复位 | **定案：改用 C++ 接管**——在 `AN_ChangeAttack` 的连段推进点置位/复位，并在该处注释里写明；在此之前**不要**把它当连段窗口闸门使用（2.4.4 的闸门用的是 `bAttackInputLock`） |
| `AC_Character::Attack`（`C_Character.cpp:274-277`） | 只有一行 `Server_Attack()`，无任何本地检查 | 按 2.4.4 加两道闸门（`bAttackInputLock == false` + 状态是 `Normal`/`Protected`），通过后再本地写段号、置锁、建键、发带键 RPC |
| `AC_Character::ChangeState`（`C_Character.cpp:207-210`） | `GetPlayerState<AC_PlayerState>()->CharacterState = target;` 无权限检查、无判空 | 加权限语义与判空；客户端先行改为走预测接口 |
| `AC_Character::MakeMove`（`C_Character.cpp:212-228`） | `AddActorLocalOffset` **没有权限语义**：客户端调用会在本地直接生效（`PrimitiveComponent.cpp:2513` / `SceneComponent.cpp:3018` 的 `MoveComponentImpl` 只做移动、无权威检查、不报错），随后被移动校正 / 位置复制拽回 | 明确"两端各自执行"的语义；客户端路径改由动画通知内先行 + `RecordMoveBaseline` |
| `AC_Character::Tick` 内直接调用 `Mult_ChangeGrabLocation`（`C_Character.cpp:510`） | 服务器从 Tick 里调 NetMulticast，触发频率与调用点都不明确 | 明确触发条件与频率（建议只在抓取状态变化时调一次） |
| 服务器校验（P3） | 四个 `Server_*` 均为裸赋值、零校验 | 见 5.1，接入前必须补齐 |

> 上面这些"先判空、再走 RPC、再分权限"的修复与预测无关，是接入过程中必然要碰到的既有问题；单独提交、单独验证，不要与预测逻辑混在一个改动里。

### 5.9 动画通知拆分与碰撞框本地先行

**两件事，先后有序。**

**(a) 动画通知拆成两类。** 当前项目在动画通知里把"时序信号"与"纯表现"混在一起，多处 `if (HasAuthority())` 与两端执行交错。接入预测前必须切开：

| 类别 | 判定标准 | 处理 |
| --- | --- | --- |
| **时机类** | 通知的**发生时刻**本身对状态机有意义：连段推进（`AN_ChangeAttack`，**只切表现、不写段号**，见 2.4.4）、位移（`AN_MakeMove`）、碰撞框尺寸 / 偏移变更、状态切换到下一段的时点、**状态授予（`AN_ChangeState`——技能一/二写 `Armor`、奥义/通灵写 `Unbreakable`，见下）** | 两端都执行；在客户端它是"本地先行 + `Record*` 记录"的首写点 |
| **纯权威类** | 通知只产生表现，不改变任何被复制 / 被校验的量：特效、音效、纯表现开关 | 改为 `HasAuthority()` 门控，由服务器触发后经 NetMulticast 分发；客户端不再自行执行 |

> 判定标准只有一条：**"如果这个通知在客户端提前执行了，会不会让某个量进入一个服务器可能不同意的值？"** 会 → 时机类；不会 → 纯权威类。
>
> 注意 `Mult_ChangeProtectedAnim` / `Mult_ChangeGravity` 这类多播**兼具表现与状态**：表现部分（动画示意）按权威门控，状态部分（`bInProtectAnim`、`LaunchState`）必须保持两端一致，不能简单当作"纯表现"处理。

> **`AN_ChangeState` 与技能键的关系（定案）**：它是唯一给 `CharacterState` 写霸体值的路径（`Armor` / `Unbreakable` / `Adamantine`；C++ 里无人写这三个值，取值由动画序列上的该通知给出），**写入不进技能键**——不写预测标记、不随技能键回滚。后果是本地霸体在"通知已跑、表里还是旧值"的窗口里会被采用规则第二行（无标记 → 写回本地）覆盖一次，直到服务器自己的同名通知运行、表带上霸体值才恢复；LAN 下窗口约一帧（不可见），`Net PktLag=100` 压测时可见。若将来要消除这个窗口，最小改动是让该通知在拥有者客户端的写入挂当前活跃键（`MarkReplicatedAttribute("Self.PS.CharacterState", GetActivePredictionKey())` 一行），代价是它不再是"只切表现"。

**(b) 碰撞框本地先行。** 这是 2.5.2 命中预测的前置条件，单独列为验收项：

- 现状：攻击框的尺寸 / 偏移 / 翻转**唯一**的变化路径是 `Server_ChangeBox`（Server RPC）→ `Mult_ChangeBoxSize`（NetMulticast）往返（`C_Character.cpp:161-171` → `127-152`）；组件上的 `SetIsReplicated(true)`（`C_Character.cpp:33/41`）只覆盖 `USceneComponent` 那几个 `COND_None` 的变换属性，而决定命中几何的 `UBoxComponent::BoxExtent`（裸 `UPROPERTY`，`BoxComponent.h:23-24`）与碰撞启用状态（`BodyInstance` 未注册复制）**都不走复制**，只能等 `Mult_ChangeBoxSize` 到达，所以攻击者本地做命中判定时几何会滞后一个 RTT。
- 目标：把碰撞框的尺寸 / 偏移 / 翻转改为**由已复制状态派生**——两端在 `AC_Character::Tick` 里按 `PS->CharacterState`（经 `GetInformation()` 已同步到 `MyCState`）与 `Toward` 计算，或另在 `AC_Character` 上新增一组 `Replicated` 的碰撞框属性（尺寸 / 偏移 / 翻转），在 `OnRep` 里应用到组件（这三个量不参与预测，不需要权威值表、也不需要 `REPNOTIFY_Always`）。**注意**：引擎的 `UBoxComponent::BoxExtent` 本身不是复制属性（`BoxComponent.h:23-24`），不能直接给它加 `Replicated`，所以要复制就必须在角色上另立一份。**优先选"派生"**：它同时消掉了往返延迟与乱序问题，也让 `Server_ChangeBox` / `Mult_ChangeBoxSize` 这对 RPC 有机会整体删除。
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
