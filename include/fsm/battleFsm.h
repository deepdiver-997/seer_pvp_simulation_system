#ifndef BATTLEFSM_H
#define BATTLEFSM_H
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <boost/asio.hpp>
#include <entities/seer-robot.h>
#include <fsm/battleContext.h>

class BattleFsm {
    enum class ActionType {
        CHOOSE_PET,

        SELECT_SKILL,

        USE_MEDICINE,

        SEND_EMOJI
    };
public:

    BattleFsm() = default;
    BattleFsm(const BattleFsm&) = delete;
    BattleFsm& operator=(const BattleFsm&) = delete;
    BattleFsm(BattleFsm&&) = default;
    BattleFsm& operator=(BattleFsm&&) = default;
    ~BattleFsm();

    using HandlerType = void (BattleFsm::*)(std::shared_ptr<BattleContext>);

    void initHandler();

    void addStateAction(State state, HandlerType action);

    /*
    * @brief Getting input
    *
    * @param robotId 
    */
    void operation(std::shared_ptr<BattleContext> battleContext, int robotId, ActionType actionType, int index); //method for getting input, robotId stands for operator, actionType clarifies the action you've chosen
    void operation();
    void run(std::shared_ptr<BattleContext> battleContext);
    void log(const std::string& message);
    bool id_debug = true;

private:
    std::unordered_map<State, HandlerType> stateHandlerMap;

