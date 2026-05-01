#include <fsm/battleContext.h>
#include <fsm/battleFsm.h>
#include <iostream>
#include <sstream>

namespace {

using EffectBucket = std::unordered_map<State, std::array<std::vector<std::unique_ptr<ContinuousEffect>>, 2>>;
using PendingBucket = std::unordered_map<State, std::array<std::vector<std::unique_ptr<PendingEffect>>, 2>>;

constexpr std::array<State, 40> kLinearStateOrder = {
    State::GAME_START,
    State::OPERATION_ENTER_EXIT_STAGE,
    State::OPERATION_CHOOSE_SKILL_MEDICAMENT,
    State::OPERATION_PROTECTION_MECHANISM_1,
    State::OPERATION_ENTER_STAGE,
    State::BATTLE_ROUND_START,
    State::BATTLE_FIRST_MOVE_RIGHT,
    State::BATTLE_FIRST_ACTION_START,
    State::BATTLE_FIRST_BEFORE_SKILL_HIT,
    State::BATTLE_FIRST_ON_SKILL_HIT,
    State::BATTLE_FIRST_SKILL_EFFECT,
    State::BATTLE_FIRST_ATTACK_DAMAGE,
    State::BATTLE_FIRST_AFTER_ACTION,
    State::BATTLE_FIRST_ACTION_END,
    State::BATTLE_FIRST_AFTER_ACTION_END,
    State::BATTLE_FIRST_EXTRA_ACTION,
    State::BATTLE_FIRST_MOVER_DEATH,
    State::BATTLE_SECOND_ACTION_START,
    State::BATTLE_SECOND_BEFORE_SKILL_HIT,
    State::BATTLE_SECOND_ON_SKILL_HIT,
    State::BATTLE_SECOND_SKILL_EFFECT,
    State::BATTLE_SECOND_ATTACK_DAMAGE,
    State::BATTLE_SECOND_AFTER_ACTION,
    State::BATTLE_SECOND_ACTION_END,
    State::BATTLE_SECOND_AFTER_ACTION_END,
    State::BATTLE_SECOND_EXTRA_ACTION,
    State::BATTLE_ROUND_END,
    State::BATTLE_SECOND_MOVER_DEATH,
    State::BATTLE_OLD_ROUND_END_1,
    State::BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS,
    State::BATTLE_ROUND_REDUCTION_NEW_ROUND_END,
    State::BATTLE_OLD_ROUND_END_2,
    State::BATTLE_DEATH_TIMING,
    State::BATTLE_DEFEAT_STATUS,
    State::BATTLE_OPPONENT_DEFEAT_STATUS,
    State::BATTLE_NEW_DEFEAT_MECHANISM,
    State::OPERATION_PROTECTION_MECHANISM_2,
    State::BATTLE_AFTER_DEFEATED,
    State::CHOOSE_AFTER_DEATH,
    State::BATTLE_AFTER_DEFEATING_OPPONENT,
};

std::size_t find_state_index(State state) {
    const auto it = std::find(kLinearStateOrder.begin(), kLinearStateOrder.end(), state);
    if (it == kLinearStateOrder.end()) {
        return kLinearStateOrder.size();
    }
    return static_cast<std::size_t>(std::distance(kLinearStateOrder.begin(), it));
}

EffectBucket& bucket_for(BattleContext* ctx, EffectContainer container) {
    return (container == EffectContainer::SoulMark) ? ctx->soul_mark_effects : ctx->skills_effects;
}

const EffectBucket& bucket_for(const BattleContext* ctx, EffectContainer container) {
    return (container == EffectContainer::SoulMark) ? ctx->soul_mark_effects : ctx->skills_effects;
}

void execute_bucket_actions(EffectBucket& bucket, int robotId, State state, int roundCount, BattleContext* ctx) {
    auto state_it = bucket.find(state);
    if (state_it == bucket.end()) {
        return;
    }

    auto& robotEffects = state_it->second[robotId];
    for (auto& effect : robotEffects) {
        // 回合类效果：检查是否被断回合（valid_id 版本号不匹配）
        if (effect->isRoundEffect() && effect->valid_id_ != ctx->round_effect_valid_id[robotId]) {
            continue;  // 已被断回合失效，跳过执行，等待统一清理
        }
        if (!effect->isExpired(roundCount)) {
            (*effect)(ctx);
        }
    }
    // 不在此处删除过期/被断效果 — 统一在 cleanup_expired_effects() 处理
}

void execute_pending_bucket_actions(PendingBucket& bucket, int robotId, State state, int roundCount, BattleContext* ctx) {
    auto state_it = bucket.find(state);
    if (state_it == bucket.end()) {
        return;
    }

    auto& robotEffects = state_it->second[robotId];
    for (auto it = robotEffects.begin(); it != robotEffects.end();) {
        auto& effect = *it;
        if (effect->isExpired(roundCount)) {
            it = robotEffects.erase(it);
            continue;
        }

        const PendingEffectDisposition disposition = effect->onState(ctx);
        if (effect->isExpired(roundCount) || disposition == PendingEffectDisposition::Consume) {
            it = robotEffects.erase(it);
            continue;
        }
        ++it;
    }
}

} // namespace

