#ifndef EVENT_CENTER_H
#define EVENT_CENTER_H

#include <algorithm>
#include <deque>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <vector>

class BattleContext;

/**
 * EventType - 事件类型
 *
 * 事件是"发生了什么"的声明，由原语在成功路径末尾 emit。
 * 枚举值增长 = 数据扩展，不是新 struct。
 */
enum class EventType {
    EVENT_BREAK,             // 断回合成功（target = 被断方）
    EVENT_DEATH,             // 精灵死亡
    EVENT_CONTROLLED,        // 控场异常施加成功
    EVENT_ANOMALY_APPLIED,   // 任意异常施加成功
    EVENT_HIT,               // 攻击命中
    EVENT_TAKE_DAMAGE,       // 受到伤害结算后
    EVENT_SHIELD_BROKEN,     // 护盾被击破（护盾消失时触发）
    EVENT_ENTER_STAGE,       // 上场
    EVENT_SWAP,              // 换宠
    EVENT_OPPONENT_DEFEATED, // 击败对手
};

/**
 * BattleEvent - 事件载荷
 * actor = 发起方，target = 承受方。payload 先最小化，随需要以数据扩展。
 */
struct BattleEvent {
    EventType type;
    int actor = -1;
    int target = -1;
};

/**
 * EventWatcher - 事件监听器
 *
 * 注册方声明"当 type 事件发生时，运行 fn"。fn 有两种反应模式：
 * 1. 直接改 BattleContext（即时补偿，如被断反伤）——在 drain 点改值安全。
 * 2. registerEffect 注册到未来时点，等 FSM 自然推进（延迟补偿）。
 *
 * 生命周期：
 * - duration_rounds == 0 永久；>0 窗口（register_round 起算）。
 * - once == true 触发一次后自动移除（断回合补偿的语义）。
 */
struct EventWatcher {
    EventType type;
    int owner;                                    // 谁注册的（回调里 self 视角）
    int register_round;
    int duration_rounds;                          // 0 = 永久
    bool once;
    std::function<void(BattleContext*, const BattleEvent&)> fn;
};

/**
 * EventCenter - 事件通道内核
 *
 * 投递策略（已定）：emit 只入队，绝不内联执行 watcher —— 从根上消除重入。
 * FSM 在"每个 State 的效果桶跑完后、推进下一个 State 前"统一 drain 投递。
 * 投递时机对状态机相对位置恒定，消除未知时序；桶迭代安全（drain 在桶之后）。
 *
 * 重入与循环：watcher 内再次 emit 合法（支持事件级联，如 断→击杀→死亡），
 * 进入下一"波"继续投递；一次 drain 最多投递 kMaxWaves 波。若超过波次上限，
 * 说明 watcher 无条件互相 emit 形成反馈环 —— 打印响亮警告并丢弃剩余事件
 * （不静默，便于定位）。不会死锁：波次上限保证有界。
 */
class EventCenter {
public:
    EventCenter() = default;
    EventCenter(const EventCenter&) = delete;
    EventCenter& operator=(const EventCenter&) = delete;

    /**
     * 注册事件监听。
     * @param owner           注册方（self 视角）
     * @param register_round  注册时的回合数（由 BattleContext 传入 roundCount）
     * @param duration_rounds 0 = 永久; >0 = 窗口
     * @param once            触发一次后自动移除
     * @return watcher_id     用于 remove_watcher 手动注销
     */
    int register_watcher(EventType type, int owner, int register_round, int duration_rounds,
                         bool once, std::function<void(BattleContext*, const BattleEvent&)> fn) {
        const int id = next_id_++;
        watchers_.emplace(id, EventWatcher{type, owner, register_round, duration_rounds, once, std::move(fn)});
        by_type_[type].push_back(id);
        return id;
    }

    void remove_watcher(int watcher_id) {
        auto it = watchers_.find(watcher_id);
        if (it == watchers_.end()) {
            return;
        }
        const EventType type = it->second.type;
        watchers_.erase(it);
        auto bit = by_type_.find(type);
        if (bit == by_type_.end()) {
            return;
        }
        auto& ids = bit->second;
        ids.erase(std::remove(ids.begin(), ids.end(), watcher_id), ids.end());
        if (ids.empty()) {
            by_type_.erase(bit);
        }
    }

