#ifndef __battleContext_H
#define __battleContext_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <boost/asio.hpp>

#include <entities/seer-robot.h>
#include <fsm/battleFsm.h>
#include <entities/soul_mark.h>

enum class State {
// 操作阶段
GAME_START,                           // 游戏开始时
OPERATION_ENTER_EXIT_STAGE,           // 出战时/下场时
OPERATION_CHOOSE_SKILL_MEDICAMENT,    // 选择使用技能/选择使用药剂
OPERATION_PROTECTION_MECHANISM_1,     // 保护机制
OPERATION_ENTER_STAGE,                // 登场时

// 战斗阶段
BATTLE_ROUND_START,                   // 回合开始时
BATTLE_FIRST_MOVE_RIGHT,              // 双方先手权结算
// 先手方流程时点（行动开始时→行动结束后）
BATTLE_FIRST_ACTION_START,                  // 行动开始时（出手流程开始）
BATTLE_FIRST_BEFORE_SKILL_HIT,              // 技能命中前
BATTLE_FIRST_ON_SKILL_HIT,                  // 技能命中时
BATTLE_FIRST_SKILL_EFFECT,                  // 技能效果结算
BATTLE_FIRST_ATTACK_DAMAGE,                 // 攻击伤害结算
BATTLE_FIRST_AFTER_ACTION,                  // 行动后
BATTLE_FIRST_ACTION_END,                    // 行动结束时（出手流程结束）
BATTLE_FIRST_AFTER_ACTION_END,              // 行动结束后（出手流程结束后）
BATTLE_FIRST_EXTRA_ACTION,                  // 额外行动
BATTLE_FIRST_MOVER_DEATH,                   // 先行方死亡结算
// 后手方重复流程时点（行动开始时→行动结束后）复用上述定义
BATTLE_SECOND_ACTION_START,                  // 行动开始时（出手流程开始）
BATTLE_SECOND_BEFORE_SKILL_HIT,              // 技能命中前
BATTLE_SECOND_ON_SKILL_HIT,                  // 技能命中时
BATTLE_SECOND_SKILL_EFFECT,                  // 技能效果结算
BATTLE_SECOND_ATTACK_DAMAGE,                 // 攻击伤害结算1
BATTLE_SECOND_AFTER_ACTION,                  // 行动后
BATTLE_SECOND_ACTION_END,                    // 行动结束时（出手流程结束）
BATTLE_SECOND_AFTER_ACTION_END,              // 行动结束后（出手流程结束后）
BATTLE_SECOND_EXTRA_ACTION,                  // 额外行动
BATTLE_ROUND_END,                            // 回合结束时
BATTLE_SECOND_MOVER_DEATH,                   // 后方人死亡时点
BATTLE_OLD_ROUND_END_1,                      // 旧版回合结束后1
BATTLE_ROUND_REDUCTION_ALL_ROUND_MINUS,      // 回合扣减点
BATTLE_ROUND_REDUCTION_NEW_ROUND_END,        // 新版回合结束时
BATTLE_OLD_ROUND_END_2,                      // 旧版回合结束后2（含新回合结束后）
BATTLE_DEATH_TIMING,                         // 死亡时点
BATTLE_DEFEAT_STATUS,                        // 未被击败/被击败时
BATTLE_OPPONENT_DEFEAT_STATUS,               // 未击败对手/击败对手时
BATTLE_NEW_DEFEAT_MECHANISM,                 // 新版击败/新版未击败/新击败被击败
OPERATION_PROTECTION_MECHANISM_2,            // 保护机制
BATTLE_AFTER_DEFEATED,                       // 被击败后
CHOOSE_AFTER_DEATH,                          // 死切
BATTLE_AFTER_DEFEATING_OPPONENT,             // 击败对手后
BATTLE_ROUND_COMPLETION,                     // 回合流程完毕
FINISHED
};


class BattleContext : public std::enable_shared_from_this<BattleContext> {
    public:
    // int uuid;
    std::unordered_map<State, std::vector<Effect>> stateActions[2];   // effect to be executed when entering the state
    SeerRobot seerRobot[2]; // 0 is the host
    int on_stage[2];
    int roundChoice[2][2]; // dimension: 0 -> robotId 1 -> actionType, index
    bool preemptive_right; //true-->seerRobot1 has the preemptive right, false-->seerRobot2 has the preemptive right
    int roundCount;  // Current round count
    unsigned int uuid;  // Unique identifier for the battle
    std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;
    std::vector<char> m_buffer;
    BattleFsm *m_fsm;
    public:
    State currentState;
    BattleContext(const SeerRobot seerRobot[2], bool host = true, std::unique_ptr<boost::asio::ip::tcp::socket>&& socket, BattleFsm *fsm);
    BattleContext() = delete;
    ~BattleContext();
    void async_read(std::function<void(std::shared_ptr<BattleContext>)> callback);
    void async_write(const std::string& data, std::function<void(std::shared_ptr<BattleContext>)> callback);
    void generateState();
    void execute_registered_actions(int target, State state);
    void check_soul_mark();


};

BattleContext::BattleContext(const SeerRobot seerRobot[2], bool host, std::unique_ptr<boost::asio::ip::tcp::socket>&& socket, BattleFsm *fsm)
    : seerRobot{seerRobot[0], seerRobot[1]}
    , m_socket(std::move(socket))
    , m_buffer(1024)  // Initialize buffer with a size of 1024 bytes
    , m_fsm(fsm)
    , roundCount(0)
    , uuid(std::chrono::system_clock::now().time_since_epoch().count())
    , currentState(State::GAME_START)
{
    m_fsm->run(shared_from_this());
}

void BattleContext::async_read(std::function<void(std::shared_ptr<BattleContext>)> callback) {
    auto self = shared_from_this();
    m_buffer.clear();
    m_socket->async_read_some(boost::asio::buffer(m_buffer),
    [self, callback](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            callback(self);
        } else {
            std::cerr << "Error reading from socket: " << ec.message() << std::endl;
        }
    });
}

void BattleContext::async_write(const std::string& data, std::function<void(std::shared_ptr<BattleContext>)> callback) {
    auto self = shared_from_this();
    boost::asio::async_write(*m_socket, boost::asio::buffer(data),
    [self, callback](boost::system::error_code ec, std::size_t length) {
        if (!ec) {
            callback(self);
        }
        else {
            std::cerr << "Error writing: " << ec.message() << std::endl;
        }
    });
}

void BattleContext::generateState() {
    // Logic to generate the next state based on the current state and game rules
    // This is a placeholder for actual state generation logic
    std::cout << "Generating next state based on current state: " << static_cast<int>(currentState) << std::endl;
    if(currentState < State::BATTLE_ROUND_COMPLETION) {
        currentState = static_cast<State>(static_cast<int>(currentState) + 1);
    } else {
        currentState = State::GAME_START;  // Reset to start after completion
    }
}

void BattleContext::execute_registered_actions(int target, State state) {
    auto& actions_map = stateActions[target];
    auto actionsIt = actions_map.find(state);
    if (actionsIt != actions_map.end()) {
        for (const auto& action : actionsIt->second) {
            action.logic(shared_from_this());
        }
        actions_map.erase(actionsIt);
    }
}

void BattleContext::check_soul_mark() {
    //优先结算房主效果
    for (auto& robot : seerRobot) {
        for (auto& pet : robot.elfPets) {
            #ifdef CLEAR_CACHE
            (*(pet.soulMark.effect))(shared_from_this());
            #else
            pet.soulMark.effect(shared_from_this());
            #endif
        }
    }
}

#endif