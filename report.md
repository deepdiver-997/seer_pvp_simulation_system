# 《赛尔号》PVP战斗系统 - FSM实现进度报告

**报告时间**: 2026年  
**项目阶段**: FSM状态机完善实现  
**核心目标**: 基于豆包提示词指引，完成40+状态处理函数，实现完整的PVP战斗流程

---

## 一、当前系统架构概览

### 1.1 技术栈
- **语言**: C++17
- **异步框架**: boost::asio (TCP网络通信 + async_read_some/async_write)
- **并发机制**: 线程池 (BoostThreadPool + IOThreadPool)
- **数据存储**: SQLite (官方数据导入)
- **状态机**: 迭代式FSM (41个状态，顺序递推)

### 1.2 核心数据结构现状

#### BattleContext (include/fsm/battleContext.h)
**已实现字段**:
```cpp
// 效果注册与执行
std::unordered_map<State, std::unordered_map<int, Effect>> stateActions[2];

// 对战主体
SeerRobot seerRobot[2];           // 双方机器人 (各6只精灵)
int on_stage[2];                  // 当前在场精灵索引

// 操作历史
int roundChoice[2][2];            // [方数][操作类型, 参数索引]
int lastActionType[2];            // 上一次操作类型
int lastActionIndex[2];           // 上一次操作参数

// 伤害系统 (NEW)
DamageSnapshot pendingDamage;     // 加算减伤后的伤害
DamageSnapshot resolvedDamage;    // 所有减伤后的最终伤害
int damage_reduce_add[2][4];      // 加算减伤百分比 (4个槽位/方)
int damage_reduce_mul[2][4];      // 乘算减伤百分比 (4个槽位/方)

// 流程控制
bool preemptive_right;            // true = 先手权持有者
int roundCount;                   // 当前回合数
State currentState;               // 当前状态

// 网络与监听
std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;
std::vector<char> m_buffer;
unsigned int uuid;
```

#### DamageSnapshot (NEW)
```cpp
struct DamageSnapshot {
    int attackerId;      // 伤害来源 (0/1)
    int defenderId;      // 伤害目标 (0/1)
    int base;            // 基础伤害
    int afterAdd;        // 加算减伤后
    int afterMul;        // 乘算减伤后
    int final;           // 最终伤害 (应用后)
    int addPct;          // 加算减伤百分比
    double mulCoef;      // 乘算减伤系数 (∏(1-mul%))
    bool isRed;          // 是否为减伤
    bool isDirect;       // 是否直伤 (无护盾)
    bool isFixed;        // 是否固定伤害
    bool isCrit;         // 是否暴击
};
```

### 1.3 已实现的核心算法

#### 伤害减伤结算 (include/numerical-calculation/calculation.h)
```cpp
/*
 * 公式: finalDamage = baseDamage × (1 - addSum%) × ∏(1 - mul_i%)
 * 
 * 加算减伤 (damage_reduce_add):
 * - addSum = min(sum(add_reduce[0..3]), 100%)
 * - 上限为100%（防止负伤害）
 * 
 * 乘算减伤 (damage_reduce_mul):
 * - 无上限约束（源于《赛尔号》官方机制）
 * - 但效果递减：每层乘算都会稀释最终伤害
 */
int applyDamageReduction(int baseDamage, 
                         int add_reduce[4], 
                         int mul_reduce[4]);
```

#### 效果槽位管理 (resources/moves_lib/lib_1.cpp)
```cpp
// 注册加算减伤 (如"30%加算减伤")
set_additive_damage_reduction_slot(context, [targetId, slot, percent])

// 注册乘算减伤 (如"乘算系数0.7")
set_multiplicative_damage_reduction_slot(context, [targetId, slot, percent])

// 清空所有减伤
clear_damage_reduction_slots(context, [targetId])
```

---

## 二、FSM 状态机现状分析

### 2.1 已完善的处理函数 (3/41)

#### ✅ handle_GameStart
```cpp
void BattleFsm::handle_GameStart(std::shared_ptr<BattleContext> battleContext) {
    log("Game Start.");
    // TODO: 初始化双方精灵状态、清空临时效果、设置roundCount=0
}
```

