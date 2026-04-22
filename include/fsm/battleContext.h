#ifndef __battleContext_H
#define __battleContext_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <array>
#include <map>

#include <entities/seer-robot.h>
#include <fsm/battleWorkspace.h>
#include <effects/continuousEffect.h>
#include <entities/soul_mark.h>

// Forward declarations
class BattleFsm;
class IControlBlock;

enum class State {
// 操作阶段
GAME_START,
OPERATION_ENTER_EXIT_STAGE,
OPERATION_CHOOSE_SKILL_MEDICAMENT,
OPERATION_PROTECTION_MECHANISM_1,
OPERATION_ENTER_STAGE,

// 战斗阶段
BATTLE_ROUND_START,
BATTLE_FIRST_MOVE_RIGHT,
BATTLE_FIRST_ACTION_START,
BATTLE_FIRST_BEFORE_SKILL_HIT,
BATTLE_FIRST_ON_SKILL_HIT,
BATTLE_FIRST_SKILL_EFFECT,
BATTLE_FIRST_ATTACK_DAMAGE,
BATTLE_FIRST_AFTER_ACTION,
BATTLE_FIRST_ACTION_END,
BATTLE_FIRST_AFTER_ACTION_END,
BATTLE_FIRST_EXTRA_ACTION,
BATTLE_FIRST_MOVER_DEATH,
BATTLE_SECOND_ACTION_START,
BATTLE_SECOND_BEFORE_SKILL_HIT,
BATTLE_SECOND_ON_SKILL_HIT,
BATTLE_SECOND_SKILL_EFFECT,
BATTLE_SECOND_ATTACK_DAMAGE,
BATTLE_SECOND_AFTER_ACTION,
BATTLE_SECOND_ACTION_END,
BATTLE_SECOND_AFTER_ACTION_END,
BATTLE_SECOND_EXTRA_ACTION,
BATTLE_ROUND_END,
BATTLE_SECOND_MOVER_DEATH,
BATTLE_OLD_ROUND_END_1,
BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS,
BATTLE_ROUND_REDUCTION_NEW_ROUND_END,
BATTLE_OLD_ROUND_END_2,
BATTLE_DEATH_TIMING,
BATTLE_DEFEAT_STATUS,
BATTLE_OPPONENT_DEFEAT_STATUS,
BATTLE_NEW_DEFEAT_MECHANISM,
OPERATION_PROTECTION_MECHANISM_2,
BATTLE_AFTER_DEFEATED,
CHOOSE_AFTER_DEATH,
BATTLE_AFTER_DEFEATING_OPPONENT,
BATTLE_ROUND_COMPLETION,
FINISHED
};

class BattleContext {
public:
    //--- 持久化核心数据 ---
    SeerRobot seerRobot[2];
    int on_stage[2];
    int roundCount;
    unsigned int uuid;
    State currentState;
    BattleFsm* m_fsm;

    //--- 临时工作层（每回合重置）---
    BattleWorkspace ws;

    //--- 效果执行表 ---
    std::unordered_map<State, std::array<std::vector<std::unique_ptr<ContinuousEffect>>, 2>> effects;

    //--- 被动效果表 ---
    std::array<std::map<int, ContinuousEffect*>, 2> passiveEffects;

    //--- 全局有效ID（回合效果过期机制）---
    int validIds_[2] = {0, 0};

    //--- 网络缓冲 ---
    std::vector<char> m_buffer;
    bool is_empty = true;

    //--- 控制块 ---
    IControlBlock* control_block_;
    int current_player_id_;  // 当前等待输入的玩家

    //--- 操作日志 ---
    std::string operation_log_;

    //--- 错误处理 ---
    static constexpr int MAX_ATTEMPTS = 3;
    int failed_attempts;

    //--- 便利引用 ---
    int (&roundChoice)[2][2];
    int (&lastActionType)[2];
    int (&lastActionIndex)[2];
    PreemptiveRight& preemptive_right;
    int (&damage_reduce_add)[2][4];
    int (&damage_reduce_mul)[2][4];
    DamageSnapshot& pendingDamage;
    DamageSnapshot& resolvedDamage;

    //--- 构造函数 ---
    BattleContext(IControlBlock* control_block, const SeerRobot robots[]);
    BattleContext() = delete;
    BattleContext(const BattleContext&) = delete;
    ~BattleContext();

    //--- 基础方法 ---
    void init_battle();

    //--- 状态控制 ---
    bool need_input();
    void generateState();
    void back_to_last_state();

    //--- Workspace ---
    void resetWorkspace() { ws.reset(); }

    //--- 效果注册 ---
    void registerEffect(State trigger, int owner, std::unique_ptr<ContinuousEffect> effect);
    void registerPassiveEffect(int owner, int effectId, ContinuousEffect* effect);
    void invalidateRoundEffects(int robotId) { ++validIds_[robotId]; }

    //--- 效果执行 ---
    void execute_registered_actions(int robotId, State state);

    //--- 效果查询 ---
    template<int EffectId>
    bool hasEffect(State trigger, int owner) const;

    template<int EffectId>
    bool consumeEffect(State trigger, int opponent);

    //--- 回合结束 ---
    void onRoundEnd();

    //--- 清空效果 ---
    void clearAllEffects() { effects.clear(); }

    //--- 日志 ---
    void log_operation(const std::string& op) {
#ifdef BATTLE_OP_LOGGING
        operation_log_ += op;
        operation_log_ += "|";
#endif
    }

    std::string get_operation_log() const { return operation_log_; }
    void clear_operation_log() { operation_log_.clear(); }

    //--- 便利方法 ---
    ElfPet& getPet(int robotId) { return seerRobot[robotId].elfPets[on_stage[robotId]]; }
    int opponent(int robotId) const { return 1 - robotId; }

    //--- 设置当前玩家 ---
    void set_current_player(int player_id) { current_player_id_ = player_id; }
};

// 显式实例化模板
extern template bool BattleContext::hasEffect<0>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<1>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<2>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<3>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<4>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<10>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<11>(State trigger, int owner) const;
extern template bool BattleContext::hasEffect<12>(State trigger, int owner) const;

extern template bool BattleContext::consumeEffect<0>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<1>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<2>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<3>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<4>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<10>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<11>(State trigger, int opponent);
extern template bool BattleContext::consumeEffect<12>(State trigger, int opponent);

#endif
