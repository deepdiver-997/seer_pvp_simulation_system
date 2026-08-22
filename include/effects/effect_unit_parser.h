#ifndef EFFECT_UNIT_PARSER_H
#define EFFECT_UNIT_PARSER_H

#include <string>
#include <vector>

#include <effects/effect_unit.h>

// 解析 effect_info 模板文本为条件效果单元（组合语法第二刀）。
//
// 第一刀范围：StatusInflict 主句（"{n}%令/使对手/自身{状态}"）+ 未触发兜底
// （递归异常 / 恢复 / 附加固定伤害）。其余模板返回 -1（不支持的跳过，维持现状）。
//
// skill_args 是该效果的参数列表（record.args，不含 build_effect_args_for_skill 加的
// [0,1] owner/target 前缀）；模板占位符 {n} → skill_args[n]，解析后烘成字面量（每技能实例化）。
//
// 分支指针存活约定：所有单元（根 + 递归分支）追加进调用方提供的 storage，
// 分支指针指向 storage 内元素。**调用方须保证本次解析期间 storage 不 reallocate**
// （loadSkills 开头按效果数 reserve 足量），否则 vector 扩容会使指针悬垂。
// 返回根单元在 storage 中的下标；失败返回 -1。
int parse_effect_unit(const std::string& info, const std::vector<int>& skill_args,
                      std::vector<EffectUnit>& storage);

#endif // EFFECT_UNIT_PARSER_H
