#ifndef EFFECT_H
#define EFFECT_H

#include <string>
#include <vector>
#include <cstddef>

enum class EffectResult : int {
    kOk = 0,
    kSkillInvalid = 1,
    kHitEffectInvalid = 2
};

enum class HitInvalidMode : int {
    kEffectsOnly = 0,
    kFullNull = 1
};

struct EffectArgs {
    const int* int_args;
    int int_count;
    const int* vec_offsets;
    const int* vec_sizes;
    int vec_count;
    const void* extra;
};

class BattleContext;
using EffectFn = EffectResult (*)(BattleContext*, const EffectArgs*);

class Effect {
public:
    Effect() : id(-1), priority(0), owner(0), left_round(-1), logic(nullptr), args(nullptr) {}
    Effect(int id, int priority, int owner, int lr, EffectFn logic = nullptr, const EffectArgs* args = nullptr);

    int id;
    int priority;
    int left_round;
    int owner;
    std::string description;
    EffectFn logic;
    const EffectArgs* args;

    bool operator<(const Effect& other) const { return priority < other.priority; }
    bool operator==(const Effect& other) const { return id == other.id; }
    bool operator!=(const Effect& other) const { return id != other.id; }
    bool operator>(const Effect& other) const { return priority > other.priority; }
    bool operator==(std::nullptr_t) const { return logic == nullptr; }
    bool operator!=(std::nullptr_t) const { return logic != nullptr; }
};

class EffectFactory {
public:
    static EffectFactory& getInstance();

    void init();
    Effect getEffect(int id, const EffectArgs* args);
    bool no_confilict(int id1, int id2);

private:
    EffectFactory();
    ~EffectFactory() = default;
    EffectFactory(const EffectFactory&) = delete;
    EffectFactory& operator=(const EffectFactory&) = delete;
    EffectFactory(EffectFactory&&) = delete;
    EffectFactory& operator=(EffectFactory&&) = delete;

    static bool roll_percent(int percent);
    static EffectResult effect_hit_invalid(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_skill_invalid(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_keep_turn(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_break_turn(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_register_skill(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_apply_poison(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_apply_fear(BattleContext* context, const EffectArgs* args);
    static EffectResult effect_drain_hp(BattleContext* context, const EffectArgs* args);

    std::vector<Effect> effects;
};

#endif // EFFECT_H
