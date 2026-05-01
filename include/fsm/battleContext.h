#ifndef __battleContext_H
#define __battleContext_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <array>
#include <map>
#include <algorithm>
#include <mutex>

#include <abnormal-system/abnormal-types.h>
#include <entities/seer-robot.h>
#include <fsm/battleWorkspace.h>
#include <effects/continuousEffect.h>
#include <effects/pendingEffect.h>
#include <entities/soul_mark.h>

// Forward declarations
class BattleFsm;
class IControlBlock;

/**
 * BreakCallback — 被断回合补偿回调
 *
 * 当精灵的回合类效果被对手断回合时触发。
 * 与关联的回合效果同持续回合，但存在独立的 map 中，不受断回合影响。
 * 减扣点时统一清理过期的回调。
 */
struct BreakCallback {
    int register_round;                             // 注册时的回合
    int duration_rounds;                            // 持续回合（和关联的回合效果一致）
    std::function<void(class BattleContext*)> fn;   // 补偿逻辑

    bool is_expired(int current_round) const {
        return current_round - register_round >= duration_rounds;
    }
};

enum class EffectContainer {
    Skill,
    SoulMark,
};

enum class State {
// 操作阶段
GAME_START = -1,                        // 游戏开始时
OPERATION_ENTER_EXIT_STAGE,        // 出战时/下场时
OPERATION_CHOOSE_SKILL_MEDICAMENT, // 选择技能/药剂
OPERATION_PROTECTION_MECHANISM_1,  // 保护机制1
OPERATION_ENTER_STAGE,             // 登场时

// 战斗阶段
BATTLE_ROUND_START,                        // 回合开始时
BATTLE_FIRST_MOVE_RIGHT,                   // 双方先手权结算
BATTLE_FIRST_ACTION_START,                 // 先手方行动开始时
BATTLE_FIRST_BEFORE_SKILL_HIT,             // 先手方技能命中前
BATTLE_FIRST_ON_SKILL_HIT,                 // 先手方技能命中时
BATTLE_FIRST_SKILL_EFFECT,                 // 先手方技能效果结算
BATTLE_FIRST_ATTACK_DAMAGE,                // 先手方攻击伤害结算
BATTLE_FIRST_AFTER_ACTION,                 // 先手方行动后
BATTLE_FIRST_ACTION_END,                   // 先手方行动结束时
BATTLE_FIRST_AFTER_ACTION_END,             // 先手方行动结束后
BATTLE_FIRST_EXTRA_ACTION,                 // 先手方额外行动
BATTLE_FIRST_MOVER_DEATH,                  // 先手方死亡结算
BATTLE_SECOND_ACTION_START,                // 后手方行动开始时
BATTLE_SECOND_BEFORE_SKILL_HIT,            // 后手方技能命中前
BATTLE_SECOND_ON_SKILL_HIT,                // 后手方技能命中时
BATTLE_SECOND_SKILL_EFFECT,                // 后手方技能效果结算
BATTLE_SECOND_ATTACK_DAMAGE,               // 后手方攻击伤害结算
BATTLE_SECOND_AFTER_ACTION,                // 后手方行动后
BATTLE_SECOND_ACTION_END,                  // 后手方行动结束时
BATTLE_SECOND_AFTER_ACTION_END,            // 后手方行动结束后
BATTLE_SECOND_EXTRA_ACTION,                // 后手方额外行动
BATTLE_ROUND_END,                          // 回合结束时
BATTLE_SECOND_MOVER_DEATH,                 // 后手方死亡结算
BATTLE_OLD_ROUND_END_1,                    // 旧版回合结束后1
BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS,    // 回合扣减点
BATTLE_ROUND_REDUCTION_NEW_ROUND_END,      // 新版回合结束后
BATTLE_OLD_ROUND_END_2,                    // 旧版回合结束后2
BATTLE_DEATH_TIMING,                       // 死亡时点
BATTLE_DEFEAT_STATUS,                      // 新版未被击败/被击败时
BATTLE_OPPONENT_DEFEAT_STATUS,             // 新版未击败对手/击败对手时
BATTLE_NEW_DEFEAT_MECHANISM,               // 新版保护机制
OPERATION_PROTECTION_MECHANISM_2,          // 保护机制2
BATTLE_AFTER_DEFEATED,                     // 被击败后
CHOOSE_AFTER_DEATH,                        // 死后选择
BATTLE_AFTER_DEFEATING_OPPONENT,           // 击败对手后
BATTLE_ROUND_COMPLETION,                   // 回合完成
FINISHED                                   // 战斗结束
};

