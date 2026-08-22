#include <effects/effect_unit_parser.h>

#include <cctype>
#include <cstring>

namespace {

// 在 text 中从 from 起找第一个 "{n}"，返回占位符下标 n；找不到返回 -1。
int find_placeholder(const std::string& text, std::size_t from) {
    const std::size_t open = text.find('{', from);
    if (open == std::string::npos) {
        return -1;
    }
    const std::size_t close = text.find('}', open);
    if (close == std::string::npos || close == open + 1) {
        return -1;
    }
    bool digits = true;
    for (std::size_t j = open + 1; j < close; ++j) {
        if (!std::isdigit(static_cast<unsigned char>(text[j]))) {
            digits = false;
            break;
        }
    }
    if (!digits) {
        return -1;
    }
    return std::stoi(text.substr(open + 1, close - open - 1));
}

// 取 skill_args[placeholder]（越界返回 -1）。
int arg_at(const std::vector<int>& skill_args, int placeholder) {
    if (placeholder < 0 || static_cast<std::size_t>(placeholder) >= skill_args.size()) {
        return -1;
    }
    return skill_args[placeholder];
}

// 找 text[0..end) 中最后一个 "{n}%" 的占位符下标；无则 -1。
int find_last_percent_placeholder(const std::string& text, std::size_t end) {
    int last = -1;
    std::size_t i = 0;
    while (i < end) {
        const std::size_t pct = text.find('%', i);
        if (pct == std::string::npos || pct >= end) {
            break;
        }
        if (pct >= 2 && text[pct - 1] == '}') {
            const std::size_t open = text.rfind('{', pct - 2);
            if (open != std::string::npos && open < pct - 1) {
                bool digits = true;
                for (std::size_t j = open + 1; j < pct - 1; ++j) {
                    if (!std::isdigit(static_cast<unsigned char>(text[j]))) {
                        digits = false;
                        break;
                    }
                }
                if (digits) {
                    last = std::stoi(text.substr(open + 1, pct - 1 - open - 1));
                }
            }
        }
        i = pct + 1;
    }
    return last;
}

// 解析 StatusInflict 主句："令/使对手/自身{状态}"。
// 填 unit（Anomaly 主动作 + chance_value + param0=anomaly_id + param1=2），
// consumed_end = 状态占位符 '}' 之后。失败返回 false。
bool parse_status_inflict_primary(const std::string& text, const std::vector<int>& skill_args,
                                  EffectUnit& unit, std::size_t& consumed_end) {
    static const char* kPatterns[] = {"令对手{", "令自身{", "使对手{", "使自身{"};
    for (const char* pat : kPatterns) {
        const std::size_t pos = text.find(pat);
        if (pos == std::string::npos) {
            continue;
        }
        // '{' 在 pattern 末尾（中文字符按 UTF-8 多字节，用 strlen 定位）。
        const std::size_t brace_pos = pos + std::strlen(pat) - 1;
        const int status_ph = find_placeholder(text, brace_pos);
        if (status_ph < 0) {
            continue;
        }
        unit.primary_tag = PrimitiveTag::Anomaly;
        unit.target = (std::strstr(pat, "自身") != nullptr) ? 0 : 1;
        unit.param0 = arg_at(skill_args, status_ph);
        unit.param1 = 2;  // 默认持续 2 回合
        unit.chance_value = (find_last_percent_placeholder(text, pos) >= 0)
            ? arg_at(skill_args, find_last_percent_placeholder(text, pos))
            : -1;
        const std::size_t close = text.find('}', brace_pos);
        consumed_end = close != std::string::npos ? close + 1 : text.size();
        return true;
    }
    return false;
}

// 递归解析兜底子句，追加到 storage，返回其根下标；不支持返回 -1。
int parse_fallback_unit(const std::string& text, const std::vector<int>& skill_args,
                        std::vector<EffectUnit>& storage) {
    EffectUnit unit;
    // 1) 递归异常：令/使对手/自身{状态}
    std::size_t consumed = 0;
    if (parse_status_inflict_primary(text, skill_args, unit, consumed)) {
        storage.push_back(unit);
        return static_cast<int>(storage.size()) - 1;
    }
    // 2) 恢复：恢复自身最大体力...1/{n} 或 全部
    if (text.find("恢复") != std::string::npos && text.find("自身") != std::string::npos) {
        unit.primary_tag = PrimitiveTag::Heal;
        unit.target = 0;  // 自身
        const std::size_t frac = text.find("1/");
        if (frac != std::string::npos) {
            unit.param0 = arg_at(skill_args, find_placeholder(text, frac));
        } else {
            unit.param0 = 0;  // fraction_denom<=0 → 恢复全部
        }
        storage.push_back(unit);
        return static_cast<int>(storage.size()) - 1;
    }
    // 3) 附加{n}点固定伤害
    if (text.find("附加") != std::string::npos && text.find("固定伤害") != std::string::npos) {
        unit.primary_tag = PrimitiveTag::FixedDamage;
        unit.target = 1;  // 对手
        unit.param0 = arg_at(skill_args, find_placeholder(text, text.find("附加") + 2));
        storage.push_back(unit);
        return static_cast<int>(storage.size()) - 1;
    }
    return -1;
}

}  // namespace

int parse_effect_unit(const std::string& info, const std::vector<int>& skill_args,
                      std::vector<EffectUnit>& storage) {
    EffectUnit primary;
    std::size_t consumed = 0;
    if (!parse_status_inflict_primary(info, skill_args, primary, consumed)) {
        return -1;
    }
    storage.push_back(primary);
    const int root_idx = static_cast<int>(storage.size()) - 1;

    // 未触发兜底：切 "，[若]未触发则" 之后的子句（"若未触发则" 含 "未触发则"）。
    const std::string remainder = info.substr(consumed);
    const std::size_t wf = remainder.find("未触发则");
    if (wf != std::string::npos) {
        const std::string fallback_text = remainder.substr(wf + 4);
        const int fallback_idx = parse_fallback_unit(fallback_text, skill_args, storage);
        if (fallback_idx >= 0) {
            storage[root_idx].on_other = &storage[fallback_idx];  // 概率未触发 → 兜底
        }
    }
    return root_idx;
}
