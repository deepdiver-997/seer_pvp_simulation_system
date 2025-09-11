#ifndef SOUL_MARK_H
#define SOUL_MARK_H

#include <fsm/battleContext.h>
#include <entities/soul_mark_manager.h>

class SoulMark {
    // using SoulMarkEffectFunc = std::function<void(std::shared_ptr<BattleContext>)>;
public:
    SoulMark(int id, const std::string& name, const std::string& description)
        : id(id), name(name), description(description) {
            #ifdef CLEAR_CACHE
            effect = std::move(SoulMarkManager::getInstance().getEffectFunc(id));
            #else
            effect = SoulMarkManager::getInstance().getEffectFunc(id);
            #endif
            if (effect == nullptr) {
                throw std::runtime_error("SoulMark::SoulMark: Invalid soul mark id: " + std::to_string(id));
            }
        }

// private:
    int id;
    std::string name;
    std::string description;
    #ifdef CLEAR_CACHE
    // If CLEAR_CACHE is defined, use shared_ptr directly to avoid dangling pointers
    std::shared_ptr<std::function<void(std::shared_ptr<BattleContext>)>> effect; // Effect function pointer
    #else
    std::function<void(std::shared_ptr<BattleContext>)> effect; // Effect function pointer
    #endif
};

#endif // SOUL_MARK_H