# 赛尔号PVP对战系统 - 效果触发时机文档

**版本**: 2026-04-17

---

## 一、项目概述

本项目是**赛尔号PVP对战模拟系统**，采用**41状态FSM**（状态机）控制战斗流程。

### 核心架构

- **BattleContext**: 战斗上下文，管理双方精灵(`SeerRobot`)、当前状态、效果注册表(`stateActions`)
- **BattleWorkspace**: 回合临时数据层，存放本回合的增减伤、闪避率、操作选择等临时变量
- **EffectFactory**: 效果工厂，注册10+种基础效果（麻痹、中毒、烧伤、恐惧等）
- **Skills**: 技能对象，绑定技能效果列表 `vector<pair<State, Effect>>`
- **AbnormalState**: 异常状态（3类：控制型/属性变化型/特殊buff型）

---

## 二、完整回合流程（41状态）

```
┌─────────────────────────────────────────────────────────────────┐
│                     操作阶段 (OPERATION)                         │
├─────────────────────────────────────────────────────────────────┤
│ GAME_START → ENTER_EXIT_STAGE → CHOOSE_SKILL_MEDICAMENT       │
│           → PROTECTION_MECHANISM_1 → ENTER_STAGE               │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                     战斗阶段 (BATTLE)                           │
├─────────────────────────────────────────────────────────────────┤
│ BATTLE_ROUND_START (回合初始化)                                 │
│                                                              │
│ ── 先手方行动链 ──                                            │
│ FIRST_MOVE_RIGHT → FIRST_ACTION_START                         │
│ → FIRST_BEFORE_SKILL_HIT → FIRST_ON_SKILL_HIT                │
│ → FIRST_SKILL_EFFECT → FIRST_ATTACK_DAMAGE                   │
│ → FIRST_AFTER_ACTION → FIRST_ACTION_END                      │
│ → FIRST_AFTER_ACTION_END → FIRST_EXTRA_ACTION                │
│ → FIRST_MOVER_DEATH                                           │
│                                                              │
│ ── 后手方行动链 ──                                            │
│ SECOND_ACTION_START ~ SECOND_EXTRA_ACTION                     │
│ SECOND_MOVER_DEATH                                            │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                   回合结束阶段 (ROUND_END)                      │
├─────────────────────────────────────────────────────────────────┤
│ ROUND_END → OLD_ROUND_END_1 → REDUCTION_ALL_ROUND_MINUS       │
│ → ROUND_REDUCTION_NEW_ROUND_END → OLD_ROUND_END_2             │
│ → DEATH_TIMING → DEFEAT_STATUS → OPPONENT_DEFEAT_STATUS       │
│ → NEW_DEFEAT_MECHANISM → PROTECTION_MECHANISM_2               │
│ → AFTER_DEFEATED → CHOOSE_AFTER_DEATH                        │
│ → AFTER_DEFEATING_OPPONENT → ROUND_COMPLETION → FINISHED      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 三、效果触发时机详解

### 3.1 特性/魂印类效果（回合级触发）

| 特性触发点 | 状态名 | 触发场景 |
|-----------|--------|---------|
| **回合开始时** | `BATTLE_ROUND_START` | 回合初始化、临时变量重置、触发"每回合开始时"效果 |
| **先手权判定前** | `BATTLE_FIRST_MOVE_RIGHT` | 特性如"抢先手"、速度修正效果 |
| **先手权判定后** | `BATTLE_FIRST_MOVE_RIGHT` | 双方先手权已确定，可触发"抢夺先手"类效果 |
| **先手方出手前** | `BATTLE_FIRST_ACTION_START` | 特性如"电能累积"、对手"带电"检测 |
| **先手方命中前** | `BATTLE_FIRST_BEFORE_SKILL_HIT` | 特性如"攻击前触发盾"、技能拦截判定 |
| **先手方命中时** | `BATTLE_FIRST_ON_SKILL_HIT` | **核心判定点！** 技能命中/未命中生效 |
| **先手方效果结算** | `BATTLE_FIRST_SKILL_EFFECT` | 技能附加效果（中毒、烧伤等）正式结算 |
| **先手方伤害结算** | `BATTLE_FIRST_ATTACK_DAMAGE` | 伤害计算 → 加算减伤 → 乘算减伤 → 应用伤害 |
| **先手方行动后** | `BATTLE_FIRST_AFTER_ACTION` | 行动后触发效果（吸血、反弹伤害检测） |
| **先手方行动结束** | `BATTLE_FIRST_ACTION_END` | 额外行动（连击）判定 |
| **先手方死亡时** | `BATTLE_FIRST_MOVER_DEATH` | 濒死复活、死亡延迟判定 |
| **回合结束时** | `BATTLE_ROUND_END` | 异常结算（中毒扣血）、回合效果清理 |
| **时长扣减时** | `BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS` | 所有持续N回合的效果时长-1 |

### 3.2 "带电"特性判定流程（示例）

```
对手使用物理攻击技能
         ↓
