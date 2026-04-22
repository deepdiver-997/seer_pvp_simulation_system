#ifndef BATTLE_WORKSPACE_H
#define BATTLE_WORKSPACE_H

#include <cstring>
#include <effects/effect.h>

// Forward declare instead of include to break circular dependency
class BattleContext;

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

    //========== 伤害计算 ==========
    DamageSnapshot pendingDamage;   // 加算减伤后
    DamageSnapshot resolvedDamage;  // 最终伤害

    //========== 减伤槽位 ==========
    int damage_reduce_add[2][4];    // 加算减伤百分比(4槽位)
    int damage_reduce_mul[2][4];    // 乘算减伤百分比(4槽位)

    //========== 临时属性修正 ==========
    float dodge_rate[2];           // 闪避率
    float hit_rate_mod[2];         // 命中率修正倍率
    float crit_rate_mod[2];        // 暴击率修正
    float damage_add_pct[2];       // 伤害加成百分比
    int   damage_add_flat[2];      // 伤害加成固定值

    //========== 回合内状态 ==========
    bool has_attacked[2];           // 本回合是否已攻击
    bool skill_used[2];             // 本回合技能使用标记
    int extra_action_count[2];      // 额外行动次数

    //========== 缓存计算值 ==========
    int cached_speed[2];            // 考虑异常后的速度
    int cached_crit_damage[2];      // 暴击伤害倍率(默认200)

    //========== 重置 ==========
    void reset() {
        memset(this, 0, sizeof(BattleWorkspace));

        // 恢复默认倍率
        for (int i = 0; i < 2; i++) {
            hit_rate_mod[i] = 1.0f;
            crit_rate_mod[i] = 1.0f;
            cached_crit_damage[i] = 200;  // 默认暴击2倍
        }
    }
};

#endif // BATTLE_WORKSPACE_H
