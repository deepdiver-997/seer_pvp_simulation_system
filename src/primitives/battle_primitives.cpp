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
#include <effects/continuousEffect.h>
#include <entities/elf-pet.h>
#include <entities/mark.h>
#include <fsm/battleContext.h>
#include <fsm/state.h>

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
// 转化异常（单跳）：入→出。免疫异常(21)也可被转化——"让对手抗性抵抗挂上免疫异常图标，
// 再转化掉"的绕过魂免+抗性 exploit 路径。
static int resolve_anomaly_conversion(BattleContext* ctx, int target, int incoming) {
    const auto& conv = ctx->anomaly_conversion[target];
    const auto it = conv.find(incoming);
    return it != conv.end() ? it->second : incoming;
}

// 内部实现。reflect_depth = 反弹深度（0=原生施加；1=反弹回来：不再反弹、不过抗性）。
static ApplyAnomalyResult apply_anomaly_impl(BattleContext* ctx,
                                             int target,
                                             int anomaly_id,
                                             int duration_rounds,
                                             int actor,
                                             int reflect_depth) {
    // [1] 参数校验
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

    // [2] 目标存活检查
    if (pet.hp <= 0) {
        return ApplyAnomalyResult::TARGET_DEFEATED;
    }

    // 弹控：目标免疫时把异常反弹给施放方。最多反弹 1 次（depth==1 不再弹）——防双方弹控打乒乓球。
    const auto reflect = [&]() -> ApplyAnomalyResult {
        if (reflect_depth == 0 && ctx->reflect_anomaly[target]
            && actor >= 0 && actor != target) {
            apply_anomaly_impl(ctx, actor, anomaly_id, duration_rounds,
                               /*actor=*/target, /*reflect_depth=*/1);
            return ApplyAnomalyResult::REFLECTED;
        }
        return ApplyAnomalyResult::TARGET_IMMUNE;
    };

    // [3] 次免/回合类免疫（soul=false）—— 官方优先级：先于抗性判定挡下。
    if (ctx->is_immune_effect(target, ImmunityType::ANOMALY, ctx->currentState, anomaly_id)) {
        return reflect();
    }

    // [4] 异常抗性 roll（弹回的异常不过抗性——depth>0 跳过；官方必修3/选修7）。
    // 抗性成功：直写附加"免疫异常"异常(21, 2回合)——**击穿魂免**（不走免疫检查）；
    // 先走转化（攻击方可预置 conversion[target][21]=Y 把抵抗结果直接转成 Y）。
    if (reflect_depth == 0 && pet.resistance.isResistantTo(anomaly_id)) {
        const int resolved = resolve_anomaly_conversion(
            ctx, target, static_cast<int>(AbnormalStatusId::AbnormalImmunity));
        ctx->set_abnormal_status_end_round(target, resolved, ctx->roundCount + 2);
        emit_anomaly_events(ctx, target, resolved, actor);
        return ApplyAnomalyResult::RESISTED_BY_RESISTANCE;
    }

    // [5] 魂免（soul=true）—— 抗性判定失败后才查（官方优先级）。
    if (ctx->is_immune_soul(target, ImmunityType::ANOMALY, ctx->currentState, anomaly_id)
        || has_mark(pet.marks, 0)) {
        return reflect();
    }

    // [6] 转化（单跳）：入→出，直接施加（绕过抗性/免疫的转换路径）。
    const int resolved = resolve_anomaly_conversion(ctx, target, anomaly_id);
    const bool converted = (resolved != anomaly_id);

    // [7] 同种异常 — 回合覆盖
    bool already_has_same = ctx->has_active_abnormal_status(target, resolved);
    if (already_has_same) {
        int current_end = ctx->get_abnormal_status_end_round(target, resolved);
        int current_remaining = current_end - ctx->roundCount;
        if (duration_rounds > current_remaining) {
            ctx->set_abnormal_status_end_round(target, resolved,
                                               ctx->roundCount + duration_rounds);
            emit_anomaly_events(ctx, target, resolved, actor);
            return converted ? ApplyAnomalyResult::CONVERTED
                             : ApplyAnomalyResult::DURATION_EXTENDED;
        }
        // 新回合数不更长，不覆盖，但也不算失败——异常已经存在
        return converted ? ApplyAnomalyResult::CONVERTED : ApplyAnomalyResult::SUCCESS;
    }

    // [8] 执行施加
    // abnormal_status_end_round[target][anomaly_id] 是异常状态的唯一权威数据源。
    ctx->set_abnormal_status_end_round(target, resolved,
                                       ctx->roundCount + duration_rounds);
    // 成功路径 emit：异常状态实际改变才通知第三方（控场额外发 EVENT_CONTROLLED）
    emit_anomaly_events(ctx, target, resolved, actor);

#ifdef BATTLE_FSM_VERBOSE_DEFAULT
    std::cout << "[apply_anomaly] target=" << target
              << " anomaly=" << abnormal_status_name_cn(resolved)
              << "(" << resolved << ")"
              << " duration=" << duration_rounds
              << " end_round=" << (ctx->roundCount + duration_rounds)
              << (converted ? " [converted]" : "")
              << std::endl;
#endif

    return converted ? ApplyAnomalyResult::CONVERTED : ApplyAnomalyResult::SUCCESS;
}