┌──────────────────────────────────────┐
│ BATTLE_FIRST_ON_SKILL_HIT            │
│ （技能命中时——物理攻击命中生效后）   │
│                                      │
│ 1. 技能命中判定通过                   │
│ 2. 执行 effect_apply_xxx（技能附加） │
│ 3. 特性检测：                         │
│    if (attacker.hasTrait("带电")      │
│        && skill.type == PHYSICAL) {  │
│        roll_paralysis_chance(25%);   │
│    }                                 │
└──────────────────────────────────────┘
         ↓
┌──────────────────────────────────────┐
│ BATTLE_FIRST_SKILL_EFFECT            │
│ （技能效果正式结算）                  │
│                                      │
│ - 上毒/烧伤/麻痹等异常状态应用       │
│ - 技能特效（如吸血、护盾）生效      │
└──────────────────────────────────────┘
```

### 3.3 伤害结算分阶段（关键！）

伤害结算在 `BATTLE_FIRST_ATTACK_DAMAGE` 状态内分多步执行：

```
┌─────────────────────────────────────────────────────────┐
│ BATTLE_FIRST_ATTACK_DAMAGE 伤害结算                     │
├─────────────────────────────────────────────────────────┤
│ Step 1: 计算基础伤害                                    │
│   baseDamage = calculateDamage(attacker, defender)      │
│   pendingDamage.base = baseDamage                       │
│                                                         │
│ Step 2: 应用加算减伤 (additive reduction)               │
│   afterAdd = baseDamage × (1 - Σadd%)                   │
│   pendingDamage.afterAdd = afterAdd                     │
│   ↓                                                    │
│   【此时调用"检测型"效果】                              │
│   例：250护盾（受到250以下伤害免伤）                   │
│   ↓                                                    │
│ Step 3: 应用乘算减伤 (multiplicative reduction)         │
│   afterMul = afterAdd × ∏(1 - mul_i%)                 │
│   resolvedDamage.afterMul = afterMul                    │
│   ↓                                                    │
│   【此时调用"最终伤害型"效果】                         │
│   例：伤害翻倍、伤害+固定值                             │
│   ↓                                                    │
│ Step 4: 应用最终伤害                                    │
│   defender.hp -= resolvedDamage.final                   │
└─────────────────────────────────────────────────────────┘
```

### 3.4 异常状态Efftype分类与触发阶段

| Efftype | 类型 | 示例 | 典型触发点 |
|---------|------|------|-----------|
| **0** | 控制类异常 | 麻痹(0)、恐惧(6)、睡眠(8)、石化(9) | `ON_SKILL_HIT`（技能命中时）概率判定 |
| **1** | 属性变化类 | 中毒(1)、烧伤(2)、衰弱(11)、沉默(30) | `SKILL_EFFECT`（技能效果结算）直接应用 |
| **2** | 特殊buff类 | 山神守护(12)、狂暴(14)、免疫(17) | `ROUND_START`或`ACTION_START`触发 |

---

## 四、效果注册机制（关键问题）

### 4.1 效果注册时机

效果在以下时机注册到 `stateActions[robotId][State]`：

| 时机 | 注册方式 | 示例 |
|------|---------|------|
| **回合开始** | 预注册 | "每回合开始时增伤10%" |
| **技能使用前** | 即时注册 | 技能选择时注册"该技能增伤"效果 |
| **伤害结算中** | 延迟注册 | 在 `pendingDamage` 阶段注册"伤害后触发"效果 |
| **精灵上场** | 被动注册 | 切入免异常、切出驱散 |

### 4.2 效果执行函数

```cpp
void BattleContext::execute_registered_actions(int target, State state) {
    auto& actions_map = stateActions[target];
    auto it = actions_map.find(state);
    if (it != actions_map.end()) {
        for (const auto& [priority, effect] : it->second) {
            effect.logic(this, effect.args);
        }
    }
}
```

---

## 五、核心效果ID对照表

| EffectID | 名称 | 优先级 | 作用 |
|---------|------|--------|------|
| 0 | `effect_hit_invalid` | 1 | 命中效果失效（免疫控制类） |
| 1 | `effect_skill_invalid` | 2 | 技能无效（封印技能） |
| 2 | `effect_keep_turn` | 3 | 免断回合（不被中断） |
| 3 | `effect_break_turn` | 4 | 断回合（中断对手） |
| 10 | `effect_apply_poison` | 3 | 上毒 |
| 11 | `effect_apply_fear` | 3 | 上恐惧 |
| 12 | `effect_drain_hp` | 3 | 吸血 |

---

## 六、典型效果触发场景汇总

| 效果名称 | 触发条件 | 触发时机（状态） |
|---------|---------|-----------------|
| **带电(麻痹)** | 对手使用**物理攻击**技能且**命中** | `BATTLE_FIRST_ON_SKILL_HIT` |
| **毒特性** | 技能附加中毒效果 | `BATTLE_FIRST_SKILL_EFFECT` |
| **每回合扣血** | 异常状态持续 | `BATTLE_ROUND_END`（回合结束） |
| **攻击前护盾** | 受到攻击前 | `BATTLE_FIRST_BEFORE_SKILL_HIT` |
| **受伤反伤** | 受到伤害后 | `BATTLE_FIRST_AFTER_ACTION` |
| **濒死复活** | HP≤0时 | `BATTLE_FIRST_MOVER_DEATH` |
| **出场加成** | 精灵上场时 | `OPERATION_ENTER_STAGE` |
| **回合开始增伤** | 每回合开始 | `BATTLE_ROUND_START` |

---

## 七、状态对应效果执行摘要

```
【操作阶段】
GAME_START                  → 游戏初始化
ENTER_EXIT_STAGE           → 精灵切换效果
CHOOSE_SKILL_MEDICAMENT     → 技能/药剂选择
PROTECTION_MECHANISM_1      → 保活机制（HP≤0→1）
ENTER_STAGE                → 登场时效果

