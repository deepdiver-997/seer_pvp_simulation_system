#include <fsm/battleFsm.h>
#include <fsm/battleContext.h>
#include <fsm/iControlBlock.h>
#include <effects/continuousEffect.h>
#include <numerical-calculation/calculation.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string ts_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::ostringstream oss;
    oss << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void log_battle_line(const std::string& level, const std::string& msg) {
    std::cout << "[" << ts_now() << "] [" << level << "] [battle] " << msg << std::endl;
}

int resolve_first_mover_id(const BattleContext* ctx) {
    if (ctx->preemptive_right == PreemptiveRight::SEER_ROBOT_2) {
        return 1;
    }
    return 0;
}

int resolve_second_mover_id(const BattleContext* ctx) {
    return 1 - resolve_first_mover_id(ctx);
}

bool field_has_on_stage_death(const BattleContext* ctx) {
    if (!ctx) {
        return false;
    }
    return ctx->seerRobot[0].elfPets[ctx->on_stage[0]].hp <= 0
        || ctx->seerRobot[1].elfPets[ctx->on_stage[1]].hp <= 0;
}

bool has_blocking_control_status(const BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return false;
    }

    for (int status_id = 0; status_id <= kOfficialAbnormalStatusMaxId; ++status_id) {
        if (!ctx->has_active_abnormal_status(robot_id, status_id)) {
            continue;
        }
        if (is_control_abnormal_status(static_cast<AbnormalStatusId>(status_id))) {
            return true;
        }
    }
    return false;
}

bool is_skill_action(const BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return false;
    }
    return ctx->roundChoice[robot_id][0] == static_cast<int>(BattleFsm::ActionType::SELECT_SKILL);
}

int resolve_selected_skill_index(const BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return -1;
    }
    if (!is_skill_action(ctx, robot_id)) {
        return -1;
    }
    const int skill_index = ctx->roundChoice[robot_id][1];
    return (skill_index >= 0 && skill_index < 5) ? skill_index : -1;
}

bool should_skip_action_flow(const BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return true;
    }
    if (!is_skill_action(ctx, robot_id)) {
        return true;
    }
    if (ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]].hp <= 0) {
        return true;
    }
    return has_blocking_control_status(ctx, robot_id);
}

void write_skill_resolution(BattleContext* ctx,
                            int robot_id,
                            SkillExecResult result,
                            const SkillResolutionFlags& flags) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return;
    }
    ctx->ws.skill_exec_result[robot_id] = result;
    ctx->ws.skill_resolution_flags[robot_id] = flags;
    ctx->ws.skill_resolution_ready[robot_id] = true;
}

bool should_consume_skill_pp(const BattleContext* ctx, int robot_id) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return false;
    }
    if (ctx->ws.skill_pp_cost_consumed[robot_id]) {
        return false;
    }
    if (!ctx->ws.skill_resolution_ready[robot_id]) {
        return false;
    }
    return resolve_selected_skill_index(ctx, robot_id) >= 0;
}

int consume_selected_skill_pp(BattleContext* ctx, int robot_id) {
    if (!should_consume_skill_pp(ctx, robot_id)) {
        return 0;
    }

    const int skill_index = resolve_selected_skill_index(ctx, robot_id);
    if (skill_index < 0) {
        return 0;
    }

    Skills& skill = ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]].skills[skill_index];
    ctx->ws.skill_pp_cost_consumed[robot_id] = true;

    if (skill.pp == -1) {
        return 0;
    }

    const int pp_cost = std::max(0, ctx->ws.skill_pp_cost_multiplier[robot_id]);
    if (pp_cost <= 0 || skill.pp <= 0) {
        return 0;
    }

    const int consumed = std::min(skill.pp, pp_cost);
    skill.pp -= consumed;
    return consumed;
}

void resolve_skill_execution(BattleContext* ctx, int robot_id, State trigger_state) {
    if (!ctx || robot_id < 0 || robot_id > 1) {
        return;
    }

    const int skill_index = resolve_selected_skill_index(ctx, robot_id);
    if (skill_index < 0) {
        write_skill_resolution(ctx, robot_id, SkillExecResult::SKILL_INVALID, SkillResolutionFlags{false, false});
        return;
    }

    SkillExecutionEffect executor(robot_id, skill_index, trigger_state);
    executor(ctx);
    write_skill_resolution(ctx, robot_id, executor.getLastResult(), executor.getLastResolutionFlags());
}

void clear_damage_snapshot(DamageSnapshot& snapshot) {
    snapshot = DamageSnapshot{};
}

void sync_workspace_from_on_stage(BattleContext* ctx) {
    if (!ctx) {
        return;
    }

    for (int robot_id = 0; robot_id < 2; ++robot_id) {
        const ElfPet& pet = ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]];
        ctx->ws.battle_attrs[robot_id] = pet.numericalProperties;
        std::copy(std::begin(pet.levels), std::end(pet.levels), std::begin(ctx->ws.view_levels[robot_id]));
        std::copy(std::begin(pet.elementalAttributes), std::end(pet.elementalAttributes), std::begin(ctx->ws.view_elementalAttributes[robot_id]));
        ctx->ws.cached_speed[robot_id] = pet.numericalProperties[NumericalPropertyIndex::SPEED];
    }
}

