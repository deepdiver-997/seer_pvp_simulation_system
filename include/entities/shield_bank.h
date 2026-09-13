#ifndef SHIELD_BANK_H
#define SHIELD_BANK_H

#include <array>

/**
 * Shield - 一条护盾记录
 *
 * 护盾机制在原作游戏里分多种来源且消耗有优先级：
 * - 魂印每回合刷新的盾、队友给的盾、道具盾……来源不同。
 * - 攻击时按 priority 从高到低消耗。
 * - 部分护盾有持续时间（N 回合后消失），部分破盾前持续。
 */
struct Shield {
    int priority = 0;    // 吸收优先级（数值越大越先被消耗）
    int quantity = 0;    // 剩余护盾值
    int source_id = 0;   // 来源标识（每回合刷新 / 队友 / 道具）——定位用
    int duration = 0;    // 剩余回合（0 = 破盾前持续）
};

/**
 * ShieldBank - 每只精灵的护盾槽
 *
 * 固定容量数组（n ≤ 8），无堆分配；吸收时线性扫最高优先级，实际 O(1)。
 * 选固定 struct 数组而非 priority_queue / 裸 int[]：
 * - priority_queue 无法按来源定位刷新/过期，且有堆开销；
 * - 裸 int[] 无法容纳同优先级的多来源盾，也没有来源身份。
 */
class ShieldBank {
public:
    static constexpr int kMaxShields = 8;

    /** 吸收 damage，从最高优先级开始扣；返回穿透（护盾没挡完的部分）。 */
    int absorb(int damage, int* broken_count = nullptr) {
        int broken = 0;
        while (damage > 0 && count_ > 0) {
            const int best = find_highest_priority_index();
            if (best < 0) {
                break;
            }
            Shield& s = slots_[best];
            if (s.quantity > damage) {
                s.quantity -= damage;
                damage = 0;
            } else {
                damage -= s.quantity;
                ++broken;
                remove_at(best);
            }
        }
        if (broken_count) {
            *broken_count = broken;
        }
        return damage;
    }

    void add(int priority, int quantity, int source_id, int duration) {
        if (count_ >= kMaxShields) {
            return;
        }
        slots_[count_++] = Shield{priority, quantity, source_id, duration};
    }

    /** 每回合刷新：找到 source_id 的盾，重置为 quantity（保留优先级/时长）。 */
    void refresh_source(int source_id, int quantity) {
        for (int i = 0; i < count_; ++i) {
            if (slots_[i].source_id == source_id) {
                slots_[i].quantity = quantity;
                return;
            }
        }
    }

    /** 回合末：duration 减一，归零的移除。 */
    void tick_duration() {
        for (int i = count_ - 1; i >= 0; --i) {
            if (slots_[i].duration > 0 && --slots_[i].duration == 0) {
                remove_at(i);
            }
        }
    }

    void clear() {
        count_ = 0;
    }

    int total() const {
        int t = 0;
        for (int i = 0; i < count_; ++i) {
            t += slots_[i].quantity;
        }
        return t;
    }

    bool empty() const { return count_ == 0; }
    int count() const { return count_; }

private:
    int find_highest_priority_index() const {
        int best = -1;
        for (int i = 0; i < count_; ++i) {
            if (best < 0 || slots_[i].priority > slots_[best].priority) {
                best = i;
            }
        }
        return best;
    }

    void remove_at(int index) {
        for (int i = index; i < count_ - 1; ++i) {
            slots_[i] = slots_[i + 1];
        }
        --count_;
    }

    std::array<Shield, kMaxShields> slots_{};
    int count_ = 0;
};

#endif // SHIELD_BANK_H
