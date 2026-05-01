#ifndef PLUGIN_INTERFACE_H
#define PLUGIN_INTERFACE_H

#include <string>
#include <utility>
#include <vector>

class BattleContext;
struct EffectArgs;
enum class EffectResult : int;

// Effect function type (same as defined in effect.h)
using EffectFn = EffectResult (*)(BattleContext*, const EffectArgs&);

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