void stage_simple_attack_damage(BattleContext* ctx, int attacker_id) {
    if (!ctx || attacker_id < 0 || attacker_id > 1) {
        return;
    }

    clear_damage_snapshot(ctx->pendingDamage);
    clear_damage_snapshot(ctx->resolvedDamage);

    const int defender_id = 1 - attacker_id;
    const int skill_index = resolve_selected_skill_index(ctx, attacker_id);
    if (skill_index < 0) {
        return;
    }

    const Skills& skill = ctx->seerRobot[attacker_id].elfPets[ctx->on_stage[attacker_id]].skills[skill_index];
    DamageSnapshot snapshot;
    snapshot.attackerId = attacker_id;
    snapshot.defenderId = defender_id;
    snapshot.base = std::max(0, Calculation::calculateDamage(attacker_id, ctx->ws, skill));
    snapshot.afterAdd = Calculation::applyDamageReduction(
        snapshot.base,
        ctx->damage_reduce_add[defender_id],
        ctx->damage_reduce_mul[defender_id]
    );
    snapshot.afterMul = snapshot.afterAdd;
    snapshot.final = snapshot.afterAdd;
    snapshot.addPct = 0;
    snapshot.mulCoef = 1.0;
    snapshot.isRed = true;
    snapshot.isDirect = false;
    snapshot.isFixed = false;
    snapshot.isTrueDamage = false;
    snapshot.isWhiteNumber = false;
    snapshot.isCrit = false;

    ctx->pendingDamage = snapshot;
    ctx->resolvedDamage = snapshot;
}

void apply_resolved_damage(BattleContext* ctx) {
    if (!ctx) {
        return;
    }

    const DamageSnapshot& damage = ctx->resolvedDamage;
    if (damage.defenderId < 0 || damage.defenderId > 1 || damage.final <= 0) {
        return;
    }

    ElfPet& defender = ctx->seerRobot[damage.defenderId].elfPets[ctx->on_stage[damage.defenderId]];
    const int hp_before = defender.hp;
    defender.hp -= damage.final;
    if (defender.hp < 0) {
        defender.hp = 0;
    }

    const int attacker_id = damage.attackerId;
    const int defender_id = damage.defenderId;
    const int skill_index = resolve_selected_skill_index(ctx, attacker_id);
    std::string skill_name = "unknown";
    if (skill_index >= 0 && skill_index < 5) {
        skill_name = ctx->seerRobot[attacker_id].elfPets[ctx->on_stage[attacker_id]].skills[skill_index].name;
    }

    const int red_damage = damage.isRed ? damage.final : 0;
    const int fixed_damage = damage.isFixed ? damage.final : 0;
    const int percent_damage = 0;

    std::ostringstream oss;
    oss << attacker_id << "号机器人"
        << ctx->seerRobot[attacker_id].elfPets[ctx->on_stage[attacker_id]].name
        << " 使用" << skill_name
        << " 对 " << defender_id << "号机器人"
        << defender.name
        << " 造成红伤=" << red_damage
        << " 固定伤害=" << fixed_damage
        << " 百分比伤害=" << percent_damage
        << " hp:" << hp_before << "->" << defender.hp;
    log_battle_line("INFO", oss.str());
}

int resolve_pet_max_hp(const ElfPet& pet) {
    int max_hp = pet.numericalBase[NumericalPropertyIndex::HP];
    if (max_hp <= 0) {
        max_hp = pet.numericalProperties[NumericalPropertyIndex::HP];
    }
    if (max_hp <= 0) {
        max_hp = pet.hp;
    }
    return max_hp;
}

void stage_action_start_abnormal_damage(BattleContext* ctx, int robot_id) {
    if (robot_id < 0 || robot_id > 1) {
        return;
    }

    ctx->ws.action_start_abnormal_damage_pending[robot_id] = false;
    ctx->ws.action_start_abnormal_damage[robot_id] = DamageSnapshot{};

    ElfPet& pet = ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]];
    if (pet.hp <= 0) {
        return;
    }

    const int max_hp = resolve_pet_max_hp(pet);
    if (max_hp <= 0) {
        return;
    }

    int total_damage = 0;
    for (int status_id = 0; status_id <= kOfficialAbnormalStatusMaxId; ++status_id) {
        if (!ctx->has_active_abnormal_status(robot_id, status_id)) {
            continue;
        }
        const auto status = static_cast<AbnormalStatusId>(status_id);
        if (!deals_damage_at_action_start(status)) {
            continue;
        }

        const int status_damage = std::max(1, max_hp / 8);
        total_damage += status_damage;
    }

    if (total_damage <= 0) {
        return;
    }

    DamageSnapshot snapshot;
    snapshot.attackerId = robot_id;
    snapshot.defenderId = robot_id;
    snapshot.base = total_damage;
    snapshot.afterAdd = total_damage;
    snapshot.afterMul = total_damage;
    snapshot.final = total_damage;
    snapshot.addPct = 0;
    snapshot.mulCoef = 1.0;
    snapshot.isRed = false;
    snapshot.isDirect = true;
    snapshot.isFixed = true;
    snapshot.isTrueDamage = true;
    snapshot.isWhiteNumber = true;
    snapshot.isCrit = false;

    ctx->ws.action_start_abnormal_damage[robot_id] = snapshot;
    ctx->ws.action_start_abnormal_damage_pending[robot_id] = true;
}