// 公开入口：原生施加（reflect_depth=0）。
ApplyAnomalyResult apply_anomaly(BattleContext* ctx,
                                 int target,
                                 int anomaly_id,
                                 int duration_rounds,
                                 int actor) {
    return apply_anomaly_impl(ctx, target, anomaly_id, duration_rounds, actor,
                              /*reflect_depth=*/0);
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
    // 回合类盔/威/封属也算回合类效果（可被断清除）。
    if (!ctx->has_round_effects(target) && !ctx->skill_invalid_center_.has_round_type(target)) {
        return BreakResult::NO_EFFECTS;
    }

    // 成功：机械无效化 + 事件（补偿 watcher 在 FSM drain 点投递）
    ctx->invalidate_all_round_effects(target);
    // 断回合清回合类盔/威/封属（次数类保留）
    ctx->skill_invalid_center_.clear_round_type(target);
    ctx->event_center_.emit(BattleEvent{EventType::EVENT_BREAK, ctx->opponent(target), target});
    return BreakResult::SUCCESS;
}

// ----------------------------------------------------------------
// deal_damage — 伤害原语（统一伤害入口）
//
// 流程：粉伤抗性（固定/百分比）→ 护盾/护罩吸收 → 扣血 → emit EVENT_TAKE_DAMAGE。
// 护盾只响应红伤(NORMAL)、护罩只响应粉伤(FIXED/PERCENT)、真实伤害直通（官方护盾/护罩分离）。
// 被击破时 emit EVENT_SHIELD_BROKEN（对应描述"护盾消失时XXX"，护罩破罩同事件）。
// 攻击方可设 ws.ignore_shield 使本次攻击无视护盾/护罩响应。
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
    // 粉转真时按原始量（PERCENT 已换算成具体数值）转，故保留 pre_resist。
    int pre_resist = amount;
    if (kind == DamageKind::PERCENT) {
        const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
        effective = max_hp > 0 ? max_hp * amount / 100 : 0;
        if (effective <= 0) {
            return;
        }
        pre_resist = effective;
    }

    // 粉伤抗性层（固定/百分比伤害）：免疫粉伤 → 对应来源抗性% → 减粉% 逐级削减。
    // 伤害抗性按来源分型：FIXED 走固定抗性、PERCENT 走百分比抗性（官方：暴击/固定/百分比）。
    // ⚠️ 读的是 ws **有效视图**而非 pet 本体——临时 buff 可修改抗性（混元天尊死亡 buff
    //    把己方精灵抗性视为 100%）。视图基线由 sync_damage_resist_view 在回合开始/换宠重基。
    // 被挡下（<=0）且粉转真 → 改以真实伤害结算（直通护盾/护罩、穿抗性/免疫）。TRUE 绕过此层。
    if (kind == DamageKind::FIXED || kind == DamageKind::PERCENT) {
        const int resist_pct =
            kind == DamageKind::FIXED ? ctx->ws.eff_fixed_resist_pct[target]
                                      : ctx->ws.eff_percent_resist_pct[target];
        if (ctx->pink_immune[target]) {
            effective = 0;
        } else {
            effective -= effective * resist_pct / 100;
            effective -= effective * ctx->pink_reduce_pct[target] / 100;
        }
        if (effective <= 0) {
            if (ctx->pink_to_true[target]) {
                deal_damage(ctx, target, pre_resist, DamageKind::TRUE, actor);
            }
            return;  // 被免粉挡下：目标体力不变（免粉补偿一类在效果侧比较 HP）
        }
    }

    // 护盾/护罩吸收：护盾只响应红伤(NORMAL)，护罩只响应粉伤(FIXED/PERCENT)，真实伤害直通。
    // 攻击方可设 ws.ignore_shield[attacker] 使本次攻击无视护盾/护罩响应（如无极圣武魂印）。
    const bool ignore_bank = (actor >= 0 && actor <= 1 && ctx->ws.ignore_shield[actor]);
    int broken = 0;
    int remaining = effective;
    if (!ignore_bank) {
        if (kind == DamageKind::NORMAL) {
            remaining = pet.shield_bank_.absorb(effective, &broken);
        } else if (kind == DamageKind::FIXED || kind == DamageKind::PERCENT) {
            remaining = pet.hood_bank_.absorb(effective, &broken);
        }
        // DamageKind::TRUE：护盾/护罩均不响应，直通
    }
    if (broken > 0) {
        ctx->event_center_.emit(BattleEvent{EventType::EVENT_SHIELD_BROKEN, actor, target});
    }
    if (remaining <= 0) {
        return;  // 护盾/护罩完全挡下
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

void seal_skill(BattleContext* ctx, int target, int effect_id, bool attribute, bool attack,
                int count, int duration_rounds, bool penetrable, int source_slot,
                InvalidBinding binding) {
    if (!ctx || target < 0 || target > 1 || count <= 0) {
        return;
    }
    if (!attribute && !attack) {
        return;
    }

    SkillArmor armor;
    armor.kind = (attribute && attack) ? SkillArmor::Kind::SEAL_ALL
               : (attack ? SkillArmor::Kind::SEAL_ATTACK : SkillArmor::Kind::SEAL_ATTRIBUTE);
    armor.penetrable = penetrable;
    armor.source_effect_id = effect_id;
    // 回合型 / 次数型二选一（文档 §5.1）：有 duration 走回合型（响应不消耗，靠减扣点/断回合结束），
    // 否则走次数型（响应即减，减到 0 注销）。
    if (duration_rounds > 0) {
        armor.remaining_rounds = duration_rounds;
    } else {
        armor.remaining_counts = count;
    }

    // 同 (owner, source_slot, source_effect_id, kind) → 覆盖刷新（不累加），见中心头注释。
    ctx->skill_invalid_center_.register_armor(target, source_slot, armor, binding);
}

void hit_effect_invalid(BattleContext* ctx, int target, HitInvalidMode mode,
                        int count, int source_id) {
    if (!ctx || target < 0 || target > 1 || count <= 0) {
        return;
    }
    ctx->hit_effect_invalids[target].push_back(
        BattleContext::HitEffectInvalid{source_id, mode, count});
}

StatChangeResult stat_change(BattleContext* ctx, int target, int stat, int delta) {
    if (!ctx || target < 0 || target > 1 || stat < 0 || stat >= 6) {
        return StatChangeResult::INVALID_PARAM;
    }
    ElfPet& pet = ctx->getPet(target);
    int& level = pet.levels[stat];
    const int new_level = level + delta;
    if (new_level > 6 || new_level < -6) {
        return StatChangeResult::AT_CAP;  // 到上限/下限，不变更
    }
    level = new_level;
    return StatChangeResult::SUCCESS;
}

// 内部：按指定量恢复，返回实际恢复量（封回血检查 + 恢复效果修正 + 记录 last_heal_amount）。
static int heal_impl(BattleContext* ctx, int target, int heal_amount) {
    ElfPet& pet = ctx->getPet(target);
    if (heal_amount <= 0) {
        ctx->ws.last_heal_amount[target] = 0;
        return 0;
    }
    // 封回血：本时点覆盖内封锁体力回复（位覆盖仿魂免——coverage 全置位=闭环恒封；
    // 只含部分时点=低级，未覆盖时点的恢复有效）。
    if (ctx->is_immune(target, ImmunityType::HEAL_BLOCK, ctx->currentState)) {
        ctx->ws.last_heal_amount[target] = 0;
        return 0;
    }
    // 恢复效果修正%（正=提升，负=降低；封回血=-100 等价归零）。
    heal_amount = heal_amount * (100 + ctx->heal_mod_pct[target]) / 100;
    if (heal_amount < 0) {
        heal_amount = 0;
    }
    const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
    const int hp_before = pet.hp;
    pet.hp = std::min(max_hp, pet.hp + heal_amount);
    const int actual = pet.hp - hp_before;
    ctx->ws.last_heal_amount[target] = actual;
    return actual;
}

HealResult heal(BattleContext* ctx, int target, int fraction_denom) {
    if (!ctx || target < 0 || target > 1) {
        return HealResult::INVALID_PARAM;
    }
    ElfPet& pet = ctx->getPet(target);
    const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
    int heal_amount = 0;
    if (fraction_denom > 0) {
        heal_amount = max_hp > 0 ? max_hp / fraction_denom : 0;
    } else {
        heal_amount = max_hp;  // 恢复全部
    }
    heal_impl(ctx, target, heal_amount);
    return HealResult::SUCCESS;
}

HealResult heal_amount(BattleContext* ctx, int target, int amount) {
    if (!ctx || target < 0 || target > 1 || amount < 0) {
        return HealResult::INVALID_PARAM;
    }
    heal_impl(ctx, target, amount);
    return HealResult::SUCCESS;
}

int clear_stat_boosts(BattleContext* ctx, int target) {
    if (!ctx || target < 0 || target > 1) {
        return 0;
    }
    ElfPet& pet = ctx->getPet(target);
    int cleared = 0;
    for (auto& lv : pet.levels) {  // 只清提升（正等级），不动弱化/负等级
        if (lv > 0) {
            lv = 0;
            ++cleared;
        }
    }
    return cleared;  // 0 = 目标本无提升（消强未成功）
}

namespace {

// 必先授予效果：在先手权时点把 ws.guaranteed_first[owner] 置为 tier。
EffectResult effect_set_guaranteed_first(BattleContext* ctx, const EffectArgs& args) {
    if (!ctx || !args.int_args || args.int_count < 3) {
        return EffectResult::kOk;
    }
    const int owner = args.int_args[0];
    if (owner < 0 || owner > 1) {
        return EffectResult::kOk;
    }
    ctx->ws.guaranteed_first[owner] = args.int_args[2];  // tier
    return EffectResult::kOk;
}

}  // namespace

void grant_guaranteed_first(BattleContext* ctx, int owner, int tier) {
    if (!ctx || owner < 0 || owner > 1 || tier <= 0) {
        return;
    }
    // 注册 once 回合类效果到 BATTLE_FIRST_MOVE_RIGHT：下一次先手权时点触发一次 → "下一回合必先"。
    // duration=2 保证活到下一回合(N+1)。先手权处每回合 memset → 只在下一回合生效；断回合可移除它。
    Effect e;
    e.id = 999201;  // 必先授予
    e.logic = &effect_set_guaranteed_first;
    e.args = EffectArgs(std::vector<int>{owner, owner ^ 1, tier});
    auto ce = std::make_unique<ContinuousEffect>(e, State::BATTLE_FIRST_MOVE_RIGHT, owner,
                                                 /*duration_rounds=*/2, ctx->roundCount);
    ce->once_ = true;
    ctx->register_skill_effect(State::BATTLE_FIRST_MOVE_RIGHT, owner, std::move(ce));
}

FixedDamageResult fixed_damage(BattleContext* ctx, int target, int amount) {
    if (!ctx || target < 0 || target > 1 || amount < 0) {
        return FixedDamageResult::INVALID_PARAM;
    }
    ElfPet& pet = ctx->getPet(target);
    if (pet.hp <= 0) {
        return FixedDamageResult::TARGET_DEFEATED;
    }
    deal_damage(ctx, target, amount, DamageKind::FIXED, -1);
    return FixedDamageResult::SUCCESS;
}

PpReduceResult pp_reduce(BattleContext* ctx, int target, int amount) {
    if (!ctx || target < 0 || target > 1 || amount <= 0) {
        return PpReduceResult::INVALID_PARAM;
    }
    ElfPet& pet = ctx->getPet(target);
    bool changed = false;
    for (Skills& skill : pet.skills) {
        if (skill.pp == -1) {
            continue;  // 无限 PP 不参与
        }
        const int before = skill.pp;
        skill.pp = std::max(0, skill.pp - amount);
        if (skill.pp != before) {
            changed = true;
        }
    }
    return changed ? PpReduceResult::SUCCESS : PpReduceResult::INVALID_PARAM;
}

RemoveRoundEffectsResult remove_round_effects(BattleContext* ctx, int target) {
    if (!ctx || target < 0 || target > 1) {
        return RemoveRoundEffectsResult::INVALID_PARAM;
    }
    switch (break_round_effects(ctx, target)) {
        case BreakResult::SUCCESS:    return RemoveRoundEffectsResult::SUCCESS;
        case BreakResult::NO_EFFECTS: return RemoveRoundEffectsResult::NONE;
        case BreakResult::IMMUNE:     return RemoveRoundEffectsResult::IMMUNE;
    }
    return RemoveRoundEffectsResult::INVALID_PARAM;
}

DrainHpResult drain_hp(BattleContext* ctx, int actor, int target, int fraction_denom) {
    if (!ctx || actor < 0 || actor > 1 || target < 0 || target > 1
        || fraction_denom <= 0 || actor == target) {
        return DrainHpResult::INVALID_PARAM;
    }
    ElfPet& defender = ctx->getPet(target);
    if (defender.hp <= 0) {
        return DrainHpResult::TARGET_DEFEATED;
    }
    const int max_hp = std::max(1, defender.numericalProperties[NumericalPropertyIndex::HP]);
    const int amount = std::max(1, max_hp / fraction_denom);
    deal_damage(ctx, target, amount, DamageKind::FIXED, actor);
    // 自身恢复等量——走 heal_impl（封回血/恢复效果修正生效；被封则吸不到血）
    heal_impl(ctx, actor, amount);
    return DrainHpResult::SUCCESS;
}

// drain_hp_amount — 吸取固定伤害（固定量版；恢复同样走 heal_impl）。
DrainHpResult drain_hp_amount(BattleContext* ctx, int actor, int target, int amount) {
    if (!ctx || actor < 0 || actor > 1 || target < 0 || target > 1
        || amount <= 0 || actor == target) {
        return DrainHpResult::INVALID_PARAM;
    }
    ElfPet& defender = ctx->getPet(target);
    if (defender.hp <= 0) {
        return DrainHpResult::TARGET_DEFEATED;
    }
    deal_damage(ctx, target, amount, DamageKind::FIXED, actor);
    heal_impl(ctx, actor, amount);
    return DrainHpResult::SUCCESS;
}

KillResult kill(BattleContext* ctx, int target) {
    if (!ctx || target < 0 || target > 1) {
        return KillResult::INVALID_PARAM;
    }
    ElfPet& pet = ctx->getPet(target);
    if (pet.hp <= 0) {
        return KillResult::ALREADY_DEFEATED;
    }
    pet.hp = 0;
    return KillResult::SUCCESS;
}