inline const char* state_name_cn(State state) {
    switch (state) {
        case State::GAME_START: return "游戏开始时";
        case State::OPERATION_ENTER_EXIT_STAGE: return "出战时/下场时";
        case State::OPERATION_CHOOSE_SKILL_MEDICAMENT: return "选择技能/药剂";
        case State::OPERATION_PROTECTION_MECHANISM_1: return "保护机制1";
        case State::OPERATION_ENTER_STAGE: return "登场时";
        case State::BATTLE_ROUND_START: return "回合开始时";
        case State::BATTLE_FIRST_MOVE_RIGHT: return "双方先手权结算";
        case State::BATTLE_FIRST_ACTION_START: return "先手方行动开始时";
        case State::BATTLE_FIRST_BEFORE_SKILL_HIT: return "先手方技能命中前";
        case State::BATTLE_FIRST_ON_SKILL_HIT: return "先手方技能命中时";
        case State::BATTLE_FIRST_SKILL_EFFECT: return "先手方技能效果结算";
        case State::BATTLE_FIRST_ATTACK_DAMAGE: return "先手方攻击伤害结算";
        case State::BATTLE_FIRST_AFTER_ACTION: return "先手方行动后";
        case State::BATTLE_FIRST_ACTION_END: return "先手方行动结束时";
        case State::BATTLE_FIRST_AFTER_ACTION_END: return "先手方行动结束后";
        case State::BATTLE_FIRST_EXTRA_ACTION: return "先手方额外行动";
        case State::BATTLE_FIRST_MOVER_DEATH: return "先手方死亡结算";
        case State::BATTLE_SECOND_ACTION_START: return "后手方行动开始时";
        case State::BATTLE_SECOND_BEFORE_SKILL_HIT: return "后手方技能命中前";
        case State::BATTLE_SECOND_ON_SKILL_HIT: return "后手方技能命中时";
        case State::BATTLE_SECOND_SKILL_EFFECT: return "后手方技能效果结算";
        case State::BATTLE_SECOND_ATTACK_DAMAGE: return "后手方攻击伤害结算";
        case State::BATTLE_SECOND_AFTER_ACTION: return "后手方行动后";
        case State::BATTLE_SECOND_ACTION_END: return "后手方行动结束时";
        case State::BATTLE_SECOND_AFTER_ACTION_END: return "后手方行动结束后";
        case State::BATTLE_SECOND_EXTRA_ACTION: return "后手方额外行动";
        case State::BATTLE_ROUND_END: return "回合结束时";
        case State::BATTLE_SECOND_MOVER_DEATH: return "后手方死亡结算";
        case State::BATTLE_OLD_ROUND_END_1: return "旧版回合结束后1";
        case State::BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS: return "回合扣减点";
        case State::BATTLE_ROUND_REDUCTION_NEW_ROUND_END: return "新版回合结束后";
        case State::BATTLE_OLD_ROUND_END_2: return "旧版回合结束后2";
        case State::BATTLE_DEATH_TIMING: return "死亡时点";
        case State::BATTLE_DEFEAT_STATUS: return "新版未被击败/被击败时";
        case State::BATTLE_OPPONENT_DEFEAT_STATUS: return "新版未击败对手/击败对手时";
        case State::BATTLE_NEW_DEFEAT_MECHANISM: return "新版保护机制";
        case State::OPERATION_PROTECTION_MECHANISM_2: return "保护机制2";
        case State::BATTLE_AFTER_DEFEATED: return "被击败后";
        case State::CHOOSE_AFTER_DEATH: return "死后选择";
        case State::BATTLE_AFTER_DEFEATING_OPPONENT: return "击败对手后";
        case State::BATTLE_ROUND_COMPLETION: return "回合完成";
        case State::FINISHED: return "战斗结束";
        default: return "未知状态";
    }
}