#### ✅ handle_OperationProtectionMechanism1
```cpp
void BattleFsm::handle_OperationProtectionMechanism1(...) {
    // 保活机制: HP≤0 → HP=1
    battleContext->execute_registered_actions(1, State::OPERATION_PROTECTION_MECHANISM_1);
    battleContext->execute_registered_actions(2, State::OPERATION_PROTECTION_MECHANISM_1);
    
    if(battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp <= 0)
         battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp = 1;
    if(battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp <= 0)
         battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp = 1;
    
    battleContext->generateState();
}
```

#### 🟡 handle_BattleFirstMoveRight (逻辑框架存在，不完整)
```cpp
void BattleFsm::handle_BattleFirstMoveRight(...) {
    // 当前状态: 仅处理"使用药剂→无先手权"的特殊情况
    // 缺失: 速度对比逻辑、效果加成计算
    
    ElfPet &p1 = battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]];
    ElfPet &p2 = battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]];
    
    if (battleContext->roundChoice[0][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = false;
        return;  // ⚠️ 但没有比较速度和优先级!
    }
    if (battleContext->roundChoice[1][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = true;
        return;
    }
}
```

#### ✅ handle_ChooseAfterDeath (async网络处理)
```cpp
void BattleFsm::handle_ChooseAfterDeath(...) {
    // 异步读取客户端数据→宠物选择→继续流程
    battleContext->async_read([this](...) {
        // 数据验证、解析、操作应用、状态递进
    });
}
```

### 2.2 空实现的处理函数 (38/41) 

**操作阶段两个**:
- `handle_OperationEnterExitStage` - 切出/切入时的状态清理
- `handle_OperationChooseSkillMedicament` - 选择技能/药品 (async, 等待客户端输入)

**战斗回合初始化**:
- `handle_BattleRoundStart` - 回合初始化

**先手方行动链 (9个)**:
- `handle_BattleFirstActionStart` → `handle_BattleFirstBeforeSkillHit` → `handle_BattleFirstOnSkillHit` 
- → `handle_BattleFirstSkillEffect` → `handle_BattleFirstAttackDamage` 
- → `handle_BattleFirstAfterAction` → `handle_BattleFirstActionEnd` 
- → `handle_BattleFirstAfterActionEnd` → `handle_BattleFirstExtraAction`

**后手方行动链 (9个)**:
- `handle_BattleSecondActionStart` ~ `handle_BattleSecondExtraAction` (结构与先手方对称)

**死亡与阵亡相关 (5个)**:
- `handle_BattleFirstMoverDeath` - 先手方阵亡判定
- `handle_BattleRoundEnd` - 回合结束清理
- `handle_BattleSecondMoverDeath` - 后手方阵亡判定
- `handle_BattleDeathTiming` / `handle_BattleDefeatStatus` - 战败判定
- ... 约10个其他死亡/战败相关处理

**回合收尾与状态递进 (8个)**:
- `handle_BattleOldRoundEnd1` ~ `handle_BattleRoundCompletion`

---

## 三、核心问题与疑惑

### 问题1: 效果注册的生命周期不清晰 ⚠️

**现象**:
- `stateActions[robotId][state]` 中存储效果，但何时注册、何时执行的时机完全不明确
- 豆包提示词说"循环遍历所有已注册效果并调用"，但没说**何时向map中插入效果**

**具体疑惑**:

```cpp
// 场景A: 切入免异常的魂印（首回合触发）
// 问题: 这个效果应该在哪个状态注册？
// A1) 在 handle_OperationEnterStage 中注册，当前状态也执行？
// A2) 在 handle_OperationEnterStage 中注册，在后续 handle_BattleRoundStart 中才执行？
// A3) 还是应该在上一个回合就注册好（"预注册"）？

// 场景B: "持续3回合减伤30%" 的效果
// 问题: 时长扣减在哪里做？
// B1) 在 handle_BattleRoundReductionAllRoundMinus 中统一扣减？
// B2) 在该状态检查时长 ≤ 0 则清除？
// B3) 若某效果在"第3回合结束时"应清除，在哪个函数体内验证？
```

**影响**:
- 导致效果可能不执行、重复执行、或执行时机错误
- 特别是"前置条件效果"（如技能选择时的增伤）容易被遗漏

