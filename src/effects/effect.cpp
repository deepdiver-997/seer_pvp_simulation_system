#include <effects/effect.h>
#include <fsm/battleContext.h>
#include <cstdlib>

Effect::Effect(int id, int priority, int owner, int lr, EffectFn logic, const EffectArgs* args)
    : id(id), priority(priority), owner(owner), left_round(lr), logic(logic), args(args) {}

EffectFactory::EffectFactory() : effects(100, Effect(-1, 0, 0, 0, nullptr, nullptr)) {
    init();
}

void EffectFactory::init() {
    effects[0] = Effect(0, 1, 0, 0, &EffectFactory::effect_hit_invalid, nullptr);
    effects[1] = Effect(1, 2, 0, 0, &EffectFactory::effect_skill_invalid, nullptr);
    effects[2] = Effect(2, 3, 0, 0, &EffectFactory::effect_keep_turn, nullptr);
    effects[3] = Effect(3, 4, 0, 0, &EffectFactory::effect_break_turn, nullptr);
    effects[4] = Effect(4, 5, 0, 0, &EffectFactory::effect_register_skill, nullptr);
    effects[10] = Effect(10, 3, 0, 0, &EffectFactory::effect_apply_poison, nullptr);
    effects[11] = Effect(11, 3, 0, 0, &EffectFactory::effect_apply_fear, nullptr);
    effects[12] = Effect(12, 3, 0, 0, &EffectFactory::effect_drain_hp, nullptr);
}

Effect EffectFactory::getEffect(int id, const EffectArgs* args) {
    if (id >= 0 && id < static_cast<int>(effects.size())) {
        Effect effect = effects[id];
        effect.args = args;
        return effect;
    }
    return Effect(-1, 0, 0, 0, nullptr, nullptr);
}

bool EffectFactory::no_confilict(int id1, int id2) {
    if ((id1 == 0 && id2 == 1) || (id1 == 1 && id2 == 0)) {
        return false;
    }
    return true;
}

bool EffectFactory::roll_percent(int percent) {
    if (percent <= 0) return false;
    if (percent >= 100) return true;
    return (std::rand() % 100) < percent;
}

EffectResult EffectFactory::effect_hit_invalid(BattleContext* context, const EffectArgs* args) {
    (void)context;
    (void)args;
    return EffectResult::kHitEffectInvalid;
}

EffectResult EffectFactory::effect_skill_invalid(BattleContext* context, const EffectArgs* args) {
    (void)context;
    (void)args;
    return EffectResult::kSkillInvalid;
}

EffectResult EffectFactory::effect_keep_turn(BattleContext* context, const EffectArgs* args) {
    (void)context;
    (void)args;
    return EffectResult::kOk;
}

EffectResult EffectFactory::effect_break_turn(BattleContext* context, const EffectArgs* args) {
    (void)context;
    (void)args;
    return EffectResult::kOk;
}

EffectResult EffectFactory::effect_register_skill(BattleContext* context, const EffectArgs* args) {
    (void)context;
    (void)args;
    return EffectResult::kOk;
}

EffectResult EffectFactory::effect_apply_poison(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 2) {
        return EffectResult::kOk;
    }
    int targetId = args->int_args[1];
    int duration = (args->int_count >= 3) ? args->int_args[2] : 3;
    int chance = (args->int_count >= 4) ? args->int_args[3] : 100;
    if (!roll_percent(chance)) {
        return EffectResult::kOk;
    }
    auto& target = context->seerRobot[targetId].elfPets[context->on_stage[targetId]];
    target.abnormalStates.emplace_back(1, duration);
    return EffectResult::kOk;
}

EffectResult EffectFactory::effect_apply_fear(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 2) {
        return EffectResult::kOk;
    }
    int targetId = args->int_args[1];
    int duration = (args->int_count >= 3) ? args->int_args[2] : 1;
    int chance = (args->int_count >= 4) ? args->int_args[3] : 100;
    if (!roll_percent(chance)) {
        return EffectResult::kOk;
    }
    auto& target = context->seerRobot[targetId].elfPets[context->on_stage[targetId]];
    target.abnormalStates.emplace_back(7, duration);
    return EffectResult::kOk;
}

EffectResult EffectFactory::effect_drain_hp(BattleContext* context, const EffectArgs* args) {
    if (!context || !args || !args->int_args || args->int_count < 3) {
        return EffectResult::kOk;
    }
    int attackerId = args->int_args[0];
    int targetId = args->int_args[1];
    int amount = args->int_args[2];
    if (amount <= 0) {
        return EffectResult::kOk;
    }
    auto& attacker = context->seerRobot[attackerId].elfPets[context->on_stage[attackerId]];
    auto& target = context->seerRobot[targetId].elfPets[context->on_stage[targetId]];
    int actual = (target.hp < amount) ? target.hp : amount;
    target.hp -= actual;
    attacker.hp += actual;
    return EffectResult::kOk;
}
