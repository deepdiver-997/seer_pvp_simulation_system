/**
 * battle_primitives.cpp — 原语实现
 *
 * 原语 = 技能/魂印效果程序调用的原子动作。返回结果枚举表达"发生了什么"。
 * 原语通过 BattleContext 的内核（效果桶 / 异常状态 / 事件中心 / 免疫内核）完成动作。
 *
 * 本文件承载：
 * - apply_anomaly    （从 abnormal-system/abnormal-applicator 迁移）
 * - break_round_effects
 */

#include <primitives/battle_primitives.h>

#include <abnormal-system/abnormal-types.h>
#include <entities/elf-pet.h>
#include <entities/mark.h>
#include <fsm/battleContext.h>

#include <iostream>

namespace {

// 成功（异常状态实际改变）时向事件中心投递事件：
// - EVENT_ANOMALY_APPLIED：任意异常施加成功（第三方"当对方被挂异常时XXX"监听）
// - EVENT_CONTROLLED：控场类异常施加成功（第三方"当对方被控场时XXX"监听）
// emit 只入队，不内联执行；FSM 在 State 桶后 drain 投递。
void emit_anomaly_events(BattleContext* ctx, int target, int anomaly_id, int actor) {
    ctx->event_center_.emit(BattleEvent{EventType::EVENT_ANOMALY_APPLIED, actor, target});
    if (is_control_abnormal_status(static_cast<AbnormalStatusId>(anomaly_id))) {
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_CONTROLLED, actor, target});
    }
}

} // namespace

// ----------------------------------------------------------------
// apply_anomaly — 施加异常状态的统一入口
//
// 施加流程（按顺序执行的检查链）：
//   [Step 1] 参数校验
//   [Step 2] 目标存活检查
//   [Step 3] 免疫检查 — 免疫内核 is_immune(ANOMALY) + Mark ID 0 兜底
//   [Step 4] 同种异常 — 比较剩余回合，用长的覆盖短的
//   [Step 5] 控场替换 — 已有控场则清除旧控场，施加新控场
//   [Step 6] 特殊阻止检查（预留：抗性/装备/场地/保护机制）
//   [Step 7] 执行施加 — 写入 abnormal_status_end_round，成功路径 emit 事件
//
// 为什么集中在一个函数？
//   1. 异常免疫机制多种多样，分散在各处极易遗漏
//   2. 未来新增阻止条件时，只需在此函数加检查分支，所有调用方自动生效
//   3. 日志/调试在此集中输出
// ----------------------------------------------------------------
ApplyAnomalyResult apply_anomaly(BattleContext* ctx,
                                 int target,
                                 int anomaly_id,
                                 int duration_rounds,
                                 int actor) {
    // [Step 1] 参数校验
    if (target < 0 || target > 1) {
        return ApplyAnomalyResult::INVALID_PARAM;
    }
    if (!is_valid_abnormal_status_id(anomaly_id)) {
        return ApplyAnomalyResult::INVALID_PARAM;
    }
    if (duration_rounds <= 0) {
        duration_rounds = random_anomaly_duration();
    }

    ElfPet& pet = ctx->getPet(target);

    // [Step 2] 目标存活检查
    if (pet.hp <= 0) {
        return ApplyAnomalyResult::TARGET_DEFEATED;
    }

    // [Step 3] 魂免检查 — 免疫内核 is_immune(ANOMALY) + Mark ID 0 兜底
    // 免疫内核统一回答"目标在当前时点是否免疫该异常"：
    // - 高级魂免 = 全时点覆盖；低级 = 只覆盖部分时点，未覆盖时点这里返回 false。
    // - anomaly_mask 细分：全免 / 魂免(控制位集合) / 天生免疫具体异常。
    if (ctx->is_immune(target, ImmunityType::ANOMALY, ctx->currentState, anomaly_id)
        || has_mark(pet.marks, 0)) {
        return ApplyAnomalyResult::TARGET_IMMUNE;
    }

    // [Step 4] 同种异常 — 回合覆盖
    bool already_has_same = ctx->has_active_abnormal_status(target, anomaly_id);
    if (already_has_same) {
        int current_end = ctx->get_abnormal_status_end_round(target, anomaly_id);
        int current_remaining = current_end - ctx->roundCount;
        if (duration_rounds > current_remaining) {
            ctx->set_abnormal_status_end_round(target, anomaly_id,
                                               ctx->roundCount + duration_rounds);
            emit_anomaly_events(ctx, target, anomaly_id, actor);
            return ApplyAnomalyResult::DURATION_EXTENDED;
        }
        // 新回合数不更长，不覆盖，但也不算失败——异常已经存在
        return ApplyAnomalyResult::SUCCESS;
    }

    // [Step 6] 特殊阻止检查（预留扩展点）
    // 未来在此添加：抗性系统（百分比）/ 装备称号免疫 / 场地阻止 / 保护机制。
    // 建议：每个阻止条件用独立的辅助函数，在此依次调用。

    // [Step 7] 执行施加
    // abnormal_status_end_round[target][anomaly_id] 是异常状态的唯一权威数据源。
    ctx->set_abnormal_status_end_round(target, anomaly_id,
                                       ctx->roundCount + duration_rounds);
    // 成功路径 emit：异常状态实际改变才通知第三方（控场额外发 EVENT_CONTROLLED）
    emit_anomaly_events(ctx, target, anomaly_id, actor);

#ifdef BATTLE_FSM_VERBOSE_DEFAULT
    std::cout << "[apply_anomaly] target=" << target
              << " anomaly=" << abnormal_status_name_cn(anomaly_id)
              << "(" << anomaly_id << ")"
              << " duration=" << duration_rounds
              << " end_round=" << (ctx->roundCount + duration_rounds)
              << std::endl;
#endif

    return ApplyAnomalyResult::SUCCESS;
}