BattleContext::BattleContext(IControlBlock* control_block, const SeerRobot robots[])
    : m_fsm(nullptr)
    , seerRobot{robots[0], robots[1]}
    , roundCount(0)
    , uuid(std::chrono::system_clock::now().time_since_epoch().count())
    , currentState(State::GAME_START)
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
    on_stage[0] = 0;
    on_stage[1] = 0;
    clear_all_on_stage_abnormal_statuses();
    reset_operation_collection();
    roundChoice[0][0] = -1;
    roundChoice[0][1] = -1;
    roundChoice[1][0] = -1;
    roundChoice[1][1] = -1;
}

bool BattleContext::need_input() const {
    if (!is_empty) return false;
    switch (currentState) {
        case State::CHOOSE_AFTER_DEATH:
        if (seerRobot[0].elfPets[on_stage[0]].hp == 0
            || seerRobot[1].elfPets[on_stage[1]].hp == 0
        ) return true;
        else return false;
        case State::OPERATION_CHOOSE_SKILL_MEDICAMENT:
            return true;
        default:
            return false;
    }
}

void BattleContext::generateState() {
    this->failed_attempts = 0;
    if (currentState == State::FINISHED) {
        return;
    }

    const std::size_t index = find_state_index(currentState);
    if (index >= kLinearStateOrder.size()) {
        currentState = State::FINISHED;
        return;
    }

    if (index + 1 >= kLinearStateOrder.size()) {
        currentState = State::BATTLE_ROUND_COMPLETION;
        return;
    }

    currentState = kLinearStateOrder[index + 1];
}

void BattleContext::back_to_last_state() {
    ++failed_attempts;
    if (MAX_ATTEMPTS && failed_attempts > MAX_ATTEMPTS) {
        currentState = State::FINISHED;
        return;
    }

    if (currentState == State::FINISHED) {
        currentState = State::BATTLE_ROUND_COMPLETION;
        return;
    }

    const std::size_t index = find_state_index(currentState);
    if (index == 0 || index >= kLinearStateOrder.size()) {
        currentState = State::GAME_START;
        return;
    }

    currentState = kLinearStateOrder[index - 1];
}

void BattleContext::registerEffect(State trigger, int owner, std::unique_ptr<ContinuousEffect> effect,
                                   EffectContainer container) {
    if (effect->isRoundEffect()) {
        ++active_round_effects[owner];
        effect->valid_id_ = round_effect_valid_id[owner];
    }
    bucket_for(this, container)[trigger][owner].push_back(std::move(effect));
}

void BattleContext::registerPendingEffect(State observeState, int owner, std::unique_ptr<PendingEffect> effect) {
    pending_effects[observeState][owner].push_back(std::move(effect));
}

void BattleContext::clear_on_stage_abnormal_statuses(int robotId) {
    if (robotId < 0 || robotId > 1) {
        return;
    }
    abnormal_status_end_round[robotId].fill(0);
}

void BattleContext::clear_all_on_stage_abnormal_statuses() {
    clear_on_stage_abnormal_statuses(0);
    clear_on_stage_abnormal_statuses(1);
}