    // State handlers
    void General_Handler(std::shared_ptr<BattleContext> battleContext);
    void handle_GameStart(std::shared_ptr<BattleContext> battleContext);
    void handle_OperationEnterExitStage(std::shared_ptr<BattleContext> battleContext);
    void handle_OperationChooseSkillMedicament(std::shared_ptr<BattleContext> battleContext);
    void handle_OperationProtectionMechanism1(std::shared_ptr<BattleContext> battleContext);
    void handle_OperationEnterStage(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleRoundStart(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstMoveRight(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstActionStart(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstBeforeSkillHit(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstOnSkillHit(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstSkillEffect(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstAttackDamage(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstAfterAction(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstActionEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstAfterActionEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstExtraAction(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleFirstMoverDeath(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondActionStart(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondBeforeSkillHit(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondOnSkillHit(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondSkillEffect(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondAttackDamage(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondAfterAction(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondActionEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondAfterActionEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondExtraAction(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleRoundEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleSecondMoverDeath(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleOldRoundEnd1(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleRoundReductionAllRoundMinus(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleRoundReductionNewRoundEnd(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleOldRoundEnd2(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleDeathTiming(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleDefeatStatus(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleOpponentDefeatStatus(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleNewDefeatMechanism(std::shared_ptr<BattleContext> battleContext);
    void handle_OperationProtectionMechanism2(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleAfterDefeated(std::shared_ptr<BattleContext> battleContext);
    void handle_ChooseAfterDeath(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleAfterDefeatingOpponent(std::shared_ptr<BattleContext> battleContext);
    void handle_BattleRoundCompletion(std::shared_ptr<BattleContext> battleContext);
    void handle_Finished(std::shared_ptr<BattleContext> battleContext);
};

void BattleFsm::initHandler() {
    // Initialize state handlers
    stateHandlerMap[State::GAME_START] = &BattleFsm::handle_GameStart;
    stateHandlerMap[State::OPERATION_ENTER_EXIT_STAGE] = &BattleFsm::handle_OperationEnterExitStage;
    // Add other state handlers as needed
}

void BattleFsm::operation(std::shared_ptr<BattleContext> battleContext, int robotId, ActionType actionType, int index) {
    int &on_stage = battleContext->on_stage[robotId];
    if (on_stage < 0 || on_stage >= 6) {
        std::cerr << "Invalid on-stage pet index: " << on_stage << std::endl;
        return;
    }
    auto &robot = battleContext->seerRobot[robotId];
    auto &pet = robot.elfPets[on_stage];
    auto &actions = battleContext->stateActions[robotId];

    switch (actionType)
    {
        case ActionType::SELECT_SKILL:
            // Handle skill selection
            if (index < 0 || index >= 5) {
                std::cerr << "Invalid skill index: " << index << std::endl;
                return;
            }
            if (pet.skills[index].skill_usable()) {
                battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::SELECT_SKILL);
                battleContext->roundChoice[robotId][1] = index;
            }
            else {
                std::cerr << "Skill not usable: " << pet.skills[index].name << std::endl;
            }
            break;
        case ActionType::USE_MEDICINE:
            // Handle medicine usage
            (robot.use_medicine(pet, index) ? std::cout << "Used medicine: " << robot.medicines[index] << std::endl : std::cerr << "Cannot use medicine: " << robot.medicines[index] << std::endl);
            battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::USE_MEDICINE);
            battleContext->roundChoice[robotId][1] = index;
            break;
        case ActionType::CHOOSE_PET:
            // Handle pet selection
            if (index < 0 || index >= 6) {
                std::cerr << "Invalid pet index: " << index << std::endl;
                return;
            }
            if (pet.hp > 0 && pet.is_locked == false) {
                battleContext->roundChoice[robotId][0] = static_cast<int>(ActionType::CHOOSE_PET);
                battleContext->roundChoice[robotId][1] = index;
                // actions[State::OPERATION_ENTER_EXIT_STAGE].push_back(Effect(5, 0, robotId, 1, /*[robotId, index, &on_stage, this](std::shared_ptr<BattleContext> battleContext) {
                //     on_stage = index;
                //     (this->*stateHandlerMap[State::BATTLE_ROUND_START])(battleContext);
                //     battleContext->currentState = State::BATTLE_FIRST_MOVER_DEATH; // Move to the next state after choosing a pet
                // } 应该使用记录了的专门的处理函数 可以对比查询*/ nullptr));
            } else {
                std::cerr << "Cannot choose this pet: " << pet.name << std::endl;
            }
            break;
        default:
            break;
    }
}

void BattleFsm::operation() {
    // Handle operation without parameters
    std::cout << "Operation without parameters." << std::endl;
}

void BattleFsm::run(std::shared_ptr<BattleContext> battleContext) {

    while(true) {
        if(battleContext->currentState < State::BATTLE_ROUND_COMPLETION && battleContext->currentState >= State::GAME_START) {
            (this->*stateHandlerMap[battleContext->currentState])(battleContext);
            battleContext->currentState = static_cast<State>(static_cast<int>(battleContext->currentState) + 1);
        // } else if (battleContext->currentState == State::BATTLE_ROUND_COMPLETION) {
        //     if(battleContext->seerRobot1.allive() == 0 || battleContext->seerRobot2.allive() == 0)
        //         return;
        //     (this->*stateHandlerMap[battleContext->currentState])(battleContext);
        //     battleContext->roundCount++;
        //     battleContext->currentState = State::GAME_START;  // Reset to start after completion
        } else if(battleContext->currentState == State::FINISHED) {
            return;
        } else {
            std::cerr << "Invalid state: " << static_cast<int>(battleContext->currentState) << std::endl;
            return;
        }
    }
}

void BattleFsm::log(const std::string& message) {
    if (id_debug) {
        std::cout << "DEBUG: " << message << std::endl;
    }
}

void BattleFsm::General_Handler(std::shared_ptr<BattleContext> battleContext) {
    battleContext->execute_registered_actions(0, battleContext->currentState);
    battleContext->execute_registered_actions(1, battleContext->currentState);
    battleContext->generateState();
}

void BattleFsm::handle_GameStart(std::shared_ptr<BattleContext> battleContext) {
    log("Game Start.");
}

void BattleFsm::handle_OperationEnterExitStage(std::shared_ptr<BattleContext> battleContext) {
    log("Operation: Enter/Exit Stage.");
}

void BattleFsm::handle_OperationChooseSkillMedicament(std::shared_ptr<BattleContext> battleContext) {
    battleContext->async_read([this](std::shared_ptr<BattleContext> battleContext) {
        if (battleContext->m_buffer.size() != 3 * sizeof(int)) {
            battleContext->async_write("Error info\n", [](std::shared_ptr<BattleContext> battleContext) {
                battleContext->currentState = State::OPERATION_ENTER_EXIT_STAGE; // 回退到上一步 在run循环里重新执行一次这个阶段的处理函数
            });
        }
        else {
            int buf[3];
            memcpy(buf, battleContext->m_buffer.data(), 3 * sizeof(int));
            operation(battleContext, buf[0], static_cast<ActionType>(buf[1]), buf[2]);
        }
    });
}

void BattleFsm::handle_OperationProtectionMechanism1(std::shared_ptr<BattleContext> battleContext) {
    battleContext->execute_registered_actions(1, State::OPERATION_PROTECTION_MECHANISM_1);
    battleContext->execute_registered_actions(2, State::OPERATION_PROTECTION_MECHANISM_1);

   if(battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp <= 0)
         battleContext->seerRobot[0].elfPets[battleContext->on_stage[0]].hp = 1;
    if(battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp <= 0)
         battleContext->seerRobot[1].elfPets[battleContext->on_stage[1]].hp = 1;

    battleContext->generateState();
}

void BattleFsm::handle_OperationEnterStage(std::shared_ptr<BattleContext> battleContext) {
    log("Operation: Enter Stage.");
}

void BattleFsm::handle_BattleRoundStart(std::shared_ptr<BattleContext> battleContext) {
    log("Battle round start.");
}

void BattleFsm::handle_BattleFirstMoveRight(std::shared_ptr<BattleContext> battleContext) {
    log("Battle: First Move Right.");
}

void BattleFsm::handle_BattleFirstActionStart(std::shared_ptr<BattleContext> battleContext) {
    log("Battle: First Action Start.");
}

void BattleFsm::handle_BattleFirstBeforeSkillHit(std::shared_ptr<BattleContext> battleContext) {
    log("Battle: First Before Skill Hit.");
}

void BattleFsm::handle_BattleFirstOnSkillHit(std::shared_ptr<BattleContext> battleContext) {
    log("Battle: First On Skill Hit.");
}

void BattleFsm::handle_BattleFirstSkillEffect(std::shared_ptr<BattleContext> battleContext) {
    log("Battle: First Skill Effect.");
}

#endif // BATTLEFSM_H