// ----------------------------------------------------------------
// break_round_effects — 断回合原语
//
// 返回"发生了什么"，技能据此分支：
//   - SUCCESS   : 清除了目标回合类效果（成功路径 emit EVENT_BREAK）
//   - NO_EFFECTS: 目标没有可清除的回合类效果（清除失败：无事发生）
//   - IMMUNE    : 目标在当前时点免断（本次断回合无效）
// ----------------------------------------------------------------
BreakResult break_round_effects(BattleContext* ctx, int target) {
    if (target < 0 || target > 1) {
        return BreakResult::NO_EFFECTS;
    }

    // 免断检查：目标在当前时点免疫断回合 → 本次断回合无效。
    // 低级免断只覆盖部分时点，未覆盖时点 is_immune 返回 false，照常可断。
    if (ctx->is_immune(target, ImmunityType::BREAK, ctx->currentState)) {
        return BreakResult::IMMUNE;
    }

    // 对手没有可清除的回合类效果 → 清除失败（无事发生），不 emit。
    if (!ctx->has_round_effects(target)) {
        return BreakResult::NO_EFFECTS;
    }

    // 成功：机械无效化 + 事件（补偿 watcher 在 FSM drain 点投递）
    ctx->invalidate_all_round_effects(target);
    ctx->event_center_.emit(BattleEvent{EventType::EVENT_BREAK, ctx->opponent(target), target});
    return BreakResult::SUCCESS;
}

// ----------------------------------------------------------------
// deal_damage — 伤害原语（统一伤害入口）
//
// 流程：护盾吸收（按优先级）→ 扣血 → emit EVENT_TAKE_DAMAGE。
// 护盾被击破时 emit EVENT_SHIELD_BROKEN（对应描述"护盾消失时XXX"）。
// 所有伤害类机制都应走这里，避免效果函数直接改 hp 绕过管线。
// ----------------------------------------------------------------
void deal_damage(BattleContext* ctx, int target, int amount,
                 DamageKind kind, int actor) {
    if (target < 0 || target > 1 || amount <= 0) {
        return;
    }
    ElfPet& pet = ctx->getPet(target);
    if (pet.hp <= 0) {
        return;  // 目标已死亡
    }

    int effective = amount;
    if (kind == DamageKind::PERCENT) {
        const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
        effective = max_hp > 0 ? max_hp * amount / 100 : 0;
        if (effective <= 0) {
            return;
        }
    }

    // 护盾吸收（按优先级从高到低），记录破盾数
    int broken = 0;
    const int remaining = pet.shield_bank_.absorb(effective, &broken);
    if (broken > 0) {
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_SHIELD_BROKEN, actor, target});
    }
    if (remaining <= 0) {
        return;  // 护盾完全挡下
    }

    // 扣血
    const int hp_before = pet.hp;
    pet.hp -= remaining;
    if (pet.hp < 0) {
        pet.hp = 0;
    }
    const int actual_damage = hp_before - pet.hp;

    // 受到伤害事件（第三方"受到攻击伤害后/受高伤/受低伤"监听），amount = 实际扣血
    ctx->event_center_.emit(BattleEvent{EventType::EVENT_TAKE_DAMAGE, actor, target, actual_damage});
}
