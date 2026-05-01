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