---

### 问题2: 伤害计算的分阶段时机不明确 ⚠️

**豆包提示词要求**:
- 基础操作 + 加算减伤 → 调用"检测型"效果（如250免伤判定）
- 加算后 + 乘算减伤 → 调用"最终伤害型"效果（如伤害翻倍）

**代码中的模糊**:

```cpp
// handle_BattleFirstAttackDamage 中应该发生什么？

int base = Calculation::calculateDamage(attacker, defender, skill);
ctx->pendingDamage.final = base;

// ❓ 这里应该调用加算减伤吗？
// ❓ 还是等到这个函数的后半部分再做？

ctx->pendingDamage.final -= (base * addSum / 100);

// ❓ 现在应该调用所有"伤害检测型"效果吗？
execute_effects_for_pending_damage();

// ❓ 还是继续做乘算减伤？
ctx->resolvedDamage.final = apply_multiplicative_reduction(ctx->pendingDamage.final);

// ❓ 再调用"最终伤害型"效果？
execute_effects_for_final_damage();

// ❓ 最后才应用伤害？
apply_damage_to_defender(ctx->resolvedDamage.final);
```

**实际的隐患**:
- 用户提到"快速魂印可在伤害结算后再修改一次"，导致伤害大幅变化
- 这说明需要在多个时机分别调用效果，但代码中没有明确区分

---

### 问题3: 先后手处理的代码重复 ⚠️

**现象**:
```cpp
// 先手方行动链
void handle_BattleFirstActionStart(...) { /* 逻辑A */ }
void handle_BattleFirstBeforeSkillHit(...) { /* 逻辑A_1 */ }
...

// 后手方行动链
void handle_BattleSecondActionStart(...) { /* 完全相同的逻辑A */ }
void handle_BattleSecondBeforeSkillHit(...) { /* 完全相同的逻辑A_1 */ }
...
```

**可复用机会**:
```cpp
// 建议提取通用处理
void execute_action_phase(std::shared_ptr<BattleContext> ctx, int actorId) {
    ElfPet &actor = ctx->seerRobot[actorId].elfPets[ctx->on_stage[actorId]];
    
    // 检查行动可行性
    if (actor.status & SEAL) {
        // 被封印，跳过行动
        return;
    }
    
    // 调用"行动开始"效果、执行技能选择、伤害计算等
    // 避免40+个重复的处理函数
}
```

---

### 问题4: 状态跳过/阵亡处理流程不清晰 ⚠️

**问题**:
- 如果在 `handle_BattleFirstActionStart` 中发现精灵已阵亡，是否应该跳到 `handle_BattleFirstMoverDeath`？
- 还是继续执行所有后续状态直到自然到达死亡检测点？

**generateState() 的问题**:
```cpp
void BattleContext::generateState() {
    currentState = static_cast<State>((static_cast<int>(currentState) + 40) % 41);
}
```
- 这样的递推会**跳过某些重要检查点**
- 如果需要"立即判定某方阵亡"，应该怎样跳转？

---

### 问题5: 回合效果的时长管理 

**现象**:
- `damage_reduce_add` / `damage_reduce_mul` 中存储百分比值，但没有"时长"字段
- 如果某个减伤效果只持续"本回合"或"3回合"，如何管理？

**建议补充结构**:
```cpp
struct EffectDuration {
    int effectId;
    int remainingRounds;  // 剩余回合数
    int addPct;
    int mulPct;
    // ...
};

std::vector<EffectDuration> activeEffects[2];  // 双方的活跃效果列表
```

---

## 四、豆包提示词 (豆包提示词.md) 解读

### 4.1 FSM范式 (核心模板)

每个状态处理函数应遵循以下模式:

```
对于每个状态处理函数(state_handler):
    1. 执行" 基础操作 "（如伤害计算、HP扣减、判定阵亡等）
    2. 遍历并调用所有已注册的 EffectFn:
        - 效果函数仅修改上下文状态（不改基础逻辑）
        - 传递 EffectArgs(包含必要参数：攻击者/目标/伤害值等)
    3. 效果执行后做 最终校验 (如伤害≥0、HP≤Max等)
```

