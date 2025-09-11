#ifndef EFFECT_H
#define EFFECT_H

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

class BattleContext;
class Effect {
public:
    Effect(int id, int priority, int owner, int lr, std::function<void(std::shared_ptr<BattleContext>)> logic = nullptr)
    : id(id), priority(priority), owner(owner), left_round(lr), logic(logic) {}
    ~Effect() = default;
    int id;
    int priority; // Effect priority
    /*
    0: reserved priority
    1: the hit effect is invalid
    2: skills are ineffective
    3: normal level
    */
    int left_round; // Remaining rounds for the effect
    int owner; // 1 for robot1, 2 for robot2
    std::string description;
    std::function<void(std::shared_ptr<BattleContext>)> logic; // Effect logic function
    bool operator<(const Effect& other) const {
        return priority < other.priority;
    }
    bool operator==(const Effect& other) const {
        return id == other.id;
    }
    bool operator!=(const Effect& other) const {
        return id != other.id;
    }
    bool operator>(const Effect& other) const {
        return priority > other.priority;
    }
    bool operator == (std::nullptr_t) const {
        return logic == nullptr;
    }
    bool operator != (std::nullptr_t) const {
        return logic != nullptr;
    }
};

class EffectFactory {
public:
    EffectFactory()
    {
        init();
    }
    EffectFactory(const EffectFactory&) = delete;
    EffectFactory& operator=(const EffectFactory&) = delete;
    EffectFactory(EffectFactory&&) = delete;
    EffectFactory& operator=(EffectFactory&&) = delete;
    ~EffectFactory() = default;

    static EffectFactory& getInstance()
    {
        static EffectFactory instance;
        return instance;
    }

    void init()
    {
        // Initialize effects with their corresponding IDs and functions
        // effects[1] = []() { /* Effect logic for ID 1 */ };
        // effects[2] = []() { /* Effect logic for ID 2 */ };
        // Add more effects as needed
    }

    Effect getEffect(int id)
    {
       if (id >= 0 && id < effects.size()) {
            return effects[id];
        }
        return Effect(-1, 0, 0, 0, nullptr); // Return a null function if the effect is not found
    }

private:
    std::vector<Effect> effects;
};

#endif // EFFECT_H