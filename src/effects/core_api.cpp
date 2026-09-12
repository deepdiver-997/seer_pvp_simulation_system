// core_api() — 填充 Core→插件 的函数指针集合（真实原语实现地址）。
#include <plugin/core_api.h>

#include <primitives/battle_primitives.h>

const CoreApi& core_api() {
    static const CoreApi api = {
        /*version=*/"1.0",
        /*apply_anomaly=*/&apply_anomaly,
        /*break_round_effects=*/&break_round_effects,
        /*clear_stat_boosts=*/&clear_stat_boosts,
        /*heal_amount=*/&heal_amount,
        /*grant_guaranteed_first=*/&grant_guaranteed_first,
        /*stat_change=*/&stat_change,
        /*seal_skill=*/&seal_skill,
        /*stat_reversal=*/&stat_reversal,
        /*fixed_damage=*/&fixed_damage,
    };
    return api;
}