### 4.2 各阶段的典型实现

| 状态 | 基础操作 | 需触发的效果场景 | 
|------|---------|-----------------|
| 切入 | 更新 on_stage、清理临时效果 | 切入免异常、驱散上一个精灵的负面 |
| 技能选择 | 校验技能可用、扣 PP | 特定技能增伤、技能封印拦截 |
| 先手权 | 对比速度 + 优先级 | 速度修正、抢夺先手 |
| 行动开始 | 检查封印/恐惧无行动 | 行动前清负面、增伤效果注册 |
| 技能命中前 | 校验命中率、防护判定 | 闪避、技能拦截 |
| 技能命中时 | **判定命中/未命中** | **命中→上毒、未命中→回血** |
| 技能效果 | 执行效果逻辑(如上状态) | 效果暴击、免疫抵抗 |
| 攻击伤害 | `calculateDamage() + applyDamageReduction()` | **增/减伤魂印、吸血、反伤、暴击翻倍** |
| 行动后 | 清理回合内临时效果 | 行动后清负面、附加灵魂印记 |
| 行动结束 | 判定额外行动(连击) | 连击概率增强 |
| 回合结束 | 时长-1、清临时增益 | 异常结算、印记回血 |
| 阵亡判定 | 统一检查 HP≤0 | 濒死复活、苟活延迟 |
| 战败判定 | `allive() == 0?` | 战败豁免、最后反击 |

---

## 五、实现优先级与建议

### 第一批优先实现 (⭐ 关键闭环)

#### 1. handle_BattleRoundStart
```cpp
void BattleFsm::handle_BattleRoundStart(...) {
    // 基础操作
    battleContext->roundCount++;
    // 重置本回合的临时标记
    battleContext->lastActionType[0] = -1;
    battleContext->lastActionIndex[0] = -1;
    battleContext->lastActionType[1] = -1;
    battleContext->lastActionIndex[1] = -1;
    
    // 调用已注册的"回合开始效果"
    battleContext->execute_registered_actions(0, State::BATTLE_ROUND_START);
    battleContext->execute_registered_actions(1, State::BATTLE_ROUND_START);
    
    battleContext->generateState();
}
```

#### 2. handle_BattleFirstMoveRight (完善)
```cpp
void BattleFsm::handle_BattleFirstMoveRight(...) {
    /* 基础操作: 速度对比 */
    ElfPet &p1 = battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]];
    ElfPet &p2 = battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]];
    
    // 药剂使用情况：药剂方无先手权
    if (battleContext->roundChoice[0][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = false;
    } else if (battleContext->roundChoice[1][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = true;
    } else {
        // 基于速度 + 优先级
        int speed1 = p1.speed;
        int speed2 = p2.speed;
        battleContext->preemptive_right = (speed1 >= speed2);
    }
    
    // 调用效果
    battleContext->execute_registered_actions(0, State::BATTLE_FIRST_MOVE_RIGHT);
    battleContext->execute_registered_actions(1, State::BATTLE_FIRST_MOVE_RIGHT);
    
    battleContext->generateState();
}
```

#### 3. handle_BattleFirstActionStart & handle_BattleSecondActionStart
```cpp
void execute_action_phase(std::shared_ptr<BattleContext> ctx, int actorId) {
    ElfPet &actor = ctx->seerRobot[actorId].elfPets[ctx->on_stage[actorId]];
    
    // 基础操作: 检查封印/恐惧等禁止行动状态
    if (actor.status & StatusType::SEAL) {
        ctx->lastActionType[actorId] = -1;  // 标记无法行动
        // 直接跳过中间的技能执行阶段?
        // 还是继续流转到自然的检测点?
        // TODO: 需要确认设计意图
    }
    
    // 调用"行动开始效果"
    ctx->execute_registered_actions(actorId, 
        (actorId == 0) ? State::BATTLE_FIRST_ACTION_START 
                       : State::BATTLE_SECOND_ACTION_START);
    
    ctx->generateState();
}

void BattleFsm::handle_BattleFirstActionStart(...) {
    execute_action_phase(battleContext, 0);
}

void BattleFsm::handle_BattleSecondActionStart(...) {
    execute_action_phase(battleContext, 1);
}
```