【战斗阶段】
ROUND_START                → 回合开始效果（如：每回合开始时XX）
FIRST_MOVE_RIGHT           → 先手权判定效果
FIRST_ACTION_START         → 出手开始效果（如：攻击前XX）
FIRST_BEFORE_SKILL_HIT     → 命中前效果（如：闪避判定）
FIRST_ON_SKILL_HIT         → 【命中时】← 异常状态附加关键点！
FIRST_SKILL_EFFECT         → 【技能效果】← 效果正式结算
FIRST_ATTACK_DAMAGE        → 【伤害结算】← 伤害计算+减伤
FIRST_AFTER_ACTION         → 行动后效果
FIRST_ACTION_END           → 行动结束
FIRST_EXTRA_ACTION         → 额外行动（连击）
FIRST_MOVER_DEATH          → 死亡判定

【回合结束】
ROUND_END                   → 回合结束效果
REDUCTION_ALL_ROUND_MINUS   → 时长扣减
DEATH_TIMING                → 统一死亡处理
DEFEAT_STATUS              → 战败判定
```

---

## 八、 BattleWorkspace 设计说明

### 8.1 设计目的

将 BattleContext 中的"回合临时变量"抽取到独立的 `BattleWorkspace` 结构中，保持 Context 清晰，每回合开始时重置。

### 8.2 应包含的字段（建议）

```cpp
struct BattleWorkspace {
    // ===== 操作选择 =====
    int roundChoice[2][2];           // [方数][操作类型, 参数索引] - 可移入
    int lastActionType[2];            // 上一次操作类型
    int lastActionIndex[2];          // 上一次操作参数