    /**
     * emit - 只入队，不执行任何代码。
     * 原语在成功路径末尾调用；投递发生在 FSM 的 drain 点。
     */
    void emit(const BattleEvent& event) {
        pending_.push_back(event);
    }

    /**
     * drain - FSM 在 State 桶之后调用，统一投递待处理事件。
     * @param ctx          拥有本 EventCenter 的 BattleContext（传给 watcher fn）
     * @param current_round 当前回合（窗口过期判定）
     */
    void drain(BattleContext* ctx, int current_round) {
        if (pending_.empty()) {
            return;
        }
        draining_ = true;
        constexpr int kMaxWaves = 8;   // 一次级联链（断→击杀→死亡）合理上限
        int waves = 0;
        while (!pending_.empty() && waves++ < kMaxWaves) {
            // 取当前波次；投递中 watcher 新 emit 的事件进入 pending_，形成下一波
            std::deque<BattleEvent> wave = std::move(pending_);
            pending_.clear();
            for (const BattleEvent& event : wave) {
                deliver(ctx, current_round, event);
            }
        }
        draining_ = false;
        if (!pending_.empty()) {
            // 波次上限被突破 → watcher 无条件互相 emit 形成反馈环。响亮报告，不静默丢弃。
            std::cerr << "[EventCenter] drain 超过 " << kMaxWaves
                      << " 波，疑似事件反馈环（watcher 无条件互相 emit）；丢弃 "
                      << pending_.size() << " 个待投递事件。请检查相关 watcher 实现。\n";
            pending_.clear();
        }
    }

    /**
     * cleanup - 移除窗口已过的 watcher。
     * 由 BattleContext::cleanup_expired_effects 在回合扣减点统一调用。
     */
    void cleanup(int current_round) {
        for (auto it = watchers_.begin(); it != watchers_.end();) {
            const EventWatcher& watcher = it->second;
            if (watcher.duration_rounds > 0
                && current_round - watcher.register_round >= watcher.duration_rounds) {
                const int id = it->first;
                const EventType type = watcher.type;
                it = watchers_.erase(it);
                auto bit = by_type_.find(type);
                if (bit == by_type_.end()) {
                    continue;
                }
                auto& ids = bit->second;
                ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
                if (ids.empty()) {
                    by_type_.erase(bit);
                }
            } else {
                ++it;
            }
        }
    }

    void clear_all() {
        watchers_.clear();
        by_type_.clear();
        pending_.clear();
    }

    int watch_count(EventType type) const {
        auto it = by_type_.find(type);
        return it == by_type_.end() ? 0 : static_cast<int>(it->second.size());
    }

    bool has_watchers(EventType type) const {
        return watch_count(type) > 0;
    }

private:
    // 把单个事件投递给其类型的所有 watcher。
    void deliver(BattleContext* ctx, int current_round, const BattleEvent& event) {
        auto it = by_type_.find(event.type);
        if (it == by_type_.end()) {
            return;
        }
        // 复制 id 列表：投递过程中 watcher 可能增删
        const std::vector<int> ids = it->second;
        for (int wid : ids) {
            auto eit = watchers_.find(wid);
            if (eit == watchers_.end()) {
                continue;
            }
            EventWatcher& watcher = eit->second;
            if (watcher.duration_rounds > 0
                && current_round - watcher.register_round >= watcher.duration_rounds) {
                continue;  // 窗口已过，等待 cleanup 移除
            }
            if (watcher.fn) {
                watcher.fn(ctx, event);
            }
            if (watcher.once) {
                remove_watcher(wid);
            }
        }
    }

    std::unordered_map<int, EventWatcher> watchers_;          // id -> watcher
    std::unordered_map<EventType, std::vector<int>> by_type_; // type -> watcher ids
    std::deque<BattleEvent> pending_;                         // 待投递事件队列
    bool draining_ = false;                                   // 是否正在 drain（日志/调试定位用）
    int next_id_ = 1;
};

#endif // EVENT_CENTER_H