#### 4. handle_BattleFirstAttackDamage ⭐ 最关键
```cpp
void execute_damage_phase(std::shared_ptr<BattleContext> ctx, int attackerId, int defenderId) {
    ElfPet &attacker = ctx->seerRobot[attackerId].elfPets[ctx->on_stage[attackerId]];
    ElfPet &defender = ctx->seerRobot[defenderId].elfPets[ctx->on_stage[defenderId]];
    
    // 基础操作1: 计算基础伤害
    int base_damage = Calculation::calculateDamage(attacker, defender, 
                                                   attacker.skills[ctx->lastActionIndex[attackerId]]);
    ctx->pendingDamage.base = base_damage;
    ctx->pendingDamage.attackerId = attackerId;
    ctx->pendingDamage.defenderId = defenderId;
    
    // 基础操作2: 应用加算减伤
    int add_reduce_pct = 0;
    for (int i = 0; i < 4; i++) {
        add_reduce_pct += ctx->damage_reduce_add[defenderId][i];
    }
    add_reduce_pct = std::min(add_reduce_pct, 100);  // 上限100%
    
    int damage_after_add = base_damage * (100 - add_reduce_pct) / 100;
    ctx->pendingDamage.afterAdd = damage_after_add;
    ctx->pendingDamage.addPct = add_reduce_pct;
    
    // 调用"伤害检测型效果" (如250免伤判定)
    // ❓ 应该单独为这个时机注册效果吗?
    // ctx->execute_registered_actions(defenderId, State::BATTLE_FIRST_DAMAGE_CHECK);
    
    // 基础操作3: 应用乘算减伤
    double mul_coef = 1.0;
    for (int i = 0; i < 4; i++) {
        int mul_reduce_pct = ctx->damage_reduce_mul[defenderId][i];
        mul_coef *= (1.0 - mul_reduce_pct / 100.0);
    }
    
    int damage_after_mul = (int)(damage_after_add * mul_coef);
    ctx->resolvedDamage.final = damage_after_mul;
    ctx->resolvedDamage.afterMul = damage_after_mul;
    ctx->resolvedDamage.mulCoef = mul_coef;
    
    // 调用"最终伤害型效果" (如伤害翻倍)
    // ctx->execute_registered_actions(defenderId, State::BATTLE_FIRST_DAMAGE_FINAL);
    
    // 基础操作4: 应用伤害
    defender.hp -= damage_after_mul;
    if (defender.hp < 0) defender.hp = 0;
    ctx->resolvedDamage.final = damage_after_mul;
    
    // 基础操作5: 最终校验
    assert(defender.hp >= 0);
    
    ctx->generateState();
}

void BattleFsm::handle_BattleFirstAttackDamage(...) {
    int attacker = (battleContext->preemptive_right) ? 0 : 1;
    int defender = 1 - attacker;
    execute_damage_phase(battleContext, attacker, defender);
}

void BattleFsm::handle_BattleSecondAttackDamage(...) {
    int attacker = (battleContext->preemptive_right) ? 1 : 0;
    int defender = 1 - attacker;
    execute_damage_phase(battleContext, attacker, defender);
}
```

---

### 第二批完善 (流程衔接)

- **handle_BattleFirstBeforeSkillHit / OnSkillHit / SkillEffect**: 命中判定与效果结算
- **handle_BattleFirstAfterAction / AfterActionEnd**: 行动后清理与状态判定
- **handle_BattleFirstExtraAction**: 额外行动判定(连击概率)
- **handle_BattleRoundEnd / BattleSecondMoverDeath**: 回合结束与死亡检测

### 第三批深化 (规则验证)

- **handle_BattleRoundReductionAllRoundMinus**: 效果时长扣减
- **handle_BattleOldRoundEnd1 / NewRoundEnd / OldRoundEnd2**: 跨回合效果清理
- **handle_BattleDeathTiming / DefeatStatus**: 战败判定与特殊豁免
- **handle_OperationEnterExitStage**: 精灵切换与效果继承

---

## 六、代码组织建议

### 6.1 提取通用处理函数

