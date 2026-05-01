#ifndef MARK_H
#define MARK_H

#include <vector>
#include <string>
#include <algorithm>

class BattleContext;

/**
 * Mark - 特殊印记 (e.g. 神印、战意)
 *
 * 与回合类效果(ContinuousEffect)的核心区别：
 * - Mark 不会被"断回合"效果移除，因为它不在 skills_effects / soul_mark_effects 桶中
 * - Mark 的增删由魂印或技能效果自行管理，是主动式的状态标记
 *
 * 常见用途：
 * - 异常免疫标记（ID 0）：免控/魂免
 * - 属性弱化免疫标记
 * - 特殊机制标记（如"下回合无法行动"等状态记录）
 *
 * 关于 ID 0（异常免疫标记）：
 * ----------------------------------------------------------------
 * 全场魂免精灵：初始化时直接挂上 Mark(0, "异常免疫")，永不移除
 * 条件魂免精灵：由魂印注册器按条件自行增删。e.g.
 *   - "回合开始时若体力 > 1/2 则本回合免控"
 *     → 魂印在 BATTLE_ROUND_START 判定，条件满足则 add_mark,
 *       在 BATTLE_ROUND_END 移除
 *   - "回合结束时若体力 > 1/2 则下两回合免控"
 *     → 魂印在 BATTLE_ROUND_END 判定，条件满足则 mark.count = 2,
 *       每回合结束时 count--, 当 count > 0 表示处于免控状态
 *
 * count 字段的两种用法：
 * 1. 永久标记（count == 0）：只要标记存在就生效，与 count 值无关
 * 2. 倒计时标记（count > 0）：每回合/每状态递减，count == 0 时移除标记
 * ----------------------------------------------------------------
 */
class Mark
{
    public:
        Mark(int id, const std::string& name, int count = 0)
            : id(id), name(name), count(count) {}
        ~Mark() = default;

        int id;
        std::string name;
        int count;  // 0 = 永久; >0 = 剩余生效次数/回合数
};

// --- Mark 辅助函数（操作 std::vector<Mark>） ---

/**
 * 检查是否存在指定 ID 的标记（且 count > 0 或永久标记有效）
 */
inline bool has_mark(const std::vector<Mark>& marks, int mark_id) {
    for (const auto& m : marks) {
        if (m.id == mark_id && m.count >= 0) {
            return true;
        }
    }
    return false;
}

/**
 * 添加标记。如果已存在，重置 count；否则新增
 */
inline void add_mark(std::vector<Mark>& marks, int mark_id,
                     const std::string& name, int count = 0) {
    for (auto& m : marks) {
        if (m.id == mark_id) {
            m.count = count;
            return;
        }
    }
    marks.emplace_back(mark_id, name, count);
}

/**
 * 移除指定 ID 的标记
 */
inline void remove_mark(std::vector<Mark>& marks, int mark_id) {
    marks.erase(
        std::remove_if(marks.begin(), marks.end(),
                       [mark_id](const Mark& m) { return m.id == mark_id; }),
        marks.end());
}

/**
 * 对倒计时标记执行 count--，count 归零时自动移除。
 * 永久标记（count == 0）不受影响。
 */
inline void tick_mark_count(std::vector<Mark>& marks, int mark_id) {
    for (auto it = marks.begin(); it != marks.end(); ++it) {
        if (it->id == mark_id && it->count > 0) {
            --it->count;
            if (it->count <= 0) {
                marks.erase(it);
            }
            return;
        }
    }
}

#endif