void BattleContext::cleanup_expired_effects() {
    // 清理一个效果桶中：
    //   1. 自然过期的效果（isExpired）
    //   2. 被断回合失效的效果（valid_id 不匹配）
    // 同时扣减 active_round_effects 计数。
    auto cleanup_bucket = [this](EffectBucket& bucket) {
        for (auto& [state, per_player] : bucket) {
            for (int p = 0; p < 2; ++p) {
                auto& effects = per_player[p];
                if (effects.empty()) continue;

                int removed = 0;
                for (const auto& e : effects) {
                    if (!e->isRoundEffect()) continue;
                    if (e->isExpired(roundCount)
                        || e->valid_id_ != round_effect_valid_id[p]) {
                        ++removed;
                    }
                }

                std::erase_if(effects,
                              [this, p](const std::unique_ptr<ContinuousEffect>& e) {
                                  if (!e->isRoundEffect()) return false;
                                  return e->isExpired(roundCount)
                                      || e->valid_id_ != round_effect_valid_id[p];
                              });

                active_round_effects[p] -= removed;
                if (active_round_effects[p] < 0) active_round_effects[p] = 0;
            }
        }
    };

    cleanup_bucket(skills_effects);
    cleanup_bucket(soul_mark_effects);

    // 清理过期的断回合补偿回调
    for (int p = 0; p < 2; ++p) {
        auto& cbs = on_round_broken[p];
        for (auto it = cbs.begin(); it != cbs.end(); ) {
            if (it->second.is_expired(roundCount)) {
                it = cbs.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void BattleContext::remove_all_round_effects(int robotId) {
    if (robotId < 0 || robotId > 1) return;

    const bool had_effects = active_round_effects[robotId] > 0;

    // O(1) 无效化：递增版本号 + 计数器归零
    ++round_effect_valid_id[robotId];
    active_round_effects[robotId] = 0;

    // 确实断了回合效果时，触发被断方的补偿回调
    if (had_effects) {
        for (auto& [id, cb] : on_round_broken[robotId]) {
            if (!cb.is_expired(roundCount)) {
                cb.fn(this);
            }
        }
        on_round_broken[robotId].clear();
    }
}

int BattleContext::register_break_callback(int owner, int duration_rounds,
                                           std::function<void(BattleContext*)> fn) {
    if (owner < 0 || owner > 1) return -1;
    int id = next_break_callback_id_++;
    on_round_broken[owner][id] = BreakCallback{roundCount, duration_rounds, std::move(fn)};
    return id;
}

void BattleContext::remove_break_callback(int owner, int callback_id) {
    if (owner < 0 || owner > 1) return;
    on_round_broken[owner].erase(callback_id);
}


void BattleContext::set_abnormal_status_end_round(int robotId, int statusId, int endRound) {
    if (robotId < 0 || robotId > 1 || !is_valid_abnormal_status_id(statusId)) {
        return;
    }
    abnormal_status_end_round[robotId][statusId] = endRound;
}

void BattleContext::apply_abnormal_status_for_rounds(int robotId, int statusId, int durationRounds) {
    if (durationRounds <= 0) {
        return;
    }
    set_abnormal_status_end_round(robotId, statusId, roundCount + durationRounds);
}

int BattleContext::get_abnormal_status_end_round(int robotId, int statusId) const {
    if (robotId < 0 || robotId > 1 || !is_valid_abnormal_status_id(statusId)) {
        return 0;
    }
    return abnormal_status_end_round[robotId][statusId];
}

bool BattleContext::has_active_abnormal_status(int robotId, int statusId) const {
    if (robotId < 0 || robotId > 1 || !is_valid_abnormal_status_id(statusId)) {
        return false;
    }
    return roundCount < abnormal_status_end_round[robotId][statusId];
}

void BattleContext::registerPassiveEffect(int owner, int effectId, ContinuousEffect* effect) {
    passiveEffects[owner][effectId] = effect;
}

void BattleContext::execute_pending_effects(int robotId, State state) {
    if (robotId == -1) {
        execute_pending_effects(0, state);
        execute_pending_effects(1, state);
        return;
    }

    execute_pending_bucket_actions(pending_effects, robotId, state, roundCount, this);
}

void BattleContext::execute_registered_actions(int robotId, State state) {
    if (robotId == -1) {
        execute_registered_actions(0, state);
        execute_registered_actions(1, state);
        return;
    }

    // 未来触发器先于正式效果运行，这样它可以在当前时点落地新的正式效果。
    execute_pending_effects(robotId, state);

    // 魂印容器优先于技能容器执行。
    execute_bucket_actions(soul_mark_effects, robotId, state, roundCount, this);
    execute_bucket_actions(skills_effects, robotId, state, roundCount, this);
}

template<int EffectId>
bool BattleContext::hasEffect(State trigger, int owner) const {
    const auto has_in_bucket = [&](EffectContainer container) {
        const auto& bucket = bucket_for(this, container);
        auto it = bucket.find(trigger);
        if (it == bucket.end()) {
            return false;
        }
        for (const auto& effect : it->second.at(owner)) {
            if (effect->getEffectId() == EffectId && !effect->isExpired(roundCount)) {
                return true;
            }
        }
        return false;
    };

    if (has_in_bucket(EffectContainer::SoulMark)) {
        return true;
    }
    if (has_in_bucket(EffectContainer::Skill)) {
        return true;
    }
    return false;
}

template<int EffectId>
bool BattleContext::consumeEffect(State trigger, int opponent) {
    // 默认仅消费技能容器，避免技能侧误删魂印侧效果。
    auto& bucket = bucket_for(this, EffectContainer::Skill);
    auto it = bucket.find(trigger);
    if (it == bucket.end()) return false;
    auto& oppEffects = it->second[opponent];
    for (auto& effect : oppEffects) {
        if (effect->getEffectId() == EffectId && !effect->isExpired(roundCount)) {
            bool consumed = effect->consume(this);
            if (effect->isExpired(roundCount)) {
                std::erase_if(oppEffects, [this](auto& e) { return e->isExpired(roundCount); });
            }
            return consumed;
        }
    }
    return false;
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

std::string BattleContext::getStateJson() const {
    // ... (existing implementation kept below)
    auto append_skills = [](std::ostringstream& oss, const ElfPet& pet) {
        oss << "\"skills\":[";
        for (int i = 0; i < 5; ++i) {
            const auto& skill = pet.skills[i];
            oss << "{\"id\":" << skill.id
                << ",\"name\":\"" << skill.name
                << "\",\"pp\":" << skill.pp
                << ",\"maxPp\":" << skill.maxPP
                << "}";
            if (i < 4) {
                oss << ",";
            }
        }
        oss << "]";
    };

    auto append_party = [this, &append_skills](std::ostringstream& oss, int robot_id) {
        oss << "\"party\":[";
        for (int slot = 0; slot < 6; ++slot) {
            const ElfPet& pet = seerRobot[robot_id].elfPets[slot];
            const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
            oss << "{"
                << "\"slot\":" << slot << ","
                << "\"id\":" << pet.id << ","
                << "\"name\":\"" << pet.name << "\","
                << "\"hp\":" << pet.hp << ","
                << "\"maxHp\":" << max_hp << ","
                << "\"alive\":" << (pet.hp > 0 ? "true" : "false") << ","
                << "\"onStage\":" << (on_stage[robot_id] == slot ? "true" : "false") << ",";
            append_skills(oss, pet);
            oss << "}";
            if (slot < 5) {
                oss << ",";
            }
        }
        oss << "]";
    };

    const bool wait_input_state = currentState == State::OPERATION_CHOOSE_SKILL_MEDICAMENT
        || currentState == State::CHOOSE_AFTER_DEATH;
    const bool waiting_input = wait_input_state && is_empty;
    const bool need_death_switch0 = waiting_input && currentState == State::CHOOSE_AFTER_DEATH
        && seerRobot[0].elfPets[on_stage[0]].hp <= 0;
    const bool need_death_switch1 = waiting_input && currentState == State::CHOOSE_AFTER_DEATH
        && seerRobot[1].elfPets[on_stage[1]].hp <= 0;

    std::ostringstream oss;
    oss << "{";
    oss << "\"uuid\":" << uuid << ",";
    oss << "\"round\":" << roundCount << ",";
    oss << "\"state\":" << static_cast<int>(currentState) << ",";
    oss << "\"stateName\":\"" << state_name_cn(currentState) << "\",";
    oss << "\"needInput\":" << (waiting_input ? "true" : "false") << ",";
    oss << "\"inputPlayer\":" << current_player_id_ << ",";
    oss << "\"needDeathSwitch\":{";
    oss << "\"player0\":" << (need_death_switch0 ? "true" : "false") << ",";
    oss << "\"player1\":" << (need_death_switch1 ? "true" : "false");
    oss << "},";

    // Player 0 info
    oss << "\"player0\":{";
    oss << "\"onStage\":" << on_stage[0] << ",";
    const ElfPet& pet0 = seerRobot[0].elfPets[on_stage[0]];
    oss << "\"pet\":{";
    oss << "\"id\":" << pet0.id << ",";
    oss << "\"name\":\"" << pet0.name << "\",";
    oss << "\"hp\":" << pet0.hp << ",";
    oss << "\"maxHp\":" << pet0.numericalBase[NumericalPropertyIndex::HP] << ",";
    oss << "\"levels\":[";
    for (int i = 0; i < 6; ++i) {
        oss << pet0.levels[i];
        if (i < 5) oss << ",";
    }
    oss << "],";
    append_skills(oss, pet0);
    oss << "}";
    oss << ",";
    append_party(oss, 0);
    oss << "},";

    // Player 1 info
    oss << "\"player1\":{";
    oss << "\"onStage\":" << on_stage[1] << ",";
    const ElfPet& pet1 = seerRobot[1].elfPets[on_stage[1]];
    oss << "\"pet\":{";
    oss << "\"id\":" << pet1.id << ",";
    oss << "\"name\":\"" << pet1.name << "\",";
    oss << "\"hp\":" << pet1.hp << ",";
    oss << "\"maxHp\":" << pet1.numericalBase[NumericalPropertyIndex::HP] << ",";
    oss << "\"levels\":[";
    for (int i = 0; i < 6; ++i) {
        oss << pet1.levels[i];
        if (i < 5) oss << ",";
    }
    oss << "],";
    append_skills(oss, pet1);
    oss << "}";
    oss << ",";
    append_party(oss, 1);
    oss << "},";

    // Last actions
    oss << "\"lastActions\":{";
    oss << "\"player0\":{\"type\":" << ws.lastActionType[0] << ",\"index\":" << ws.lastActionIndex[0] << "},";
    oss << "\"player1\":{\"type\":" << ws.lastActionType[1] << ",\"index\":" << ws.lastActionIndex[1] << "}";
    oss << "},";

    // Damage info
    oss << "\"damage\":{";
    oss << "\"pending\":{";
    oss << "\"attacker\":" << ws.pendingDamage.attackerId << ",";
    oss << "\"defender\":" << ws.pendingDamage.defenderId << ",";
    oss << "\"final\":" << ws.pendingDamage.final;
    oss << "},";
    oss << "\"resolved\":{";
    oss << "\"attacker\":" << ws.resolvedDamage.attackerId << ",";
    oss << "\"defender\":" << ws.resolvedDamage.defenderId << ",";
    oss << "\"final\":" << ws.resolvedDamage.final;
    oss << "}";
    oss << "}";

    oss << "}";
    return oss.str();
}

std::string BattleContext::getFullStateJson() const {
    // Shared helpers
    auto json_escape = [](const std::string& s) -> std::string {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c;
            }
        }
        return out;
    };

    auto append_skills_full = [&](std::ostringstream& oss, const ElfPet& pet) {
        oss << "\"skills\":[";
        for (int i = 0; i < 5; ++i) {
            const auto& skill = pet.skills[i];
            oss << "{"
                << "\"id\":" << skill.id << ","
                << "\"name\":\"" << json_escape(skill.name) << "\","
                << "\"pp\":" << skill.pp << ","
                << "\"maxPp\":" << skill.maxPP << ","
                << "\"power\":" << skill.power << ","
                << "\"accuracy\":" << skill.accuracy << ","
                << "\"type\":" << static_cast<int>(skill.type) << ","
                << "\"priority\":" << skill.priority << ","
                << "\"element\":[" << skill.element[0] << "," << skill.element[1] << "]"
                << "}";
            if (i < 4) oss << ",";
        }
        oss << "]";
    };

    auto append_pet_full = [&](std::ostringstream& oss, const ElfPet& pet, int slot, int robot_id) {
        const int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
        oss << "{"
            << "\"slot\":" << slot << ","
            << "\"id\":" << pet.id << ","
            << "\"name\":\"" << json_escape(pet.name) << "\","
            << "\"hp\":" << pet.hp << ","
            << "\"maxHp\":" << max_hp << ","
            << "\"alive\":" << (pet.hp > 0 ? "true" : "false") << ","
            << "\"onStage\":" << (on_stage[robot_id] == slot ? "true" : "false") << ","
            << "\"shield\":" << pet.shield << ","
            << "\"cover\":" << pet.cover << ","
            << "\"isLocked\":" << (pet.is_locked ? "true" : "false") << ","
            << "\"speedPriority\":" << pet.speed_priority << ",";
        // Numerical properties
        oss << "\"stats\":{"
            << "\"attack\":" << pet.numericalProperties[NumericalPropertyIndex::PHYSICAL_ATTACK] << ","
            << "\"specialAttack\":" << pet.numericalProperties[NumericalPropertyIndex::SPECIAL_ATTACK] << ","
            << "\"defense\":" << pet.numericalProperties[NumericalPropertyIndex::DEFENSE] << ","
            << "\"specialDefense\":" << pet.numericalProperties[NumericalPropertyIndex::SPECIAL_DEFENSE] << ","
            << "\"speed\":" << pet.numericalProperties[NumericalPropertyIndex::SPEED] << ","
            << "\"hp\":" << pet.numericalProperties[NumericalPropertyIndex::HP]
            << "},";
        oss << "\"baseStats\":{"
            << "\"attack\":" << pet.numericalBase[NumericalPropertyIndex::PHYSICAL_ATTACK] << ","
            << "\"specialAttack\":" << pet.numericalBase[NumericalPropertyIndex::SPECIAL_ATTACK] << ","
            << "\"defense\":" << pet.numericalBase[NumericalPropertyIndex::DEFENSE] << ","
            << "\"specialDefense\":" << pet.numericalBase[NumericalPropertyIndex::SPECIAL_DEFENSE] << ","
            << "\"speed\":" << pet.numericalBase[NumericalPropertyIndex::SPEED] << ","
            << "\"hp\":" << pet.numericalBase[NumericalPropertyIndex::HP]
            << "},";
        oss << "\"levels\":[";
        for (int i = 0; i < 6; ++i) { oss << pet.levels[i]; if (i < 5) oss << ","; }
        oss << "],";
        oss << "\"elementalAttributes\":[" << pet.elementalAttributes[0] << "," << pet.elementalAttributes[1] << "],";
        oss << "\"soulSeal\":" << pet.soulSeal << ",";
        oss << "\"gender\":" << static_cast<int>(pet.gender) << ",";
        oss << "\"soulMark\":{"
            << "\"id\":" << pet.soulMark.id << ","
            << "\"name\":\"" << json_escape(pet.soulMark.name) << "\""
            << "},";
        append_skills_full(oss, pet);
        // Marks
        oss << ",\"marks\":[";
        for (size_t m = 0; m < pet.marks.size(); ++m) {
            oss << "{\"name\":\"" << json_escape(pet.marks[m].name) << "\",\"count\":" << pet.marks[m].count << "}";
            if (m + 1 < pet.marks.size()) oss << ",";
        }
        oss << "]";
        // Abnormal states — 从 context 的权威数组读取
        oss << ",\"abnormalStates\":[";
        bool first_ab = true;
        for (int ab_id = 0; ab_id <= kOfficialAbnormalStatusMaxId; ++ab_id) {
            if (abnormal_status_end_round[robot_id][ab_id] > roundCount) {
                int remaining = abnormal_status_end_round[robot_id][ab_id] - roundCount;
                if (!first_ab) oss << ",";
                first_ab = false;
                oss << "{\"id\":" << ab_id
                    << ",\"name\":\"" << json_escape(abnormal_status_name_cn(ab_id))
                    << "\",\"remaining\":" << remaining << "}";
            }
        }
        oss << "]";
        oss << "}";
    };

    // --- Build JSON ---
    std::ostringstream oss;
    oss << "{";

    // Basic info
    oss << "\"uuid\":" << uuid << ",";
    oss << "\"round\":" << roundCount << ",";
    oss << "\"state\":" << static_cast<int>(currentState) << ",";
    oss << "\"stateName\":\"" << state_name_cn(currentState) << "\",";
    oss << "\"needInput\":" << (need_input() ? "true" : "false") << ",";
    oss << "\"inputPlayer\":" << current_player_id_ << ",";

    // Debug settings
    oss << "\"debug\":{"
        << "\"stepMode\":" << (debug_step_mode ? "true" : "false") << ","
        << "\"breakpoints\":[";
    {
        bool first = true;
        for (int bp : breakpoints) {
            if (!first) oss << ",";
            first = false;
            oss << bp;
        }
    }
    oss << "]},";

    // Need death switch
    const bool need_ds0 = currentState == State::CHOOSE_AFTER_DEATH && is_empty &&
        seerRobot[0].elfPets[on_stage[0]].hp <= 0;
    const bool need_ds1 = currentState == State::CHOOSE_AFTER_DEATH && is_empty &&
        seerRobot[1].elfPets[on_stage[1]].hp <= 0;
    oss << "\"needDeathSwitch\":{\"player0\":" << (need_ds0 ? "true" : "false")
        << ",\"player1\":" << (need_ds1 ? "true" : "false") << "},";

    // Player 0 full party
    oss << "\"player0\":{";
    oss << "\"onStage\":" << on_stage[0] << ",";
    oss << "\"medicines\":[";
    for (int m = 0; m < MEDICINES_SIZE; ++m) {
        oss << seerRobot[0].medicines[m];
        if (m + 1 < MEDICINES_SIZE) oss << ",";
    }
    oss << "],";
    oss << "\"aliveCount\":" << seerRobot[0].allive() << ",";
    oss << "\"party\":[";
    for (int slot = 0; slot < 6; ++slot) {
        append_pet_full(oss, seerRobot[0].elfPets[slot], slot, 0);
        if (slot < 5) oss << ",";
    }
    oss << "],";
    // Abnormal status end rounds for current on-stage pet
    oss << "\"abnormalStatusEndRounds\":{";
    {
        bool first = true;
        for (int s = 0; s < kOfficialAbnormalStatusSlotCount; ++s) {
            int end = abnormal_status_end_round[0][s];
            if (end > 0) {
                if (!first) oss << ",";
                first = false;
                oss << "\"" << s << "\":" << end;
            }
        }
    }
    oss << "}";
    oss << "},";

    // Player 1 full party
    oss << "\"player1\":{";
    oss << "\"onStage\":" << on_stage[1] << ",";
    oss << "\"medicines\":[";
    for (int m = 0; m < MEDICINES_SIZE; ++m) {
        oss << seerRobot[1].medicines[m];
        if (m + 1 < MEDICINES_SIZE) oss << ",";
    }
    oss << "],";
    oss << "\"aliveCount\":" << seerRobot[1].allive() << ",";
    oss << "\"party\":[";
    for (int slot = 0; slot < 6; ++slot) {
        append_pet_full(oss, seerRobot[1].elfPets[slot], slot, 1);
        if (slot < 5) oss << ",";
    }
    oss << "],";
    oss << "\"abnormalStatusEndRounds\":{";
    {
        bool first = true;
        for (int s = 0; s < kOfficialAbnormalStatusSlotCount; ++s) {
            int end = abnormal_status_end_round[1][s];
            if (end > 0) {
                if (!first) oss << ",";
                first = false;
                oss << "\"" << s << "\":" << end;
            }
        }
    }
    oss << "}";
    oss << "},";

    // Workspace details
    oss << "\"workspace\":{";
    oss << "\"preemptiveRight\":\"" << (preemptive_right == PreemptiveRight::SEER_ROBOT_1 ? "player0"
            : preemptive_right == PreemptiveRight::SEER_ROBOT_2 ? "player1" : "none") << "\",";
    oss << "\"preemptiveLevel\":[" << ws.preemptive_level[0] << "," << ws.preemptive_level[1] << "],";
    oss << "\"roundChoice\":[";
    oss << "{\"player0\":{\"type\":" << ws.roundChoice[0][0] << ",\"index\":" << ws.roundChoice[0][1] << "}},";
    oss << "{\"player1\":{\"type\":" << ws.roundChoice[1][0] << ",\"index\":" << ws.roundChoice[1][1] << "}}";
    oss << "],";
    oss << "\"lastActions\":[";
    oss << "{\"player0\":{\"type\":" << ws.lastActionType[0] << ",\"index\":" << ws.lastActionIndex[0] << "}},";
    oss << "{\"player1\":{\"type\":" << ws.lastActionType[1] << ",\"index\":" << ws.lastActionIndex[1] << "}}";
    oss << "],";
    oss << "\"damage\":{";
    oss << "\"pending\":{\"attacker\":" << ws.pendingDamage.attackerId
        << ",\"defender\":" << ws.pendingDamage.defenderId
        << ",\"final\":" << ws.pendingDamage.final
        << ",\"isCrit\":" << (ws.pendingDamage.isCrit ? "true" : "false")
        << ",\"isTrueDamage\":" << (ws.pendingDamage.isTrueDamage ? "true" : "false") << "},";
    oss << "\"resolved\":{\"attacker\":" << ws.resolvedDamage.attackerId
        << ",\"defender\":" << ws.resolvedDamage.defenderId
        << ",\"final\":" << ws.resolvedDamage.final
        << ",\"isCrit\":" << (ws.resolvedDamage.isCrit ? "true" : "false")
        << ",\"isTrueDamage\":" << (ws.resolvedDamage.isTrueDamage ? "true" : "false") << "}";
    oss << "},";
    oss << "\"damageReduce\":{";
    oss << "\"player0Add\":[" << ws.damage_reduce_add[0][0] << "," << ws.damage_reduce_add[0][1]
        << "," << ws.damage_reduce_add[0][2] << "," << ws.damage_reduce_add[0][3] << "],";
    oss << "\"player0Mul\":[" << ws.damage_reduce_mul[0][0] << "," << ws.damage_reduce_mul[0][1]
        << "," << ws.damage_reduce_mul[0][2] << "," << ws.damage_reduce_mul[0][3] << "],";
    oss << "\"player1Add\":[" << ws.damage_reduce_add[1][0] << "," << ws.damage_reduce_add[1][1]
        << "," << ws.damage_reduce_add[1][2] << "," << ws.damage_reduce_add[1][3] << "],";
    oss << "\"player1Mul\":[" << ws.damage_reduce_mul[1][0] << "," << ws.damage_reduce_mul[1][1]
        << "," << ws.damage_reduce_mul[1][2] << "," << ws.damage_reduce_mul[1][3] << "]";
    oss << "},";
    oss << "\"viewLevels\":{"
        << "\"player0\":[" << ws.view_levels[0][0] << "," << ws.view_levels[0][1] << "," << ws.view_levels[0][2]
        << "," << ws.view_levels[0][3] << "," << ws.view_levels[0][4] << "," << ws.view_levels[0][5] << "],"
        << "\"player1\":[" << ws.view_levels[1][0] << "," << ws.view_levels[1][1] << "," << ws.view_levels[1][2]
        << "," << ws.view_levels[1][3] << "," << ws.view_levels[1][4] << "," << ws.view_levels[1][5] << "]"
        << "},";
    oss << "\"battleAttrs\":{"
        << "\"player0\":[" << ws.battle_attrs[0][NumericalPropertyIndex::PHYSICAL_ATTACK]
        << "," << ws.battle_attrs[0][NumericalPropertyIndex::SPECIAL_ATTACK]
        << "," << ws.battle_attrs[0][NumericalPropertyIndex::DEFENSE]
        << "," << ws.battle_attrs[0][NumericalPropertyIndex::SPECIAL_DEFENSE]
        << "," << ws.battle_attrs[0][NumericalPropertyIndex::SPEED]
        << "," << ws.battle_attrs[0][NumericalPropertyIndex::HP] << "],"
        << "\"player1\":[" << ws.battle_attrs[1][NumericalPropertyIndex::PHYSICAL_ATTACK]
        << "," << ws.battle_attrs[1][NumericalPropertyIndex::SPECIAL_ATTACK]
        << "," << ws.battle_attrs[1][NumericalPropertyIndex::DEFENSE]
        << "," << ws.battle_attrs[1][NumericalPropertyIndex::SPECIAL_DEFENSE]
        << "," << ws.battle_attrs[1][NumericalPropertyIndex::SPEED]
        << "," << ws.battle_attrs[1][NumericalPropertyIndex::HP] << "]"
        << "},";
    // Skill resolution status
    oss << "\"skillResolution\":{";
    oss << "\"player0Ready\":" << (ws.skill_resolution_ready[0] ? "true" : "false") << ",";
    oss << "\"player1Ready\":" << (ws.skill_resolution_ready[1] ? "true" : "false") << ",";
    oss << "\"player0Used\":" << (ws.skill_used[0] ? "true" : "false") << ",";
    oss << "\"player1Used\":" << (ws.skill_used[1] ? "true" : "false") << ",";
    oss << "\"player0Attacked\":" << (ws.has_attacked[0] ? "true" : "false") << ",";
    oss << "\"player1Attacked\":" << (ws.has_attacked[1] ? "true" : "false");
    oss << "}";
    oss << "},";

    // Effects tables
    auto append_effect_table = [&](std::ostringstream& oss,
                                    const std::unordered_map<State, std::array<std::vector<std::unique_ptr<ContinuousEffect>>, 2>>& bucket) {
        oss << "{";
        bool first_state = true;
        for (const auto& [state, per_player] : bucket) {
            for (int p = 0; p < 2; ++p) {
                if (per_player[p].empty()) continue;
                if (!first_state) oss << ",";
                first_state = false;
                oss << "\"" << static_cast<int>(state) << "_p" << p << "\":{";
                oss << "\"state\":" << static_cast<int>(state) << ",";
                oss << "\"stateName\":\"" << state_name_cn(state) << "\",";
                oss << "\"player\":" << p << ",";
                oss << "\"count\":" << per_player[p].size() << ",";
                oss << "\"effects\":[";
                for (size_t ei = 0; ei < per_player[p].size(); ++ei) {
                    const auto& eff = per_player[p][ei];
                    oss << "{"
                        << "\"effectId\":" << eff->getEffectId() << ","
                        << "\"owner\":" << eff->owner() << ","
                        << "\"isExpired\":" << (eff->isExpired(roundCount) ? "true" : "false") << ","
                        << "\"isRoundEffect\":" << (eff->isRoundEffect() ? "true" : "false") << ","
                        << "\"registeredRound\":" << eff->getRegisteredRound();
                    oss << "}";
                    if (ei + 1 < per_player[p].size()) oss << ",";
                }
                oss << "]}";
            }
        }
        oss << "}";
    };

    auto append_pending_table = [&](std::ostringstream& oss,
                                     const PendingBucket& bucket) {
        oss << "{";
        bool first_state = true;
        for (const auto& [state, per_player] : bucket) {
            for (int p = 0; p < 2; ++p) {
                if (per_player[p].empty()) continue;
                if (!first_state) oss << ",";
                first_state = false;
                oss << "\"" << static_cast<int>(state) << "_p" << p << "\":{";
                oss << "\"state\":" << static_cast<int>(state) << ",";
                oss << "\"stateName\":\"" << state_name_cn(state) << "\",";
                oss << "\"player\":" << p << ",";
                oss << "\"count\":" << per_player[p].size();
                oss << "}";
            }
        }
        oss << "}";
    };

    oss << "\"skillEffects\":";
    append_effect_table(oss, skills_effects);
    oss << ",\"soulMarkEffects\":";
    append_effect_table(oss, soul_mark_effects);
    oss << ",\"pendingEffects\":";
    append_pending_table(oss, pending_effects);
    oss << ",\"passiveEffects\":{";
    {
        bool first_player = true;
        for (int p = 0; p < 2; ++p) {
            if (passiveEffects[p].empty()) continue;
            if (!first_player) oss << ",";
            first_player = false;
            oss << "\"player" << p << "\":[";
            bool first_pe = true;
            for (const auto& [effId, eff] : passiveEffects[p]) {
                if (!first_pe) oss << ",";
                first_pe = false;
                oss << "{\"effectId\":" << effId << ",\"isExpired\":" << (eff->isExpired(roundCount) ? "true" : "false") << "}";
            }
            oss << "]";
        }
    }
    oss << "},";

    // Operation log
    oss << "\"operationLog\":\"" << json_escape(operation_log_) << "\",";

    // Raw buffer info
    oss << "\"bufferInfo\":{"
        << "\"isEmpty\":" << (is_empty ? "true" : "false") << ","
        << "\"bufferSize\":" << m_buffer.size()
        << "},";

    // Operation collection status
    oss << "\"operationCollected\":{"
        << "\"player0\":" << (operation_collected[0] ? "true" : "false") << ","
        << "\"player1\":" << (operation_collected[1] ? "true" : "false")
        << "}";

    oss << "}";
    return oss.str();
}