```cpp
// 位置: include/fsm/battleFsm.h (private section)

// 通用行动阶段执行
void execute_action_phase(std::shared_ptr<BattleContext> ctx, int actorId);

// 通用伤害结算
void execute_damage_phase(std::shared_ptr<BattleContext> ctx, 
                         int attackerId, int defenderId);

// 伤害前效果调用 (检测型)
void invoke_pre_damage_effects(std::shared_ptr<BattleContext> ctx, 
                               int defenderId);

// 伤害后效果调用 (最终型)
void invoke_post_damage_effects(std::shared_ptr<BattleContext> ctx, 
                                int defenderId);

// 效果时长扣减与清理
void reduce_effect_durations(std::shared_ptr<BattleContext> ctx);

// 检查是否全灭
bool check_all_defeated(std::shared_ptr<BattleContext> ctx, int robotId);
```

### 6.2 明确的状态转移规则

```cpp
// battleContext->generateState() 改进建议:
// 不应该简单的 (state + 1) % 41，而应该基于实际流程

void BattleContext::generateState() {
    State nextState = currentState;
    
    switch (currentState) {
        case State::GAME_START:
            nextState = State::OPERATION_PROTECTION_MECHANISM_1;
            break;
            
        case State::BATTLE_FIRST_ACTION_START:
            // 如果先手被封印，可能应该跳到BATTLE_FIRST_MOVER_DEATH?
            // 需要明确规则
            nextState = State::BATTLE_FIRST_BEFORE_SKILL_HIT;
            break;
            
        // ... 手动管理每个转移
        
        default:
            nextState = static_cast<State>((static_cast<int>(currentState) + 1) % 41);
    }
    
    currentState = nextState;
}
```

---

## 七、遗留问题清单

| 问题编号 | 问题描述 | 优先级 | 状态 |
|---------|---------|--------|------|
| P1 | 效果注册时机与生命周期 | 🔴 高 | 待定 |
| P2 | 伤害计算分阶段时机 | 🔴 高 | 待定 |
| P3 | 先后手处理代码重复 | 🟡 中 | 可重构 |
| P4 | 阵亡处理与状态跳转 | 🔴 高 | 待定 |
| P5 | 效果时长管理机制 | 🟡 中 | 需补充数据结构 |
| P6 | 优先级排序与冲突检测 | 🟡 中 | 待定 |
| P7 | 异常状态的"行动禁止"逻辑 | 🟡 中 | 需确认设计 |
| P8 | 敌我双方非对称规则 | 🟡 中 | 需补充 |

---

## 八、下一步行动计划

### Phase 1: 问题澄清与决策 (1-2天)
1. **确认效果注册时机** - 是否需要"预注册"机制？还是"当前状态即时注册即时调用"？
2. **明确伤害时机切分** - 是否需要额外的 `DAMAGE_CHECK` / `DAMAGE_FINAL` 状态？
3. **决定状态转移方式** - 保持简单的 +1 模式，还是手动管理转移逻辑？

### Phase 2: 关键函数实现 (3-5天)
1. `handle_BattleRoundStart` - 回合初始化
2. `handle_BattleFirstMoveRight` - 先手权结算 (完善)
3. `execute_action_phase()` - 通用行动处理
4. `execute_damage_phase()` - 通用伤害处理 (含效果插入点)

### Phase 3: 行动链完成 (5-7天)
1. 先手方完整行动链 (9个处理函数)
2. 后手方完整行动链 (9个处理函数)
3. 额外行动判定与重入

### Phase 4: 收尾与测试 (5-7天)
1. 死亡/战败检测完善
2. 跨回合效果时长管理
3. 测试与PVP规则验证

---

## 附录：参考文档

- **豆包提示词**: [豆包提示词.md](豆包提示词.md) - FSM实现指南
- **官方数据映射**: SQL导入脚本 (scripts/import_seer_official_sqlite.py)
- **伤害计算**: [include/numerical-calculation/calculation.h](include/numerical-calculation/calculation.h)
- **效果库**: [resources/moves_lib/lib_1.cpp](resources/moves_lib/lib_1.cpp)
- **当前FSM框架**: [include/fsm/battleFsm.h](include/fsm/battleFsm.h)

---

**报告完成于**: 2024年  
**作者**: GitHub Copilot  
**状态**: 待审核与讨论