void settle_staged_action_start_abnormal_damage(BattleContext* ctx, int robot_id) {
    if (robot_id < 0 || robot_id > 1) {
        return;
    }

    if (!ctx->ws.action_start_abnormal_damage_pending[robot_id]) {
        return;
    }

    const DamageSnapshot& snapshot = ctx->ws.action_start_abnormal_damage[robot_id];
    if (snapshot.final <= 0) {
        ctx->ws.action_start_abnormal_damage_pending[robot_id] = false;
        ctx->ws.action_start_abnormal_damage[robot_id] = DamageSnapshot{};
        return;
    }

    ElfPet& pet = ctx->seerRobot[robot_id].elfPets[ctx->on_stage[robot_id]];
    if (pet.hp > 0) {
        pet.hp -= snapshot.final;
        if (pet.hp < 0) {
            pet.hp = 0;
        }
    }

    ctx->ws.action_start_abnormal_damage_pending[robot_id] = false;
    ctx->ws.action_start_abnormal_damage[robot_id] = DamageSnapshot{};
}

} // namespace

BattleFsm::BattleFsm(bool enable_debug)
    : id_debug(enable_debug) {
    bool enable_verbose_trace = false;
#ifdef BATTLE_FSM_VERBOSE_DEFAULT
    enable_verbose_trace = true;
#endif

    if (const char* env = std::getenv("BATTLE_FSM_VERBOSE")) {
        enable_verbose_trace = std::string(env) != "0";
    }
    verbose_trace_ = enable_verbose_trace;

    initHandler();
}

BattleFsm::~BattleFsm() {
}

void BattleFsm::initHandler() {
    stateHandlerMap[State::GAME_START] = &BattleFsm::handle_GameStart;
    stateHandlerMap[State::OPERATION_ENTER_EXIT_STAGE] = &BattleFsm::handle_OperationEnterExitStage;
    stateHandlerMap[State::OPERATION_CHOOSE_SKILL_MEDICAMENT] = &BattleFsm::handle_OperationChooseSkillMedicament;
    stateHandlerMap[State::OPERATION_PROTECTION_MECHANISM_1] = &BattleFsm::handle_OperationProtectionMechanism1;
    stateHandlerMap[State::OPERATION_ENTER_STAGE] = &BattleFsm::handle_OperationEnterStage;
    stateHandlerMap[State::BATTLE_ROUND_START] = &BattleFsm::handle_BattleRoundStart;
    stateHandlerMap[State::BATTLE_FIRST_MOVE_RIGHT] = &BattleFsm::handle_BattleFirstMoveRight;
    stateHandlerMap[State::BATTLE_FIRST_ACTION_START] = &BattleFsm::handle_BattleFirstActionStart;
    stateHandlerMap[State::BATTLE_FIRST_BEFORE_SKILL_HIT] = &BattleFsm::handle_BattleFirstBeforeSkillHit;
    stateHandlerMap[State::BATTLE_FIRST_ON_SKILL_HIT] = &BattleFsm::handle_BattleFirstOnSkillHit;
    stateHandlerMap[State::BATTLE_FIRST_SKILL_EFFECT] = &BattleFsm::handle_BattleFirstSkillEffect;
    stateHandlerMap[State::BATTLE_FIRST_ATTACK_DAMAGE] = &BattleFsm::handle_BattleFirstAttackDamage;
    stateHandlerMap[State::BATTLE_FIRST_AFTER_ACTION] = &BattleFsm::handle_BattleFirstAfterAction;
    stateHandlerMap[State::BATTLE_FIRST_ACTION_END] = &BattleFsm::handle_BattleFirstActionEnd;
    stateHandlerMap[State::BATTLE_FIRST_AFTER_ACTION_END] = &BattleFsm::handle_BattleFirstAfterActionEnd;
    stateHandlerMap[State::BATTLE_FIRST_EXTRA_ACTION] = &BattleFsm::handle_BattleFirstExtraAction;
    stateHandlerMap[State::BATTLE_FIRST_MOVER_DEATH] = &BattleFsm::handle_BattleFirstMoverDeath;
    stateHandlerMap[State::BATTLE_SECOND_ACTION_START] = &BattleFsm::handle_BattleSecondActionStart;
    stateHandlerMap[State::BATTLE_SECOND_BEFORE_SKILL_HIT] = &BattleFsm::handle_BattleSecondBeforeSkillHit;
    stateHandlerMap[State::BATTLE_SECOND_ON_SKILL_HIT] = &BattleFsm::handle_BattleSecondOnSkillHit;
    stateHandlerMap[State::BATTLE_SECOND_SKILL_EFFECT] = &BattleFsm::handle_BattleSecondSkillEffect;
    stateHandlerMap[State::BATTLE_SECOND_ATTACK_DAMAGE] = &BattleFsm::handle_BattleSecondAttackDamage;
    stateHandlerMap[State::BATTLE_SECOND_AFTER_ACTION] = &BattleFsm::handle_BattleSecondAfterAction;
    stateHandlerMap[State::BATTLE_SECOND_ACTION_END] = &BattleFsm::handle_BattleSecondActionEnd;
    stateHandlerMap[State::BATTLE_SECOND_AFTER_ACTION_END] = &BattleFsm::handle_BattleSecondAfterActionEnd;
    stateHandlerMap[State::BATTLE_SECOND_EXTRA_ACTION] = &BattleFsm::handle_BattleSecondExtraAction;
    stateHandlerMap[State::BATTLE_ROUND_END] = &BattleFsm::handle_BattleRoundEnd;
    stateHandlerMap[State::BATTLE_SECOND_MOVER_DEATH] = &BattleFsm::handle_BattleSecondMoverDeath;
    stateHandlerMap[State::BATTLE_OLD_ROUND_END_1] = &BattleFsm::handle_BattleOldRoundEnd1;
    stateHandlerMap[State::BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS] = &BattleFsm::handle_BattleRoundReductionAllRoundMinus;
    stateHandlerMap[State::BATTLE_ROUND_REDUCTION_NEW_ROUND_END] = &BattleFsm::handle_BattleRoundReductionNewRoundEnd;
    stateHandlerMap[State::BATTLE_OLD_ROUND_END_2] = &BattleFsm::handle_BattleOldRoundEnd2;
    stateHandlerMap[State::BATTLE_DEATH_TIMING] = &BattleFsm::handle_BattleDeathTiming;
    stateHandlerMap[State::BATTLE_DEFEAT_STATUS] = &BattleFsm::handle_BattleDefeatStatus;
    stateHandlerMap[State::BATTLE_OPPONENT_DEFEAT_STATUS] = &BattleFsm::handle_BattleOpponentDefeatStatus;
    stateHandlerMap[State::BATTLE_NEW_DEFEAT_MECHANISM] = &BattleFsm::handle_BattleNewDefeatMechanism;
    stateHandlerMap[State::OPERATION_PROTECTION_MECHANISM_2] = &BattleFsm::handle_OperationProtectionMechanism2;
    stateHandlerMap[State::BATTLE_AFTER_DEFEATED] = &BattleFsm::handle_BattleAfterDefeated;
    stateHandlerMap[State::CHOOSE_AFTER_DEATH] = &BattleFsm::handle_ChooseAfterDeath;
    stateHandlerMap[State::BATTLE_AFTER_DEFEATING_OPPONENT] = &BattleFsm::handle_BattleAfterDefeatingOpponent;
    stateHandlerMap[State::BATTLE_ROUND_COMPLETION] = &BattleFsm::handle_BattleRoundCompletion;
    stateHandlerMap[State::FINISHED] = &BattleFsm::handle_Finished;
}

