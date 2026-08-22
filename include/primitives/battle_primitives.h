#ifndef BATTLE_PRIMITIVES_H
#define BATTLE_PRIMITIVES_H

class BattleContext;

// ================================================================
// 原语层 — 技能/魂印效果程序调用的原子动作
//
// 原语是闭集：增长慢、每个值得审。原语用"结果枚举"表达发生了什么，
// 技能据此分支后续行为（如"清除成功则下回合先手+1"）。
//
// 返回值约定：
//   - 状态变更类原语 → 结果枚举（区分不同失败/成功变体）
//   - 谓词/查询 → bool（is_immune、has_round_effects 留在内核）
//   - 发射即忘 → void（emit、grant_immunity）
// ================================================================

// ----------------------------------------------------------------
// 施加异常
// ----------------------------------------------------------------
enum class ApplyAnomalyResult {
    SUCCESS,               // 成功施加
    TARGET_IMMUNE,         // 目标免疫异常
    BLOCKED_BY_EFFECT,     // 被其他效果阻止（装备/场地/特定保护）
    TARGET_DEFEATED,       // 目标已死亡
    INVALID_PARAM,         // 无效参数（anomaly_id 非法、target 非法）
    REPLACED_EXISTING,     // 替换了已有的控场类异常
    DURATION_EXTENDED,     // 同种异常已存在，延长了回合数
};

/**
 * 生成异常状态的随机持续回合数。
 * 绝大多数异常状态持续 2~3 回合（具体区间待确认）。
 */
inline int random_anomaly_duration() {
    return 2;
}

/**
 * apply_anomaly - 施加异常的原子动作。
 * 全系统唯一施加入口，返回"发生了什么"。
 *
 * 成功（异常状态实际改变）时 emit EVENT_ANOMALY_APPLIED；
 * 若是控场类异常，额外 emit EVENT_CONTROLLED（第三方 watcher 监听）。
 *
 * @param actor 施放方（0/1），效果程序调用时传效果所属方；未知传 -1
 */
ApplyAnomalyResult apply_anomaly(BattleContext* ctx,
                                 int target,
                                 int anomaly_id,
                                 int duration_rounds = -1,
                                 int actor = -1);

/** 便捷函数：尝试施加异常，成功返回 true。 */
inline bool try_apply_anomaly(BattleContext* ctx,
                              int target,
                              int anomaly_id,
                              int duration_rounds = -1,
                              int actor = -1) {
    ApplyAnomalyResult r = apply_anomaly(ctx, target, anomaly_id, duration_rounds, actor);
    return r == ApplyAnomalyResult::SUCCESS
        || r == ApplyAnomalyResult::REPLACED_EXISTING
        || r == ApplyAnomalyResult::DURATION_EXTENDED;
}

// ----------------------------------------------------------------
// 断回合
// ----------------------------------------------------------------
enum class BreakResult {
    SUCCESS,      // 清除了对手回合类效果（成功路径 emit EVENT_BREAK）
    NO_EFFECTS,   // 对手没有可清除的回合类效果
    IMMUNE,       // 对手免断，本次断回合无效
};

/**
 * break_round_effects - 断回合原语：清除目标回合类效果，返回"发生了什么"。
 * 技能可按结果分支："清除成功则XXX"（SUCCESS）、"对手免疫则XXX"（IMMUNE）。
 */
BreakResult break_round_effects(BattleContext* ctx, int target);

// ----------------------------------------------------------------
// 伤害
// ----------------------------------------------------------------
enum class DamageKind {
    NORMAL,   // 普通攻击伤害（吃护盾）
    FIXED,    // 固定伤害（吃护盾）
    PERCENT,  // 百分比伤害（占目标最大体力百分比，吃护盾）
    TRUE,     // 真实伤害（吃护盾）
};

/**
 * deal_damage - 伤害原语：统一伤害入口。
 *
 * 流程：护盾吸收（按优先级）→ 扣血 → emit EVENT_TAKE_DAMAGE。
 * 护盾被击破时 emit EVENT_SHIELD_BROKEN。
 * 所有伤害类机制（攻击管线、效果、固定/百分比伤害）都应走这里，
 * 避免效果函数直接改 hp 绕过管线。
 *
 * @param target 承受方 (0/1)
 * @param amount 伤害量；PERCENT 时为占最大体力的百分比
 * @param kind   伤害类型
 * @param actor  施放方（未知传 -1）
 */
void deal_damage(BattleContext* ctx, int target, int amount,
                 DamageKind kind = DamageKind::NORMAL, int actor = -1);

// ----------------------------------------------------------------
// 技能拦截（封属性/封攻击）
// ----------------------------------------------------------------

/**
 * seal_skill - 给目标方挂技能拦截效果（次数类）。
 *
 * "对手下1次属性技能失效"类效果用：目标方后续使用对应类别技能时被拦截（次数-1），
 * 拦截发生时技能按 SKILL_INVALID 处理。回合类拦截（被控）不归这里，走异常系统。
 *
 * @param target     被拦截方 (0/1)
 * @param attribute  是否封属性技能
 * @param attack     是否封攻击技能
 * @param count      拦截次数（>0）
 * @param source_id  施放方（未知传 -1）
 * @param penetrable 可否被"无视攻击免疫"穿透（默认 true=可穿盔；false=条件盔/龙威）
 */
void seal_skill(BattleContext* ctx, int target, bool attribute, bool attack,
                int count, int source_id = -1, bool penetrable = true);

#endif // BATTLE_PRIMITIVES_H
