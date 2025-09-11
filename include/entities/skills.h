#ifndef SKILLS_H
#define SKILLS_H

#include <effects/effect.h>
#include <fsm/battleContext.h>

class Skills {
public:
    Skills() = default; // Default constructor
    ~Skills() = default; // Default destructor
    // private:
    void loadSkills(); // Function to load skills
    bool skill_usable(); // Indicates if the skill can be used
    bool is_locked; // Indicates if the skill is locked
    int maxPP;
    int pp; // pp == -1 -> using skills without limitation of pp
    int id;
    std::string name;
    std::vector<std::pair<State, Effect>> effects; // Vector to hold effects associated with the skill
    int type; // Type of the skill, e.g., 0: physical, 1: status, 2: special
    int power; // Power of the skill
    int accuracy; // Accuracy of the skill
    float critical_strike_rate; // Critical strike rate of the skill
    int priority; // Priority of the skill
    int element[2];
};

void Skills::loadSkills() {
    // Implementation to load skills from a data source
    // This could involve reading from a file or database
    std::vector<int> skillIds = {1, 2, 3, 4, 5}; // Example skill IDs
    effects.reserve(skillIds.size()); 
    for (int id : skillIds) {
        auto effect = EffectFactory::getInstance().getEffect(id);
        if (effect.logic == nullptr) {
            continue; // error handling if effect is not found
        }
        effects.emplace_back(effect);
    }
    // For now, we can leave it empty or add some mock data
}

bool Skills::skill_usable() {
    return pp != 0 && !is_locked;
}

#endif // SKILLS_H