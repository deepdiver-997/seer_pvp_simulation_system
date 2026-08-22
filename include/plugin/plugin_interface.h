#ifndef PLUGIN_INTERFACE_H
#define PLUGIN_INTERFACE_H

#include <string>
#include <utility>
#include <vector>

#include <fsm/state.h>

class BattleContext;
struct EffectArgs;
enum class EffectResult : int;

// Effect function type (same as defined in effect.h)
using EffectFn = EffectResult (*)(BattleContext*, const EffectArgs&);

// 魂印程序节点（插件面）：一个时点 + 一个效果函数。
// 对等 skill 的 SkillEffectNode，但坍缩掉"执行结果分叉"维度——魂印只有"时点"。
// trigger_state：触发时点（State 枚举）。
// once：触发一次后移除（回合限一次；下回合由 ROUND_START 重新注册）。
// early：战斗开始立即执行一次（信号类，如 2260 的 ignore_pp 须在选择技能前就绪）。
// effect_fn：节点效果函数（插件自写，args[0]=owner, args[1]=1-owner 由引擎绑定）。
struct SoulMarkNodeRef {
    State trigger_state = State::BATTLE_ROUND_START;
    EffectFn effect_fn = nullptr;
    bool once = false;
    bool early = false;

    SoulMarkNodeRef() = default;
    SoulMarkNodeRef(State trigger, EffectFn fn, bool once_ = false, bool early_ = false)
        : trigger_state(trigger), effect_fn(fn), once(once_), early(early_) {}
};

// Plugin interface version for compatibility checking
constexpr const char* kPluginInterfaceVersion = "1.0";

// Plugin registration function signature
// Each plugin library MUST export a function with this signature:
// extern "C" void plugin_register_effects(void* registry)
using PluginRegisterFn = void (*)(void*);

// Effect registry interface - passed to plugins for registration
// This interface allows plugins to register their effects without
// needing to know the concrete manager/factory types
class IEffectRegistry {
public:
    virtual ~IEffectRegistry() = default;

    // Register a soul mark effect
    virtual void registerSoulMark(int soulmark_id, EffectFn effect_fn) = 0;

    // Register a soul mark program（多时点：一个魂印 = 多个 State→effect 节点）。
    virtual void registerSoulMarkProgram(int soulmark_id,
                                         const std::vector<SoulMarkNodeRef>& nodes) = 0;

    // Register a skill/move effect
    virtual void registerSkillEffect(int effect_id, EffectFn effect_fn) = 0;

    // Register multiple soul marks at once
    virtual void registerSoulMarks(
        const std::vector<std::pair<int, EffectFn>>& soulmarks) = 0;

    // Register multiple skill effects at once
    virtual void registerSkillEffects(
        const std::vector<std::pair<int, EffectFn>>& effects) = 0;
};

// Utility to convert effect ID to function name
// Format: effect_{category}_{id}
// category: "soulmark" for soul marks, "skill" for skills
inline std::string effectIdToFunctionName(const std::string& category, int id) {
    return "effect_" + category + "_" + std::to_string(id);
}

// Default plugin init function names
constexpr const char* kSoulMarkPluginInitFn = "soulmark_plugin_register";
constexpr const char* kSkillPluginInitFn = "skill_plugin_register";

#endif // PLUGIN_INTERFACE_H