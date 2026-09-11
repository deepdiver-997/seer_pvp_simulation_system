#ifndef BATTLE_PRIMITIVES_H
#define BATTLE_PRIMITIVES_H

#include <effects/effect.h>
#include <effects/skill_invalid_center.h>

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
    RESISTED_BY_RESISTANCE,// 异常抗性抵抗成功：直写附加"免疫异常"异常(21, 2回合)，击穿魂免
    REFLECTED,             // 弹控：目标免疫并将异常反弹给施放方（最多反弹 1 次）
    CONVERTED,             // 转化异常：进入异常时转为另一指定异常
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
        || r == ApplyAnomalyResult::DURATION_EXTENDED
        || r == ApplyAnomalyResult::CONVERTED;
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
 * seal_skill - 给目标方挂技能拦截效果（封属性/封攻击）。
 *
 * "对手下N次属性技能失效"（次数型）/"对手3回合内属性技能无效"（回合型）类效果。
 * 挂到**被拦截方**的桶（skill_seals[target]）：施放方切换不影响；被拦截方切换清空。
 * 按 effect_id 覆盖去重（同效果重复挂 → 刷新次数/回合，不叠加）。
 * 拦截发生时技能按 SKILL_INVALID 处理。回合型每回合递减、可被断回合清除。
 *
 * @param target         被拦截方 (0/1)
 * @param effect_id      来源效果 id（覆盖去重 key）
 * @param attribute      是否封属性技能
 * @param attack         是否封攻击技能
 * @param count          次数型拦截次数（>0）
 * @param duration_rounds 回合型持续回合（>0 走回合型；0=次数型）
 * @param penetrable     可否被"无视攻击免疫"穿透（默认 true=可穿盔；false=条件盔/龙威）
 *
 * 注：拦截**没有**"免断"属性——免断是 owner 级的 `ImmunityType::BREAK`（免疫内核），
 *     由 `break_round_effects` 在入口统一查询，见技能判定流程与无效效果体系.md §二。
 */
void seal_skill(BattleContext* ctx, int target, int effect_id, bool attribute, bool attack,
                int count, int duration_rounds = 0, bool penetrable = true,
                int source_slot = -1, InvalidBinding binding = InvalidBinding::SELF);

/**
 * hit_effect_invalid - 给目标方挂"命中效果失效"（③层，次数类）。
 *
 * 目标方后续技能命中时，命中效果按 mode 处理（效果不注册）：
 *   - kEffectsOnly：保留伤害（效果失效但伤害照常）
 *   - kFullNull：白板（效果失效 + 伤害归 0）
 * 强制执行（force_execute）可绕过③层。
 *
 * @param target     被失效方 (0/1)
 * @param mode       失效模式（kEffectsOnly / kFullNull）
 * @param count      失效次数（>0）
 * @param source_id  施放方（未知传 -1）
 */
void hit_effect_invalid(BattleContext* ctx, int target, HitInvalidMode mode,
                        int count, int source_id = -1);

// ----------------------------------------------------------------
// 能力等级变化
// ----------------------------------------------------------------
enum class StatChangeResult {
    SUCCESS,       // 成功变更
    AT_CAP,        // 到上限/下限（等级越界，未变更）
    INVALID_PARAM, // 无效参数（target/stat 非法）
};

/**
 * stat_change - 真实能力等级变更（pet.levels，持久；视层留 ws）。
 * 返回"发生了什么"，效果程序据此分支（如"提升失败则附加固定伤害"）。
 *
 * @param target 目标方 (0/1)
 * @param stat   能力下标（0=攻击 1=特攻 2=防御 3=特防 4=速度 5=体力）
 * @param delta  变化量（正=提升，负=下降；越界则 AT_CAP 不变更）
 */
StatChangeResult stat_change(BattleContext* ctx, int target, int stat, int delta);

// ----------------------------------------------------------------
// 恢复体力 / 固定伤害
// ----------------------------------------------------------------
enum class HealResult {
    SUCCESS,       // 成功恢复
    INVALID_PARAM, // 无效参数
};

enum class FixedDamageResult {
    SUCCESS,         // 造成固定伤害
    TARGET_DEFEATED, // 目标已死亡
    INVALID_PARAM,   // 无效参数
};

