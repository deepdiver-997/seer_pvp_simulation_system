#include <fsm/battleFsm.h>
#include <fsm/battleContext.h>
#include <iostream>

BattleFsm::BattleFsm(bool enable_debug)
    : id_debug(enable_debug) {
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
        HandlerType handler = it->second;
        (this->*handler)(battleContext);
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
            if (pet.hp > 0 && pet.is_locked == false) {
                battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::CHOOSE_PET);
                battleContext->roundChoice[robotId][1] = index;
            } else {
                std::cerr << "Cannot choose this pet: " << pet.name << std::endl;
            }
            break;
        default:
            break;
    }
}

void BattleFsm::log(const std::string& message) {
    if (id_debug) {
        std::cout << "DEBUG: " << message << std::endl;
    }
}

void BattleFsm::post(std::function<void()> task) {
    // 由控制块设置 battle_pool_
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
}

void BattleFsm::handle_OperationEnterExitStage(BattleContext* battleContext) {
    log("Operation: Enter/Exit Stage.");
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

    operation(battleContext, buf[0], static_cast<ActionType>(buf[1]), buf[2]);
    battleContext->generateState();
}

void BattleFsm::handle_OperationProtectionMechanism1(BattleContext* battleContext) {
    battleContext->execute_registered_actions(1, State::OPERATION_PROTECTION_MECHANISM_1);
    battleContext->execute_registered_actions(2, State::OPERATION_PROTECTION_MECHANISM_1);

    if (battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp <= 0)
        battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp = 1;
    if (battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp <= 0)
        battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp = 1;

    battleContext->generateState();
}

void BattleFsm::handle_OperationEnterStage(BattleContext* battleContext) {
    log("Operation: Enter Stage.");
}

void BattleFsm::handle_BattleRoundStart(BattleContext* battleContext) {
    log("Battle round start.");
}

void BattleFsm::handle_BattleFirstMoveRight(BattleContext* battleContext) {
    auto &pr = battleContext->preemptive_right;
    pr = PreemptiveRight::NONE;
    battleContext->execute_registered_actions(0, State::BATTLE_FIRST_MOVE_RIGHT);
    battleContext->execute_registered_actions(1, State::BATTLE_FIRST_MOVE_RIGHT);
    ElfPet &p1 = battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]];
    ElfPet &p2 = battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]];
    if (battleContext->roundChoice[0][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = PreemptiveRight::SEER_ROBOT_2;
        return;
    }
    if (battleContext->roundChoice[1][0] == static_cast<int>(ActionType::USE_MEDICINE)) {
        battleContext->preemptive_right = PreemptiveRight::SEER_ROBOT_1;
        return;
    }
}

void BattleFsm::handle_BattleFirstActionStart(BattleContext* battleContext) {
    log("Battle: First Action Start.");
}

void BattleFsm::handle_BattleFirstBeforeSkillHit(BattleContext* battleContext) {
    log("Battle: First Before Skill Hit.");
}

void BattleFsm::handle_BattleFirstOnSkillHit(BattleContext* battleContext) {
    log("Battle: First On Skill Hit.");
}

void BattleFsm::handle_BattleFirstSkillEffect(BattleContext* battleContext) {
    log("Battle: First Skill Effect.");
}

void BattleFsm::handle_BattleFirstAttackDamage(BattleContext* battleContext) {
    log("Battle: First Attack Damage.");
}

void BattleFsm::handle_BattleFirstAfterAction(BattleContext* battleContext) {
    log("Battle: First After Action.");
}

void BattleFsm::handle_BattleFirstActionEnd(BattleContext* battleContext) {
    log("Battle: First Action End.");
}

void BattleFsm::handle_BattleFirstAfterActionEnd(BattleContext* battleContext) {
    log("Battle: First After Action End.");
}

void BattleFsm::handle_BattleFirstExtraAction(BattleContext* battleContext) {
    log("Battle: First Extra Action.");
}

void BattleFsm::handle_BattleFirstMoverDeath(BattleContext* battleContext) {
    log("Battle: First Mover Death.");
}

void BattleFsm::handle_BattleSecondActionStart(BattleContext* battleContext) {
    log("Battle: Second Action Start.");
}

void BattleFsm::handle_BattleSecondBeforeSkillHit(BattleContext* battleContext) {
    log("Battle: Second Before Skill Hit.");
}

void BattleFsm::handle_BattleSecondOnSkillHit(BattleContext* battleContext) {
    log("Battle: Second On Skill Hit.");
}

void BattleFsm::handle_BattleSecondSkillEffect(BattleContext* battleContext) {
    log("Battle: Second Skill Effect.");
}

void BattleFsm::handle_BattleSecondAttackDamage(BattleContext* battleContext) {
    log("Battle: Second Attack Damage.");
}

void BattleFsm::handle_BattleSecondAfterAction(BattleContext* battleContext) {
    log("Battle: Second After Action.");
}

void BattleFsm::handle_BattleSecondActionEnd(BattleContext* battleContext) {
    log("Battle: Second Action End.");
}

void BattleFsm::handle_BattleSecondAfterActionEnd(BattleContext* battleContext) {
    log("Battle: Second After Action End.");
}

void BattleFsm::handle_BattleSecondExtraAction(BattleContext* battleContext) {
    log("Battle: Second Extra Action.");
}

void BattleFsm::handle_BattleRoundEnd(BattleContext* battleContext) {
    log("Battle: Round End.");
}

void BattleFsm::handle_BattleSecondMoverDeath(BattleContext* battleContext) {
    log("Battle: Second Mover Death.");
}

void BattleFsm::handle_BattleOldRoundEnd1(BattleContext* battleContext) {
    log("Battle: Old Round End 1.");
}

void BattleFsm::handle_BattleRoundReductionAllRoundMinus(BattleContext* battleContext) {
    log("Battle: Round Reduction All Round Minus.");
}

void BattleFsm::handle_BattleRoundReductionNewRoundEnd(BattleContext* battleContext) {
    log("Battle: Round Reduction New Round End.");
}

void BattleFsm::handle_BattleOldRoundEnd2(BattleContext* battleContext) {
    log("Battle: Old Round End 2.");
}

void BattleFsm::handle_BattleDeathTiming(BattleContext* battleContext) {
    log("Battle: Death Timing.");
}

void BattleFsm::handle_BattleDefeatStatus(BattleContext* battleContext) {
    log("Battle: Defeat Status.");
}

void BattleFsm::handle_BattleOpponentDefeatStatus(BattleContext* battleContext) {
    log("Battle: Opponent Defeat Status.");
}

void BattleFsm::handle_BattleNewDefeatMechanism(BattleContext* battleContext) {
    log("Battle: New Defeat Mechanism.");
}

void BattleFsm::handle_OperationProtectionMechanism2(BattleContext* battleContext) {
    log("Operation: Protection Mechanism 2.");
}

void BattleFsm::handle_BattleAfterDefeated(BattleContext* battleContext) {
    log("Battle: After Defeated.");
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
}

void BattleFsm::handle_BattleRoundCompletion(BattleContext* battleContext) {
    log("Battle: Round Completion.");
}

void BattleFsm::handle_Finished(BattleContext* battleContext) {
    log("Battle: Finished.");
}
