#ifndef SOUL_MARK_H
#define SOUL_MARK_H

#include <effects/effect.h>
#include <entities/soul_mark_manager.h>

class SoulMark {
    // using SoulMarkEffectFunc = std::function<void(std::shared_ptr<BattleContext>)>;
public:
    SoulMark(int id, const std::string& name, const std::string& description, const EffectArgs *args = nullptr)
        : id(id), name(name), description(description) {
            effect = SoulMarkManager::getInstance().getEffectFunc(id);
            this->args = args;
            if (effect == nullptr) {
                throw std::runtime_error("SoulMark::SoulMark: Invalid soul mark id: " + std::to_string(id));
            }
        }

// private:
    int id;
    std::string name;
    std::string description;
    EffectFn effect; // Effect function pointer
    const EffectArgs *args;
};

#endif // SOUL_MARK_H