#ifndef BATTLE_WORKSPACE_H
#define BATTLE_WORKSPACE_H

#include <cstring>
#include <effects/effect.h>
#include <entities/numerical-properties.h>

// Forward declare instead of include to break circular dependency
// class BattleContext;

enum class PreemptiveRight {
    SEER_ROBOT_1,
    SEER_ROBOT_2,
    NONE
};

struct DamageSnapshot {
    int attackerId = -1;
    int defenderId = -1;
    int base = 0;
    int afterAdd = 0;
    int afterMul = 0;
    int final = 0;
    int addPct = 0;
    double mulCoef = 1.0;
    bool isRed = true;
    bool isDirect = false;
    bool isFixed = false;
    bool isTrueDamage = false;
    bool isWhiteNumber = false;
    bool isCrit = false;
};

/**
 * BattleWorkspace - 回合临时数据层
 *
 * 存放本回合内需要用到的中间变量，每回合开始时重置。
 * 所有效果执行都通过 BattleContext.stateEffects 管理，不存在这里。
 */
struct BattleWorkspace {
    // 默认构造即零初始化。reset() 原本只靠 BATTLE_ROUND_START 显式调用，
    // 若在首回合开始前（或未走完整 FSM 时）访问 ws 成员会读到未初始化垃圾
    // （如 dodge_rate 导致命中判定整数溢出）。构造时调用一次 reset() 兜底。
    BattleWorkspace() { reset(); }

    //========== 操作选择 ==========
    int roundChoice[2][2];     // [方数][操作类型, 参数索引]
    int lastActionType[2];     // 上一次操作类型
    int lastActionIndex[2];    // 上一次操作参数

    //========== 先手权 ==========
    PreemptiveRight preemptive_right;
    int preemptive_level[2];  // 先制等级，数值越大优先级越高

    //========== 伤害计算 ==========
    DamageSnapshot pendingDamage;   // 加算减伤后
    DamageSnapshot resolvedDamage;  // 最终伤害

    //========== 行动开始异常白字真伤（不可减免） ==========
    DamageSnapshot action_start_abnormal_damage[2];
    bool action_start_abnormal_damage_pending[2];

    //========== 减伤槽位 ==========
    int damage_reduce_add[2][4];    // 加算减伤百分比(4槽位)
    int damage_reduce_mul[2][4];    // 乘算减伤百分比(4槽位)

    //========== 临时属性修正 ==========
    float dodge_rate[2];           // 闪避率
    float hit_rate_mod[2];         // 命中率修正倍率
    float crit_rate_mod[2];        // 暴击率修正
    float damage_add_pct[2];       // 伤害加成百分比
    int   damage_add_flat[2];      // 伤害加成固定值
    numerical_properties battle_attrs[2];        // 本回合视角的数值属性，受到效果修正但不改变真实属性
    int  view_levels[2][6];         // 本回合能力提升/下降等级，受到视强为弱、示弱为强效果修正，但是不会改变真实能力上升/下降等级
    int view_elementalAttributes[2][2];

    //========== 回合内状态 ==========
    bool has_attacked[2];           // 本回合是否已攻击
    bool skill_used[2];             // 本回合技能使用标记
    int extra_action_count[2];      // 额外行动次数
    SkillExecResult skill_exec_result[2];
    SkillResolutionFlags skill_resolution_flags[2];
    bool skill_resolution_ready[2];
    int skill_pp_cost_multiplier[2];
    bool skill_pp_cost_consumed[2];

    //========== 攻击穿透凭证（本次攻击临时物化，每回合 reset 自动清）==========
    // "下N次攻击无视免疫/伤害限制"的物化槽：攻击时由 materialize_attack_credential
    // 现算合并（技能自带 697/699 + 次数授予），query_usage 门判定读它；回合结束 reset 清除。
    struct AttackCredential {
        bool valid = false;                  // 本次攻击是否携带凭证（穿透或强制）
        bool ignore_attack_immunity = false; // 穿"攻击免疫/狮盔"（官方 699）
        bool ignore_damage_limit = false;    // 穿"伤害限制"（官方 697，本轮只存不消费）
        bool force_execute = false;          // 强制执行：必定命中 + 无视命中效果失效（官方 2380/2474/魂印2260）
        int  level = 0;                      // 穿透等级：0=无, 1=可穿盔（等级比较留 SkillInvalidCenter）
    };
    AttackCredential attack_credential[2];   // 按攻击方索引

    //========== 技能威力视图层 ==========
    // 本回合视角的技能威力：攻击时由 resolve_skill_execution 物化 skill.power，
    // 效果（如无相谛 179"属性相同威力提升"、未来黯玉咒言随机/累积威力）在
    // SKILL_EFFECT 时点修改它，ATTACK_DAMAGE 阶段 calculateDamage 从 ws 读最终值。
    // 0 = 未物化（calculateDamage 回退 skill.power）。
    int skill_power_view[2];

    //========== 命中效果失效标记（③层，白板模式） ==========
    // execute 判定③层 kFullNull 时置位；ATTACK_DAMAGE 阶段据此把伤害归 0（白板）。
    // 每回合 reset 自动清；kEffectsOnly（保留伤害）不置位。
    bool hit_invalid_zero_damage[2]{};

    //========== 缓存计算值 ==========
    int cached_speed[2];            // 考虑异常后的速度
    int cached_crit_damage[2];      // 暴击伤害倍率(默认200)

    //========== 重置 ==========
    void reset() {
        memset(this, 0, sizeof(BattleWorkspace));
        // consider better reset strategy if more fields are added, to avoid accidentally forgetting to reset new fields

        // 恢复默认倍率
        for (int i = 0; i < 2; i++) {
            hit_rate_mod[i] = 1.0f;
            crit_rate_mod[i] = 1.0f;
            cached_crit_damage[i] = 200;  // 默认暴击2倍
            skill_exec_result[i] = SkillExecResult::SKILL_INVALID;
            skill_resolution_flags[i] = SkillResolutionFlags{false, false};
            skill_pp_cost_multiplier[i] = 1;
        }
    }
    int getTempAbilityValue(int owner, NumericalPropertyIndex i) const {
        if (owner < 0 || owner >= 2) {
            throw std::out_of_range("Owner index out of range");
        }
        auto &level = view_levels[owner];
        int index = static_cast<int>(i);
        if (index < 0 || index >= 6) {
            throw std::out_of_range("Index out of range");
        }
        if (level[index] < -6 || level[index] > 6) {
            throw std::out_of_range("Level out of range");
        }
        if (level[index] >= 0) {
            return static_cast<int>(battle_attrs[owner][i] * ((level[index] + 2) / 2.0));
        }
        if (index == 5 && level[index] < 0) {
            // return Cm * 100 (命中等级为负时的特殊处理)
            switch (level[index]) {
                case -1: return 85;
                case -2: return 70;
                case -3: return 55;
                case -4: return 45;
                case -5: return 35;
                case -6: return 25;
                // no need for default since level range is already checked
            }
        }
        return static_cast<int>(battle_attrs[owner][i] * (2.0 / (2 + level[index])));
    }
};

#endif // BATTLE_WORKSPACE_H
