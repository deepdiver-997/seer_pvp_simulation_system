#ifndef CORE_API_H
#define CORE_API_H

#include <primitives/battle_primitives.h>
#include <plugin/plugin_interface.h>

// ════════════════════════════════════════════════════════════════════
// Core → 插件 的函数指针集合（依赖倒置 / vtable 模式）
//
// 目的：插件效果要"走真原语"（断回合/异常/回血/清强…），但插件不链接 sim_core，
// 无法直调非内联原语，也不该为此把原语内联进头（那会让 dylib 间接依赖 core 符号）。
// 解法：core 在插件初始化时把**自己原语实现的函数指针**打包传给插件；插件只调指针。
//   - 原语留在 sim_core（单一真相源，免疫/异常/事件语义不外泄）。
//   - "原语缓慢增多" → 往 CoreApi 加一个槽，插件初始化自动拿到。
//   - 与现有"插件→core 注册"（IEffectRegistry）互补：这是"core→插件 传函数"另一半。
// ════════════════════════════════════════════════════════════════════

// Core 原语函数指针集合。由 sim_core 填充（见 src/effects/core_api.cpp 的 core_api()）。
struct CoreApi {
    // 契约版本：插件初始化时可与 core 核对；不匹配可拒绝加载。
    const char* version = nullptr;

    // 施加异常（免疫/反弹/转化/抗性 全在 core 侧按序处理）。
    ApplyAnomalyResult (*apply_anomaly)(BattleContext*, int target, int anomaly_id,
                                        int duration_rounds, int actor);
    // 断回合（查免断 + 发 EVENT_BREAK + 清回合类盔威封属）。
    BreakResult (*break_round_effects)(BattleContext*, int target);
    // 消除目标正等级能力提升（"消除双方能力提升状态"）。
    void (*clear_stat_boosts)(BattleContext*, int target);
    // 按数值恢复（吃封回血 + 恢复效果修正% + 记 last_heal）。
    HealResult (*heal_amount)(BattleContext*, int target, int amount);
    // 授予"下一回合必先"（分等级：tier 越高越先；可被断回合移除）。
    void (*grant_guaranteed_first)(BattleContext*, int owner, int tier);
};

// sim_core 暴露的 CoreApi 单例（实际填充）。插件侧不调它；由 core 在初始化时传入。
const CoreApi& core_api();

// 插件初始化时 core 传入的打包句柄 = 注册表(插件→core) + core 函数指针(core→插件)。
struct PluginInitApi {
    IEffectRegistry* registry;
    const CoreApi* core;
};

#endif // CORE_API_H