void BattleFsm::addStateAction(State state, HandlerType action) {
    stateHandlerMap[state] = action;
}

void BattleFsm::run(BattleContext* battleContext) {
    std::lock_guard<std::mutex> guard(battleContext->run_mutex);

    // 单步模式或命中断点 → 只执行一个状态就返回
    bool single_shot = battleContext->debug_step_mode ||
        battleContext->breakpoints.count(static_cast<int>(battleContext->currentState)) > 0;

    // 控制块全程持有 unique_ptr，FSM 使用 raw pointer
    // 如果需要等待输入，调用 wait_for_input 后直接返回
    // 数据到达时控制块会再次调用 run
    while(true) {
        if (runInternal(battleContext)) {
            // 需要等待输入，退出 run 让控制块处理
            return;
        }
        if (battleContext->currentState == State::FINISHED) {
            return;
        }
        if (single_shot) {
            // 命中后清除本次断点触发（避免下次 run 再次停在同一状态）
            // 但保留断点设置本身，下次到达该状态时仍会停
            return;
        }
    }
}

bool BattleFsm::runInternal(BattleContext* battleContext) {
    if (battleContext->need_input()) {
        // 需要等待输入，调用控制块的 wait_for_input
        // 控制块会在数据到达时再次调用 run
        battleContext->control_block_->wait_for_input(battleContext);
        return true;
    }

    auto it = stateHandlerMap.find(battleContext->currentState);
    if (it != stateHandlerMap.end()) {
        const State before_state = battleContext->currentState;
        trace_fsm(battleContext, "before");
        HandlerType handler = it->second;
        (this->*handler)(battleContext);
        if (before_state != battleContext->currentState) {
            trace_fsm(battleContext, "after-transition");
        } else {
            trace_fsm(battleContext, "after");
        }
        return false;
    } else {
        std::cerr << "No handler for state: " << static_cast<int>(battleContext->currentState) << std::endl;
        return true;
    }
}

void BattleFsm::operation(BattleContext* battleContext, int robotId, ActionType actionType, int index) {
    int &on_stage = battleContext->on_stage[robotId];
    if (on_stage < 0 || on_stage >= 6) {
        std::cerr << "Invalid on-stage pet index: " << on_stage << std::endl;
        return;
    }
    auto &robot = battleContext->seerRobot[robotId];
    auto &pet = robot.elfPets[on_stage];

    switch (actionType) {
        case ActionType::SELECT_SKILL:
            if (index < 0 || index >= 5) {
                std::cerr << "Invalid skill index: " << index << std::endl;
                return;
            }
            if (pet.skills[index].skill_usable()) {
                battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::SELECT_SKILL);
                battleContext->roundChoice[robotId][1] = index;
            } else {
                std::cerr << "Skill not usable: " << pet.skills[index].name << std::endl;
            }
            break;
        case ActionType::USE_MEDICINE:
            (robot.use_medicine(pet, index) ? std::cout << "Used medicine: " << robot.medicines[index] << std::endl : std::cerr << "Cannot use medicine: " << robot.medicines[index] << std::endl);
            battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::USE_MEDICINE);
            battleContext->roundChoice[robotId][1] = index;
            break;
        case ActionType::CHOOSE_PET:
            if (index < 0 || index >= 6) {
                std::cerr << "Invalid pet index: " << index << std::endl;
                return;
            }
            if (robot.elfPets[index].hp > 0 && robot.elfPets[index].is_locked == false) {
                battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::CHOOSE_PET);
                battleContext->roundChoice[robotId][1] = index;
            } else {
                std::cerr << "Cannot choose this pet: " << robot.elfPets[index].name << std::endl;
            }
            break;
        default:
            break;
    }

    if (verbose_trace_) {
        std::ostringstream oss;
        oss << "operation robot=" << robotId
            << " action=" << static_cast<int>(actionType)
            << " index=" << index
            << " roundChoice=[" << battleContext->roundChoice[robotId][0]
            << "," << battleContext->roundChoice[robotId][1] << "]";
        log(oss.str());
    }
}

