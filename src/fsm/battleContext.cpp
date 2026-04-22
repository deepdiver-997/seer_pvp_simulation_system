#include <fsm/battleContext.h>
#include <fsm/battleFsm.h>
#include <iostream>

BattleContext::BattleContext(IControlBlock* control_block, const SeerRobot robots[])
    : m_fsm(nullptr)
    , seerRobot{robots[0], robots[1]}
    , roundCount(0)
    , uuid(std::chrono::system_clock::now().time_since_epoch().count())
    , currentState(State::BATTLE_ROUND_START)
    , failed_attempts(0)
    , control_block_(control_block)
    , current_player_id_(0)
    , m_buffer(1024)
    , roundChoice(ws.roundChoice)
    , lastActionType(ws.lastActionType)
    , lastActionIndex(ws.lastActionIndex)
    , preemptive_right(ws.preemptive_right)
    , damage_reduce_add(ws.damage_reduce_add)
    , damage_reduce_mul(ws.damage_reduce_mul)
    , pendingDamage(ws.pendingDamage)
    , resolvedDamage(ws.resolvedDamage)
{
    init_battle();
}

BattleContext::~BattleContext() {
}

void BattleContext::init_battle() {
    // 初始化逻辑
}

bool BattleContext::need_input() {
    if (!is_empty) return false;
    switch (currentState) {
        case State::OPERATION_CHOOSE_SKILL_MEDICAMENT:
        case State::OPERATION_PROTECTION_MECHANISM_1:
        case State::OPERATION_PROTECTION_MECHANISM_2:
        case State::CHOOSE_AFTER_DEATH:
            return true;
        default:
            return false;
    }
}

void BattleContext::generateState() {
    std::cout << "State: " << static_cast<int>(currentState) << std::endl;
    this->failed_attempts = 0;
    currentState = static_cast<State>((static_cast<int>(currentState) + 40) % 41);
}

void BattleContext::back_to_last_state() {
    ++failed_attempts;
    if (MAX_ATTEMPTS && failed_attempts > MAX_ATTEMPTS) {
        currentState = State::FINISHED;
        return;
    }
    currentState = static_cast<State>((static_cast<int>(currentState) + 40) % 41);
}

void BattleContext::registerEffect(State trigger, int owner, std::unique_ptr<ContinuousEffect> effect) {
    effects[trigger][owner].push_back(std::move(effect));
}

void BattleContext::registerPassiveEffect(int owner, int effectId, ContinuousEffect* effect) {
    passiveEffects[owner][effectId] = effect;
}

void BattleContext::execute_registered_actions(int robotId, State state) {
    if (robotId == -1) {
        execute_registered_actions(0, state);
        execute_registered_actions(1, state);
        return;
    }

    auto& robotEffects = effects[state][robotId];
    for (auto& effect : robotEffects) {
        if (effect->isRoundEffect()) {
            if (effect->getCreatedInId() != validIds_[robotId]) {
                continue;
            }
        }
        if (!effect->isExpired()) {
            (*effect)(this);
        }
    }
    std::erase_if(robotEffects, [](auto& e) { return e->isExpired(); });
}

template<int EffectId>
bool BattleContext::hasEffect(State trigger, int owner) const {
    auto it = effects.find(trigger);
    if (it == effects.end()) return false;
    for (auto& effect : it->second.at(owner)) {
        if (effect->getEffectId() == EffectId && !effect->isExpired()) {
            return true;
        }
    }
    return false;
}

template<int EffectId>
bool BattleContext::consumeEffect(State trigger, int opponent) {
    auto it = effects.find(trigger);
    if (it == effects.end()) return false;
    auto& oppEffects = it->second[opponent];
    for (auto& effect : oppEffects) {
        if (effect->getEffectId() == EffectId && !effect->isExpired()) {
            bool consumed = effect->consume(this);
            if (effect->isExpired()) {
                std::erase_if(oppEffects, [](auto& e) { return e->isExpired(); });
            }
            return consumed;
        }
    }
    return false;
}

void BattleContext::onRoundEnd() {
    for (auto& [state, robotEffects] : effects) {
        for (int i = 0; i < 2; i++) {
            for (auto& effect : robotEffects[i]) {
                effect->onRoundEnd();
            }
            std::erase_if(robotEffects[i], [](auto& e) { return e->isExpired(); });
        }
    }
}

// 显式实例化模板
template bool BattleContext::hasEffect<0>(State trigger, int owner) const;
template bool BattleContext::hasEffect<1>(State trigger, int owner) const;
template bool BattleContext::hasEffect<2>(State trigger, int owner) const;
template bool BattleContext::hasEffect<3>(State trigger, int owner) const;
template bool BattleContext::hasEffect<4>(State trigger, int owner) const;
template bool BattleContext::hasEffect<10>(State trigger, int owner) const;
template bool BattleContext::hasEffect<11>(State trigger, int owner) const;
template bool BattleContext::hasEffect<12>(State trigger, int owner) const;

template bool BattleContext::consumeEffect<0>(State trigger, int opponent);
template bool BattleContext::consumeEffect<1>(State trigger, int opponent);
template bool BattleContext::consumeEffect<2>(State trigger, int opponent);
template bool BattleContext::consumeEffect<3>(State trigger, int opponent);
template bool BattleContext::consumeEffect<4>(State trigger, int opponent);
template bool BattleContext::consumeEffect<10>(State trigger, int opponent);
template bool BattleContext::consumeEffect<11>(State trigger, int opponent);
template bool BattleContext::consumeEffect<12>(State trigger, int opponent);
