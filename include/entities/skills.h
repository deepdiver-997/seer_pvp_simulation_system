#ifndef SKILLS_H
#define SKILLS_H

#include <map>
#include <vector>
#include <effects/effect.h>
#include <effects/continuousEffect.h>

// Forward declare BattleContext and State
class BattleContext;
enum class State;

class Skills {
public:
    Skills() = default;
    ~Skills() = default;

    void loadSkills();
    bool skill_usable();

    bool is_locked;
    int maxPP;
    int pp;  // pp == -1 -> 技能使用无限制
    int id;
    std::string name;

    // 技能分类：0=物理, 1=属性(变化), 2=特殊
    int type;
    int power;
    int accuracy;
    float critical_strike_rate;
    int priority;
    int element[2];  // 元素属性

    // 效果分支表 - 不同执行结果对应不同的效果列表
    // Skill执行后会根据执行结果(HIT/MISS/EFFECT_INVALID/SKILL_INVALID)
    // 从这里取出对应的效果指针列表注册到 BattleContext
    // raw Effect* 指针指向 EffectFactory 中持有的数据，生命周期由Skills保证
    std::map<SkillExecResult, std::vector<Effect*>> effectBranches;
};

void Skills::loadSkills() {
    // TODO: 从数据库加载技能效果分支
    // effectBranches[SkillExecResult::HIT].push_back(effectPtr);
}

bool Skills::skill_usable() {
    return pp != 0 && !is_locked;
}

#endif // SKILLS_H