/**
 * heal - 恢复目标最大体力的一定比例。
 * fraction_denom > 0：恢复 max_hp / fraction_denom；<= 0：恢复全部。
 */
HealResult heal(BattleContext* ctx, int target, int fraction_denom);

/**
 * heal_amount - 按具体数值恢复（吃封回血 + 恢复效果修正% + 记 last_heal）。
 * 用于"恢复已损失体力的 1/2"这类非整比例恢复（amount 由调用方算好）。
 */
HealResult heal_amount(BattleContext* ctx, int target, int amount);

/**
 * clear_stat_boosts - 消除目标方正等级上的能力提升（"消除双方能力提升状态"）。
 * 只清提升（等级 > 0 → 0），不动弱化/负等级。
 * 返回清掉的提升个数（0 = 目标本无提升 = "消强未成功"，调用方可据此决定后续分支，如"消强成功→必先"）。
 */
int clear_stat_boosts(BattleContext* ctx, int target);

/**
 * grant_guaranteed_first - 授予"下一回合必定先出手"（必先，分等级）。
 * 注册一个 once 回合类效果到 BATTLE_FIRST_MOVE_RIGHT：下一次先手权时点置
 * ws.guaranteed_first[owner]=tier（tier 越高越先）。是回合类效果 → 可被断回合移除；
 * once + 先手权处 memset → 只在下一回合生效一次。
 */
void grant_guaranteed_first(BattleContext* ctx, int owner, int tier);

/**
 * fixed_damage - 固定伤害（复用 deal_damage，吃护盾/事件）。
 */
FixedDamageResult fixed_damage(BattleContext* ctx, int target, int amount);

// ----------------------------------------------------------------
// 第二刀新原语（组合语法：无相谛 5 类条件模板）
// ----------------------------------------------------------------
enum class PpReduceResult {
    SUCCESS,        // 已降低（至少一个技能 PP 变化）
    INVALID_PARAM,  // 无效参数
};

/**
 * pp_reduce - 降低目标方所有技能的 PP。
 * 无相谛 700"先出手时降低对手所有PP"。target 方每个技能 pp -= amount（clamp ≥0）。
 */
PpReduceResult pp_reduce(BattleContext* ctx, int target, int amount);

enum class RemoveRoundEffectsResult {
    SUCCESS,     // 清除了目标回合类效果
    NONE,        // 目标没有可清除的回合类效果
    IMMUNE,      // 目标免断，本次无效
    INVALID_PARAM,
};

/**
 * remove_round_effects - 消除目标回合类效果（复用 break_round_effects，吃免断 + EVENT_BREAK）。
 * 无相谛 1083"若后出手则消除对手回合类效果"。
 */
RemoveRoundEffectsResult remove_round_effects(BattleContext* ctx, int target);

enum class DrainHpResult {
    SUCCESS,        // 吸取成功（造成固定伤害 + 自身恢复等量）
    TARGET_DEFEATED,// 目标已死亡
    INVALID_PARAM,
};

/**
 * drain_hp - 吸取体力：目标掉 max_hp/denom 固定伤害，actor 恢复等量（clamp 到 max_hp）。
 * 无相谛 1257"对手不处于异常状态则吸取对手最大体力的1/{n}"。
 */
DrainHpResult drain_hp(BattleContext* ctx, int actor, int target, int fraction_denom);

/**
 * drain_hp_amount - 吸取固定伤害：目标掉 amount 固定伤害，actor 恢复等量。
 * 恢复走 heal 原语（封回血/恢复效果修正生效；被封则吸不到血）。
 */
DrainHpResult drain_hp_amount(BattleContext* ctx, int actor, int target, int amount);

enum class KillResult {
    SUCCESS,        // 秒杀（目标体力归 0）
    ALREADY_DEFEATED,
    INVALID_PARAM,
};

/**
 * kill - 秒杀：目标体力直接归 0。
 * 无相谛 456"若对手体力不足{n}则直接秒杀"。
 */
KillResult kill(BattleContext* ctx, int target);

#endif // BATTLE_PRIMITIVES_H