class BattleContext {
public:
    //--- 持久化核心数据 ---
    SeerRobot seerRobot[2];
    int on_stage[2];
    int roundCount;         // 当前回合数（用于效果过期判定）
    unsigned int uuid;
    State currentState;
    BattleFsm* m_fsm;

    //--- 临时工作层（每回合重置）---
    BattleWorkspace ws;

    //--- 当前在场精灵的异常状态结束回合 ---
    // 下标一层是对战方(0/1)，二层是异常状态 id。
    // 存的是”失效回合(exclusive end round)”：
    // currentRound < endRound 视为仍然生效。
    // 这里不放 workspace，因为 workspace 每回合都会 reset。
    std::array<std::array<int, kOfficialAbnormalStatusSlotCount>, 2> abnormal_status_end_round{};

    //--- 回合类效果计数（O(1) 查询”是否有回合类效果”）---
    // 每次 registerEffect 时如果 isRoundEffect()==true 则 +1
    // cleanup_expired_effects / remove_all_round_effects 时扣减
    int active_round_effects[2]{};

    //--- 回合效果版本号（epoch，用于 O(1) 断回合）---
    // 注册回合效果时 effect.valid_id_ = round_effect_valid_id[owner]
    // 断回合时 ++round_effect_valid_id[robotId] 即可使所有旧效果失效
    int round_effect_valid_id[2]{1, 1};  // 从 1 开始，避免和默认初始化的 0 混淆

    //--- 被断回合补偿回调表 ---
    // key: callback_id（自增分配），value: 回调信息
    // 断回合时遍历触发未过期的回调，然后清空该玩家的全部回调
    std::map<int, BreakCallback> on_round_broken[2];
    int next_break_callback_id_ = 1;

    //--- 技能效果执行表 ---
    std::unordered_map<State, std::array<std::vector<std::unique_ptr<ContinuousEffect>>, 2>> skills_effects;

    //--- 魂印效果执行表 ---
    std::unordered_map<State, std::array<std::vector<std::unique_ptr<ContinuousEffect>>, 2>> soul_mark_effects;

    //--- 被动效果表 ---
    std::array<std::map<int, ContinuousEffect*>, 2> passiveEffects;

    //--- 待注册效果 / 未来触发表 ---
    std::unordered_map<State, std::array<std::vector<std::unique_ptr<PendingEffect>>, 2>> pending_effects;

    //--- 网络缓冲 ---
    std::vector<char> m_buffer;
    bool is_empty = true;

    //--- 运行串行化 ---
    // 同一个 battle context 只能串行执行 run，避免多线程 post 导致并发状态破坏。
    std::mutex run_mutex;

    //--- 调试模式 ---
    bool debug_step_mode = false;                       // 单步执行模式：每执行一个状态就停
    std::unordered_set<int> breakpoints;                // 断点状态集合（存 int 便于序列化）

    //--- 控制块 ---
    IControlBlock* control_block_;
    int current_player_id_;  // 当前等待输入的玩家

    //--- 操作日志 ---
    std::string operation_log_;
    std::string moves_log_;

    //--- 每回合输入收集 ---
    // 训练模式下同一连接会顺序提交双方操作，先收齐再进入战斗链。
    std::array<bool, 2> operation_collected{};

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
    bool need_input() const;
    void generateState();
    void back_to_last_state();

    //--- Workspace ---
    void resetWorkspace() { ws.reset(); }