void BattleFsm::log(const std::string& message) {
    if (id_debug) {
        std::cout << "DEBUG: " << message << std::endl;
    }
}

void BattleFsm::trace_fsm(const BattleContext* battleContext, const std::string& phase) const {
    if (!verbose_trace_ || !battleContext) {
        return;
    }

    std::ostringstream oss;
    oss << "FSM_TRACE phase=" << phase
        << " state=" << state_name_cn(battleContext->currentState)
        << "(" << static_cast<int>(battleContext->currentState) << ")"
        << " round=" << battleContext->roundCount
        << " current_player=" << battleContext->current_player_id_
        << " waiting_empty=" << (battleContext->is_empty ? 1 : 0)
        << " pending_final=" << battleContext->pendingDamage.final
        << " resolved_final=" << battleContext->resolvedDamage.final;
    if (id_debug) {
        std::cout << oss.str() << std::endl;
    }
}

void BattleFsm::post(std::function<void()> task) {
    if (battle_pool_) {
        battle_pool_->post(std::move(task));
    }
}

void BattleFsm::General_Handler(BattleContext* battleContext, int robotId) {
    battleContext->execute_registered_actions(robotId, battleContext->currentState);
    battleContext->generateState();
}

void BattleFsm::handle_GameStart(BattleContext* battleContext) {
    log("Game Start.");
    battleContext->execute_registered_actions(-1, State::GAME_START);
    battleContext->generateState();
}

void BattleFsm::handle_OperationEnterExitStage(BattleContext* battleContext) {
    log("Operation: Enter/Exit Stage.");
    battleContext->execute_registered_actions(-1, State::OPERATION_ENTER_EXIT_STAGE);
    battleContext->generateState();
}

void BattleFsm::handle_OperationChooseSkillMedicament(BattleContext* battleContext) {
    if (battleContext->m_buffer.size() <= 3 * sizeof(int)) {
        // IO层检查：数据太短，直接发错误
        battleContext->control_block_->async_write(
            battleContext->current_player_id_,
            "Error: invalid input length\n",
            battleContext, this);
        return;
    }

    int buf[4] = {0};
    memcpy(buf, battleContext->m_buffer.data(), 4 * sizeof(int));
    battleContext->m_buffer.clear();
    battleContext->is_empty = true;

    if (buf[0] < 0 || buf[0] > 1) {
        battleContext->control_block_->async_write(
            battleContext->current_player_id_,
            "Error: invalid player id\n",
            battleContext, this);
        return;
    }

    const int actor = buf[0];

    operation(battleContext, buf[0], static_cast<ActionType>(buf[1]), buf[2]);

    const bool accepted =
        battleContext->roundChoice[actor][0] == buf[1] &&
        battleContext->roundChoice[actor][1] == buf[2];

    if (accepted) {
        battleContext->operation_collected[actor] = true;
    } else {
        battleContext->control_block_->async_write(
            battleContext->current_player_id_,
            "Error: action rejected, please resubmit\n",
            battleContext, this);
        return;
    }

    if (!battleContext->has_collected_both_operations()) {
        battleContext->set_current_player(battleContext->operation_collected[0] ? 1 : 0);
        return;
    }

    {
        std::ostringstream oss;
        oss << "round choices ready: p0=(" << battleContext->roundChoice[0][0] << "," << battleContext->roundChoice[0][1]
            << ") p1=(" << battleContext->roundChoice[1][0] << "," << battleContext->roundChoice[1][1] << ")";
        log_battle_line("INFO", oss.str());
    }

    battleContext->generateState();
}

void BattleFsm::handle_OperationProtectionMechanism1(BattleContext* battleContext) {
    battleContext->execute_registered_actions(0, State::OPERATION_PROTECTION_MECHANISM_1);
    battleContext->execute_registered_actions(1, State::OPERATION_PROTECTION_MECHANISM_1);

    if (battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp <= 0)
        battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp = 1;
    if (battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp <= 0)
        battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp = 1;

    battleContext->generateState();
}

void BattleFsm::handle_OperationEnterStage(BattleContext* battleContext) {
    log("Operation: Enter Stage.");
    battleContext->execute_registered_actions(-1, State::OPERATION_ENTER_STAGE);
    battleContext->generateState();
}

