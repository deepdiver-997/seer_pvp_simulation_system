#ifndef BATTLEFSM_H
#define BATTLEFSM_H

#include <memory>
#include <map>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <entities/seer-robot.h>
#include <thread_pool/boost_thread_pool.h>

class BattleContext;
enum class State;

// Forward declare for callback type
class BattleFsm;

class BattleFsm {
public:
    enum class ActionType {
        CHOOSE_PET,
        SELECT_SKILL,
        USE_MEDICINE,
        SEND_EMOJI
    };

    BattleFsm(bool enable_debug = true);
    BattleFsm(const BattleFsm&) = delete;
    BattleFsm& operator=(const BattleFsm&) = delete;
    BattleFsm(BattleFsm&&) = delete;
    BattleFsm& operator=(BattleFsm&&) = delete;
    ~BattleFsm();

    using HandlerType = void (BattleFsm::*)(BattleContext*);

    void initHandler();
    void addStateAction(State state, HandlerType action);

    // 使用 raw pointer，context 由 control block 持有
    void run(BattleContext* battleContext);
    void operation(BattleContext* battleContext, int robotId, ActionType actionType, int index);
    void log(const std::string& message);

    bool id_debug = true;

    // 战斗线程池（由 Server 注入）
    std::shared_ptr<BoostThreadPool> battle_pool_;

    // Battle pool 用于继续执行
    void post(std::function<void()> task);

private:
    std::unordered_map<State, HandlerType> stateHandlerMap;

    bool runInternal(BattleContext* battleContext);

    void General_Handler(BattleContext* battleContext, int robotId);
    void handle_GameStart(BattleContext* battleContext);
    void handle_OperationEnterExitStage(BattleContext* battleContext);
    void handle_OperationChooseSkillMedicament(BattleContext* battleContext);
    void handle_OperationProtectionMechanism1(BattleContext* battleContext);
    void handle_OperationEnterStage(BattleContext* battleContext);
    void handle_BattleRoundStart(BattleContext* battleContext);
    void handle_BattleFirstMoveRight(BattleContext* battleContext);
    void handle_BattleFirstActionStart(BattleContext* battleContext);
    void handle_BattleFirstBeforeSkillHit(BattleContext* battleContext);
    void handle_BattleFirstOnSkillHit(BattleContext* battleContext);
    void handle_BattleFirstSkillEffect(BattleContext* battleContext);
    void handle_BattleFirstAttackDamage(BattleContext* battleContext);
    void handle_BattleFirstAfterAction(BattleContext* battleContext);
    void handle_BattleFirstActionEnd(BattleContext* battleContext);
    void handle_BattleFirstAfterActionEnd(BattleContext* battleContext);
    void handle_BattleFirstExtraAction(BattleContext* battleContext);
    void handle_BattleFirstMoverDeath(BattleContext* battleContext);
    void handle_BattleSecondActionStart(BattleContext* battleContext);
    void handle_BattleSecondBeforeSkillHit(BattleContext* battleContext);
    void handle_BattleSecondOnSkillHit(BattleContext* battleContext);
    void handle_BattleSecondSkillEffect(BattleContext* battleContext);
    void handle_BattleSecondAttackDamage(BattleContext* battleContext);
    void handle_BattleSecondAfterAction(BattleContext* battleContext);
    void handle_BattleSecondActionEnd(BattleContext* battleContext);
    void handle_BattleSecondAfterActionEnd(BattleContext* battleContext);
    void handle_BattleSecondExtraAction(BattleContext* battleContext);
    void handle_BattleRoundEnd(BattleContext* battleContext);
    void handle_BattleSecondMoverDeath(BattleContext* battleContext);
    void handle_BattleOldRoundEnd1(BattleContext* battleContext);
    void handle_BattleRoundReductionAllRoundMinus(BattleContext* battleContext);
    void handle_BattleRoundReductionNewRoundEnd(BattleContext* battleContext);
    void handle_BattleOldRoundEnd2(BattleContext* battleContext);
    void handle_BattleDeathTiming(BattleContext* battleContext);
    void handle_BattleDefeatStatus(BattleContext* battleContext);
    void handle_BattleOpponentDefeatStatus(BattleContext* battleContext);
    void handle_BattleNewDefeatMechanism(BattleContext* battleContext);
    void handle_OperationProtectionMechanism2(BattleContext* battleContext);
    void handle_BattleAfterDefeated(BattleContext* battleContext);
    void handle_ChooseAfterDeath(BattleContext* battleContext);
    void handle_BattleAfterDefeatingOpponent(BattleContext* battleContext);
    void handle_BattleRoundCompletion(BattleContext* battleContext);
    void handle_Finished(BattleContext* battleContext);
};

#endif // BATTLEFSM_H