    //--- 当前在场异常状态 ---
    void clear_on_stage_abnormal_statuses(int robotId);
    void clear_all_on_stage_abnormal_statuses();
    void set_abnormal_status_end_round(int robotId, int statusId, int endRound);
    void apply_abnormal_status_for_rounds(int robotId, int statusId, int durationRounds);
    int get_abnormal_status_end_round(int robotId, int statusId) const;
    bool has_active_abnormal_status(int robotId, int statusId) const;

    //--- 效果注册 ---
    void registerEffect(State trigger, int owner, std::unique_ptr<ContinuousEffect> effect,
                        EffectContainer container = EffectContainer::Skill);
    void registerPendingEffect(State observeState, int owner, std::unique_ptr<PendingEffect> effect);
    void registerPassiveEffect(int owner, int effectId, ContinuousEffect* effect);

    //--- 效果执行 ---
    void execute_pending_effects(int robotId, State state);
    void execute_registered_actions(int robotId, State state);

    //--- 效果查询 ---
    template<int EffectId>
    bool hasEffect(State trigger, int owner) const;

    template<int EffectId>
    bool consumeEffect(State trigger, int opponent);

    //--- 回合结束 ---
    void advanceRound() { ++roundCount; }

    //--- 清空效果 ---
    void clearAllEffects() {
        skills_effects.clear();
        soul_mark_effects.clear();
        pending_effects.clear();
        active_round_effects[0] = 0;
        active_round_effects[1] = 0;
        on_round_broken[0].clear();
        on_round_broken[1].clear();
    }

    //--- 回合类效果管理 ---

    /**
     * 清理所有已过期的回合类效果
     *
     * 在 BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS 统一调用，
     * 遍历所有效果桶，移除 isExpired() == true 的效果。
     * 同时更新 active_round_effects 计数器。
     */
    void cleanup_expired_effects();

    /**
     * 断回合 — 移除目标的全部回合类效果
     *
     * O(1) 实现：递增 round_effect_valid_id[robotId] 使所有旧效果失效。
     * 如果目标确实有回合类效果被断，触发目标注册的 break callback。
     * 注意：Mark ID 0（异常免疫标记）不受断回合影响，它不在效果桶中。
     */
    void remove_all_round_effects(int robotId);

    /**
     * 注册被断回合补偿回调
     *
     * @param owner          注册方（被断回合时的补偿触发方）
     * @param duration_rounds 持续回合（与关联的回合效果一致）
     * @param fn             补偿逻辑
     * @return callback_id   用于手动注销
     */
    int register_break_callback(int owner, int duration_rounds,
                                std::function<void(BattleContext*)> fn);

    /**
     * 手动注销被断回合补偿回调
     */
    void remove_break_callback(int owner, int callback_id);

    /**
     * O(1) 查询目标是否还有回合类效果
     */
    bool has_round_effects(int robotId) const {
        return robotId >= 0 && robotId <= 1 && active_round_effects[robotId] > 0;
    }

    //--- 日志 ---
    void log_operation(const std::string& op) {
#ifdef BATTLE_OP_LOGGING
        operation_log_ += op;
        operation_log_ += "|";
#endif
    }

    std::string get_operation_log() const { return operation_log_; }
    void clear_operation_log() { operation_log_.clear(); }

      void reset_operation_collection() { operation_collected = {false, false}; }
      bool has_collected_both_operations() const {
          return std::all_of(operation_collected.begin(), operation_collected.end(), [](bool v) { return v; });
      }

    //--- 便利方法 ---
    ElfPet& getPet(int robotId) { return seerRobot[robotId].elfPets[on_stage[robotId]]; }
    int opponent(int robotId) const { return 1 - robotId; }

    //--- 设置当前玩家 ---
    void set_current_player(int player_id) { current_player_id_ = player_id; }

    //--- 状态同步 ---
    std::string getStateJson() const;
    std::string getFullStateJson() const;
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