void BattleFsm::handle_BattleRoundStart(BattleContext* battleContext) {
    log("Battle round start.");
    int preserved_round_choice[2][2];
    std::memcpy(preserved_round_choice, battleContext->roundChoice, sizeof(preserved_round_choice));
    battleContext->resetWorkspace();
    std::memcpy(battleContext->roundChoice, preserved_round_choice, sizeof(preserved_round_choice));
    for (int i = 0; i < 2; ++i) {
        if (!battleContext->operation_collected[i]) {
            battleContext->roundChoice[i][0] = -1;
            battleContext->roundChoice[i][1] = -1;
        }
    }
    sync_workspace_from_on_stage(battleContext);
    battleContext->execute_registered_actions(-1, State::BATTLE_ROUND_START);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstMoveRight(BattleContext* battleContext) {
    log("Battle: Determine First Move Right.");
    auto &pr = battleContext->preemptive_right;
    pr = PreemptiveRight::NONE;
    memset(battleContext->ws.preemptive_level, 0, sizeof(battleContext->ws.preemptive_level));
    battleContext->execute_registered_actions(0, State::BATTLE_FIRST_MOVE_RIGHT);
    battleContext->execute_registered_actions(1, State::BATTLE_FIRST_MOVE_RIGHT);
    // 回合类效果的先手权已经被写入ws.preemptive_level供后续使用，这里先判断是否有效果直接决定先手权
    if (pr != PreemptiveRight::NONE) {
        log("Preemptive right determined by effects.");
        battleContext->generateState();
        return;
    }
    if (battleContext->roundChoice[0][0] == battleContext->roundChoice[1][0] && battleContext->roundChoice[0][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        pr = rand() % 2 == 1 ? PreemptiveRight::SEER_ROBOT_1 : PreemptiveRight::SEER_ROBOT_2;
        log("Use medicine tie-break: " + std::string(pr == PreemptiveRight::SEER_ROBOT_1 ? "player0 wins." : "player1 wins."));
        battleContext->generateState();
        return;
    }

    if (battleContext->roundChoice[0][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = PreemptiveRight::SEER_ROBOT_2;
        log("Preemptive right determined by use medicine: player0 used medicine, player1 wins.");
        battleContext->generateState();
        return;
    }
    if (battleContext->roundChoice[1][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = PreemptiveRight::SEER_ROBOT_1;
        log("Preemptive right determined by use medicine: player1 used medicine, player0 wins.");
        battleContext->generateState();
        return;
    }
    // 都使用了技能，比较先制等级
    battleContext->ws.preemptive_level[0] += battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].skills[battleContext->ws.lastActionIndex[0]].priority;
    battleContext->ws.preemptive_level[1] += battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].skills[battleContext->ws.lastActionIndex[1]].priority;
    if (battleContext->ws.preemptive_level[0] > battleContext->ws.preemptive_level[1]) {
        pr = PreemptiveRight::SEER_ROBOT_1;
        log("Preemptive right determined by preemptive level: player0 wins.");
    } else if (battleContext->ws.preemptive_level[0] < battleContext->ws.preemptive_level[1]) {
        pr = PreemptiveRight::SEER_ROBOT_2;
        log("Preemptive right determined by preemptive level: player1 wins.");
    } else {
        // 先制等级相同，比较速度
        if (battleContext->ws.getTempAbilityValue(0, NumericalPropertyIndex::SPEED) > battleContext->ws.getTempAbilityValue(1, NumericalPropertyIndex::SPEED)) {
            pr = PreemptiveRight::SEER_ROBOT_1;
            log("Preemptive right determined by speed: player0 wins.");
        } else if (battleContext->ws.getTempAbilityValue(0, NumericalPropertyIndex::SPEED) < battleContext->ws.getTempAbilityValue(1, NumericalPropertyIndex::SPEED)) {
            pr = PreemptiveRight::SEER_ROBOT_2;
            log("Preemptive right determined by speed: player1 wins.");
        } else {
            // 速度也相同，默认先手权归 Robot 1
            // pr = PreemptiveRight::SEER_ROBOT_1;
            // log("Preemptive right determined by default: Host Robot 1 wins.");
            // 现在改为随机决定先手权，增加不确定性
            pr = rand() % 2 == 1 ? PreemptiveRight::SEER_ROBOT_1 : PreemptiveRight::SEER_ROBOT_2;
            log("Preemptive right determined by tie-break: " + std::string(pr == PreemptiveRight::SEER_ROBOT_1 ? "player0 wins." : "player1 wins."));
        }
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstActionStart(BattleContext* battleContext) {
    log("Battle: First Action Start.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    stage_action_start_abnormal_damage(battleContext, first_mover_id);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_ACTION_START);
    settle_staged_action_start_abnormal_damage(battleContext, first_mover_id);
    if (should_skip_action_flow(battleContext, first_mover_id)) {
        log("Battle: First mover skips main action flow and jumps to extra-action/death timing.");
        battleContext->currentState = State::BATTLE_FIRST_EXTRA_ACTION;
        return;
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstBeforeSkillHit(BattleContext* battleContext) {
    log("Battle: First Before Skill Hit.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_BEFORE_SKILL_HIT);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstOnSkillHit(BattleContext* battleContext) {
    log("Battle: First On Skill Hit.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    resolve_skill_execution(battleContext, first_mover_id, State::BATTLE_FIRST_ON_SKILL_HIT);
    if (battleContext->ws.skill_exec_result[first_mover_id] != SkillExecResult::SKILL_INVALID) {
        battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_ON_SKILL_HIT);
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstSkillEffect(BattleContext* battleContext) {
    log("Battle: First Skill Effect.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    if (battleContext->ws.skill_resolution_flags[first_mover_id].registerSkillEffects) {
        battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_SKILL_EFFECT);
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstAttackDamage(BattleContext* battleContext) {
    log("Battle: First Attack Damage.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    if (!battleContext->ws.skill_resolution_flags[first_mover_id].allowAttackDamagePipeline) {
        clear_damage_snapshot(battleContext->pendingDamage);
        clear_damage_snapshot(battleContext->resolvedDamage);
        battleContext->generateState();
        return;
    }

    stage_simple_attack_damage(battleContext, first_mover_id);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_ATTACK_DAMAGE);
    apply_resolved_damage(battleContext);
    battleContext->ws.has_attacked[first_mover_id] = true;
    battleContext->ws.skill_used[first_mover_id] = true;
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstAfterAction(BattleContext* battleContext) {
    log("Battle: First After Action.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    consume_selected_skill_pp(battleContext, first_mover_id);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_AFTER_ACTION);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstActionEnd(BattleContext* battleContext) {
    log("Battle: First Action End.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_ACTION_END);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstAfterActionEnd(BattleContext* battleContext) {
    log("Battle: First After Action End.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_AFTER_ACTION_END);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstExtraAction(BattleContext* battleContext) {
    log("Battle: First Extra Action.");
    const int first_mover_id = resolve_first_mover_id(battleContext);
    battleContext->execute_registered_actions(first_mover_id, State::BATTLE_FIRST_EXTRA_ACTION);
    battleContext->generateState();
}

void BattleFsm::handle_BattleFirstMoverDeath(BattleContext* battleContext) {
    log("Battle: First Mover Death.");
    battleContext->execute_registered_actions(-1, State::BATTLE_FIRST_MOVER_DEATH);
    if (field_has_on_stage_death(battleContext)) {
        log("Battle: Death occurred during first mover flow, skipping second mover flow and round end.");
        battleContext->currentState = State::BATTLE_OLD_ROUND_END_1;
        return;
    }
    battleContext->currentState = State::BATTLE_SECOND_ACTION_START;
}

void BattleFsm::handle_BattleSecondActionStart(BattleContext* battleContext) {
    log("Battle: Second Action Start.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    stage_action_start_abnormal_damage(battleContext, second_mover_id);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_ACTION_START);
    settle_staged_action_start_abnormal_damage(battleContext, second_mover_id);
    if (should_skip_action_flow(battleContext, second_mover_id)) {
        log("Battle: Second mover skips main action flow and jumps to extra-action/death timing.");
        battleContext->currentState = State::BATTLE_SECOND_EXTRA_ACTION;
        return;
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondBeforeSkillHit(BattleContext* battleContext) {
    log("Battle: Second Before Skill Hit.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_BEFORE_SKILL_HIT);
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondOnSkillHit(BattleContext* battleContext) {
    log("Battle: Second On Skill Hit.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    resolve_skill_execution(battleContext, second_mover_id, State::BATTLE_SECOND_ON_SKILL_HIT);
    if (battleContext->ws.skill_exec_result[second_mover_id] != SkillExecResult::SKILL_INVALID) {
        battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_ON_SKILL_HIT);
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondSkillEffect(BattleContext* battleContext) {
    log("Battle: Second Skill Effect.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    if (battleContext->ws.skill_resolution_flags[second_mover_id].registerSkillEffects) {
        battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_SKILL_EFFECT);
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondAttackDamage(BattleContext* battleContext) {
    log("Battle: Second Attack Damage.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    if (!battleContext->ws.skill_resolution_flags[second_mover_id].allowAttackDamagePipeline) {
        log("Second mover's skill does not allow attack damage pipeline, skipping damage stage.");
        clear_damage_snapshot(battleContext->pendingDamage);
        clear_damage_snapshot(battleContext->resolvedDamage);
        battleContext->generateState();
        return;
    }

    stage_simple_attack_damage(battleContext, second_mover_id);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_ATTACK_DAMAGE);
    apply_resolved_damage(battleContext);
    battleContext->ws.has_attacked[second_mover_id] = true;
    battleContext->ws.skill_used[second_mover_id] = true;
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondAfterAction(BattleContext* battleContext) {
    log("Battle: Second After Action.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    consume_selected_skill_pp(battleContext, second_mover_id);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_AFTER_ACTION);
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondActionEnd(BattleContext* battleContext) {
    log("Battle: Second Action End.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_ACTION_END);
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondAfterActionEnd(BattleContext* battleContext) {
    log("Battle: Second After Action End.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_AFTER_ACTION_END);
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondExtraAction(BattleContext* battleContext) {
    log("Battle: Second Extra Action.");
    const int second_mover_id = resolve_second_mover_id(battleContext);
    battleContext->execute_registered_actions(second_mover_id, State::BATTLE_SECOND_EXTRA_ACTION);
    battleContext->generateState();
}

void BattleFsm::handle_BattleRoundEnd(BattleContext* battleContext) {
    log("Battle: Round End.");
    if (battleContext->preemptive_right == PreemptiveRight::SEER_ROBOT_1) {
        battleContext->execute_registered_actions(1, State::BATTLE_ROUND_END);
        battleContext->execute_registered_actions(0, State::BATTLE_ROUND_END);
    } else if (battleContext->preemptive_right == PreemptiveRight::SEER_ROBOT_2) {
        battleContext->execute_registered_actions(0, State::BATTLE_ROUND_END);
        battleContext->execute_registered_actions(1, State::BATTLE_ROUND_END);
    } else {
        battleContext->execute_registered_actions(-1, State::BATTLE_ROUND_END);
    }
    battleContext->generateState();
}

void BattleFsm::handle_BattleSecondMoverDeath(BattleContext* battleContext) {
    log("Battle: Second Mover Death.");
    battleContext->execute_registered_actions(-1, State::BATTLE_SECOND_MOVER_DEATH);
    battleContext->generateState();
}

void BattleFsm::handle_BattleOldRoundEnd1(BattleContext* battleContext) {
    log("Battle: Old Round End 1.");
    battleContext->execute_registered_actions(-1, State::BATTLE_OLD_ROUND_END_1);
    battleContext->generateState();
}

void BattleFsm::handle_BattleRoundReductionAllRoundMinus(BattleContext* battleContext) {
    log("Battle: Round Reduction All Round Minus.");
    // 先执行注册在本时点的效果（包括断回合效果本身）
    battleContext->execute_registered_actions(-1, State::BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS);
    // 然后统一清理所有已过期的回合类效果
    battleContext->cleanup_expired_effects();
    battleContext->generateState();
}

void BattleFsm::handle_BattleRoundReductionNewRoundEnd(BattleContext* battleContext) {
    log("Battle: Round Reduction New Round End.");
    battleContext->execute_registered_actions(-1, State::BATTLE_ROUND_REDUCTION_NEW_ROUND_END);
    battleContext->generateState();
}

void BattleFsm::handle_BattleOldRoundEnd2(BattleContext* battleContext) {
    log("Battle: Old Round End 2.");
    battleContext->execute_registered_actions(-1, State::BATTLE_OLD_ROUND_END_2);
    battleContext->generateState();
}

void BattleFsm::handle_BattleDeathTiming(BattleContext* battleContext) {
    log("Battle: Death Timing.");
    battleContext->execute_registered_actions(-1, State::BATTLE_DEATH_TIMING);
    battleContext->generateState();
}

void BattleFsm::handle_BattleDefeatStatus(BattleContext* battleContext) {
    log("Battle: Defeat Status.");
    battleContext->execute_registered_actions(-1, State::BATTLE_DEFEAT_STATUS);
    battleContext->generateState();
}

void BattleFsm::handle_BattleOpponentDefeatStatus(BattleContext* battleContext) {
    log("Battle: Opponent Defeat Status.");
    battleContext->execute_registered_actions(-1, State::BATTLE_OPPONENT_DEFEAT_STATUS);
    battleContext->generateState();
}

void BattleFsm::handle_BattleNewDefeatMechanism(BattleContext* battleContext) {
    log("Battle: New Defeat Mechanism.");
    battleContext->execute_registered_actions(-1, State::BATTLE_NEW_DEFEAT_MECHANISM);
    battleContext->generateState();
}

void BattleFsm::handle_OperationProtectionMechanism2(BattleContext* battleContext) {
    log("Operation: Protection Mechanism 2.");
    battleContext->execute_registered_actions(-1, State::OPERATION_PROTECTION_MECHANISM_2);
    battleContext->generateState();
}

void BattleFsm::handle_BattleAfterDefeated(BattleContext* battleContext) {
    log("Battle: After Defeated.");
    battleContext->execute_registered_actions(-1, State::BATTLE_AFTER_DEFEATED);

    const bool dead0 = battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp <= 0;
    const bool dead1 = battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp <= 0;

    if (!dead0 && !dead1) {
        battleContext->currentState = State::BATTLE_AFTER_DEFEATING_OPPONENT;
        return;
    }

    battleContext->set_current_player(dead0 ? 0 : 1);
    battleContext->generateState();
}

void BattleFsm::handle_ChooseAfterDeath(BattleContext* battleContext) {
    log("Battle: Choose After Death.");
    if (battleContext->m_buffer.size() <= 3 * sizeof(int)) {
        battleContext->control_block_->async_write(
            battleContext->current_player_id_,
            "Error: invalid input\n",
            battleContext, this);
        return;
    }

    int buf[4] = {0};
    memcpy(buf, battleContext->m_buffer.data(), 4 * sizeof(int));
    battleContext->m_buffer.clear();
    battleContext->is_empty = true;

    if (buf[3] == 1) {
        return;
    }
    if (static_cast<ActionType>(buf[1]) != ActionType::CHOOSE_PET) {
        battleContext->control_block_->async_write(
            battleContext->current_player_id_,
            "Error: must choose pet\n",
            battleContext, this);
        return;
    }
    operation(battleContext, buf[0], static_cast<ActionType>(buf[1]), buf[2]);
    battleContext->generateState();
}

void BattleFsm::handle_BattleAfterDefeatingOpponent(BattleContext* battleContext) {
    log("Battle: After Defeating Opponent.");
    battleContext->execute_registered_actions(-1, State::BATTLE_AFTER_DEFEATING_OPPONENT);
    battleContext->generateState();
}

void BattleFsm::handle_BattleRoundCompletion(BattleContext* battleContext) {
    log("Battle: Round Completion.");
    battleContext->execute_registered_actions(-1, State::BATTLE_ROUND_COMPLETION);
    battleContext->advanceRound();
    battleContext->reset_operation_collection();
    battleContext->roundChoice[0][0] = -1;
    battleContext->roundChoice[0][1] = -1;
    battleContext->roundChoice[1][0] = -1;
    battleContext->roundChoice[1][1] = -1;
    battleContext->set_current_player(0);
    battleContext->currentState = State::OPERATION_CHOOSE_SKILL_MEDICAMENT;
}

void BattleFsm::handle_Finished(BattleContext* battleContext) {
    log("Battle: Finished.");
}
