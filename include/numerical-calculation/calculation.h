#ifndef CALCULATION_H
#define CALCULATION_H

#include <cstdlib>
#include <entities/elf-pet.h>
#include <fsm/battleWorkspace.h>

class Calculation {
    public:
    static int calculateDamage(int attacker, const BattleWorkspace& ws, const Skills& skill) {
        int defender = 1 - attacker;
        if(skill.type == SkillType::Attribute) return 0; // Status skills do not deal damage
        // if(skill.element[1] == 0 && defender.elementalAttributes[1] == 0 && ElementalAttributes::elementalAttributesRestraints[skill.element[0]][defender.elementalAttributes[0]] == 0) {
        //     return 0; // No elemental attributes to calculate damage
        // }
        double damage = 0.0;
        double Attack = ws.getTempAbilityValue(attacker, static_cast<NumericalPropertyIndex>(skill.type));
        double Defense = ws.getTempAbilityValue(defender, static_cast<NumericalPropertyIndex>(static_cast<int>(skill.type) + 2));
        // 技能威力视图层：ws.skill_power_view（效果可改，如威力提升/随机威力）优先，
        // 未物化(0)回退技能静态 power。
        const int power = ws.skill_power_view[attacker] > 0 ? ws.skill_power_view[attacker] : skill.power;
        // 技能元素/克制倍率视图层：
        // - 克制按"技能元素视图" vs 防御方元素算（官方机制：克制 = 技能系别 vs 防御方系别，
        //   非攻击方精灵系别——"以XX系别计算克制倍数"类效果改写 skill_element_view）。
        // - restraint_view >= 0 时直接覆盖（"不会出现微弱"钳到1、固定倍率直写）。
        // - 本系加成(involve) 仍用技能真实系别 skill.element（改系别只改克制、不改本系）。
        const auto& elem_view = ws.skill_element_view[attacker];
        double restraint = ws.restraint_view[attacker] >= 0.0
            ? ws.restraint_view[attacker]
            : calculateRestraintMultiples(elem_view, ws.view_elementalAttributes[defender]);
        // "不会出现微弱"(effect 760)：克制<1(微弱)→钳到1(普通)；克制(>1)保持克制，不被硬削。
        if (ws.no_weakness[attacker] && restraint < 1.0) {
            restraint = 1.0;
        }
        damage = (0.84 * Attack / Defense * power + 2) * restraint
                * (217 + rand() % 39) / 255;
        if(involve(ws.view_elementalAttributes[attacker], skill.element)) {
            damage *= 1.5; // Elemental advantage
        }
        return damage;
    }

    static int applyDamageReduction(int base_damage, const int add_reduce[4], const int mul_reduce[4]) {
        double add_sum = 0.0;
        for (int i = 0; i < 4; ++i) {
            add_sum += static_cast<double>(add_reduce[i]);
        }
        if (add_sum > 100.0) {
            add_sum = 100.0;
        }
        if (add_sum < -100.0) {
            add_sum = -100.0;
        }

        double mul_coef = 1.0;
        for (int i = 0; i < 4; ++i) {
            double v = static_cast<double>(mul_reduce[i]);
            if (v > 100.0) {
                v = 100.0;
            }
            if (v < -100.0) {
                v = -100.0;
            }
            mul_coef *= (1.0 - v / 100.0);
        }

        double result = static_cast<double>(base_damage) * (1.0 - add_sum / 100.0) * mul_coef;
        if (result < 0.0) {
            result = 0.0;
        }
        return static_cast<int>(result);
    }
    static double calculateRestraintMultiples(const int attacker[2], const int defender[2]) {
        // 克制倍率矩阵存官方原始倍率 {0免疫, 0.5减半, 1普通, 2克制}。
        // 双属性组合 = 各"属性对"倍率相乘（2v2/2v1/1v2 各两对）。
        const double m00 =
            ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[0]];
        if (attacker[1] == 0 && defender[1] == 0) {  // 1 v 1
            return m00;
        }
        if (attacker[1] != 0 && defender[1] != 0) {  // 2 v 2
            return m00
                * ElementalAttributes::elementalAttributesRestraints[attacker[1]][defender[1]];
        }
        if (attacker[1] != 0) {  // 2 v 1：攻击方双属性各自对防御方单属性的倍率相乘
            return m00
                * ElementalAttributes::elementalAttributesRestraints[attacker[1]][defender[0]];
        }
        // 1 v 2：防御方双属性各自被攻击方单属性克制的倍率相乘
        return m00
            * ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[1]];
    }
    static inline bool involve(const int elf[2], const int skill[2]) {
        if(skill[1] == 0)
            return elf[0] == skill[0] || elf[1] == skill[0];
        return elf[0] == skill[0] && elf[1] == skill[1];
    }
};

#endif // CALCULATION_H