    // ===== 先手权 =====
    PreemptiveRight preemptive_right;

    // ===== 伤害相关 =====
    DamageSnapshot pendingDamage;     // 加算减伤后的伤害
    DamageSnapshot resolvedDamage;    // 所有减伤后的最终伤害

    // ===== 减伤槽位（4槽位 x 2方）=====
    int damage_reduce_add[2][4];      // 加算减伤百分比
    int damage_reduce_mul[2][4];      // 乘算减伤百分比

    // ===== 临时属性修正（只在本回合生效）=====
    float dodge_rate[2];             // 闪避率
    float hit_rate_mod[2];            // 命中率修正
    float crit_rate_mod[2];           // 暴击率修正
    int damage_add[2];               // 伤害加成（固定值）
    float damage_mul[2];              // 伤害加成（百分比）

    // ===== 临时状态标记 =====
    bool has_attacked[2];             // 本回合是否已攻击
    bool has_used_skill[2];           // 本回合是否已使用技能
    int extra_action_count[2];        // 额外行动次数

    // ===== 缓存的计算结果 ======
    int calculated_speed[2];           // 缓存的速度（考虑异常状态后）
    int calculated_crit_damage[2];     // 暴击伤害倍率

    // ===== 重置函数 =====
    void reset() {
        // 所有字段归零/归默认值
        memset(this, 0, sizeof(BattleWorkspace));
        // 设置一些默认值
        for (int i = 0; i < 2; i++) {
            dodge_rate[i] = 0.0f;
            hit_rate_mod[i] = 1.0f;
            crit_rate_mod[i] = 1.0f;
            damage_mul[i] = 1.0f;
        }
    }
};
```

### 8.3 使用方式

```cpp
class BattleContext {
public:
    // 核心数据（持久化）
    SeerRobot seerRobot[2];
    std::unordered_map<State, std::unordered_map<int, Effect>> stateActions[2];
    // ...

    // 临时数据层（每回合重置）
    BattleWorkspace workspace;

    // 回合开始时重置
    void resetWorkspace() {
        workspace.reset();
    }
};
```

---

## 九、其他设计建议

### 9.1 关于字段归类

当前 BattleContext 中混合了：
- **持久化数据**：`seerRobot[2]`、`on_stage[2]`、`stateActions`
- **半持久化**：`roundCount`、`uuid`
- **临时数据**：`roundChoice`、`damage_reduce_add/mul`、`preemptive_right`

建议统一迁移到 `workspace`，并在 `BATTLE_ROUND_START` 时调用 `resetWorkspace()`。

### 9.2 关于 EffectArgs 的问题

当前 `EffectArgs` 使用裸指针传递参数，建议封装为值类型或使用 `std::variant`/`std::any`：

```cpp
struct EffectArgs {
    std::vector<int> int_args;
    std::vector<std::vector<int>> vec_args;
    std::any extra;  // 更安全的额外数据传递
};
```

### 9.3 关于状态转移

当前 `generateState()` 是简单的 `(currentState + 1) % 41`，这在某些需要跳跃的场景（如死亡后跳到 CHOOSE_AFTER_DEATH）会出问题。建议改用显式状态转移表。

---

**文档版本**: 2026-04-17
