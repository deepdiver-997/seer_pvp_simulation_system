#include <effects/effect.h>
#include <fsm/battleContext.h>

namespace {
bool is_valid_slot(int slot) {
    return slot >= 0 && slot < 4;
}
}

// Set additive (general) damage reduction percent for target slot.
// args: [targetId, slot, percent]
extern "C" EffectResult set_additive_damage_reduction_slot(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 3) {
        return EffectResult::kOk;
    }
    int target = args->int_args[0];
    int slot = args->int_args[1];
    int percent = args->int_args[2];
    if (target < 0 || target > 1 || !is_valid_slot(slot)) {
        return EffectResult::kOk;
    }
    context->damage_reduce_add[target][slot] = percent;
    return EffectResult::kOk;
}

// Set multiplicative (extra) damage reduction percent for target slot.
// args: [targetId, slot, percent]
extern "C" EffectResult set_multiplicative_damage_reduction_slot(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 3) {
        return EffectResult::kOk;
    }
    int target = args->int_args[0];
    int slot = args->int_args[1];
    int percent = args->int_args[2];
    if (target < 0 || target > 1 || !is_valid_slot(slot)) {
        return EffectResult::kOk;
    }
    context->damage_reduce_mul[target][slot] = percent;
    return EffectResult::kOk;
}

// Clear both reduction arrays for a target.
// args: [targetId]
extern "C" EffectResult clear_damage_reduction_slots(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 1) {
        return EffectResult::kOk;
    }
    int target = args->int_args[0];
    if (target < 0 || target > 1) {
        return EffectResult::kOk;
    }
    for (int i = 0; i < 4; ++i) {
        context->damage_reduce_add[target][i] = 0;
        context->damage_reduce_mul[target][i] = 0;
    }
    return EffectResult::kOk;
}
