#ifndef CALCULATION_H
#define CALCULATION_H

#include <cstdlib>
#include <entities/elf-pet.h>

class Calculation {
    public:
    static int calculateDamage(const ElfPet& attacker, const ElfPet& defender, const Skills& skill) {
        if(skill.type == 1) return 0; // Status skills do not deal damage
        if(skill.element[1] == 0 && defender.elementalAttributes[1] == 0 && ElementalAttributes::elementalAttributesRestraints[skill.element[0]][defender.elementalAttributes[0]] == 0) {
            return 0; // No elemental attributes to calculate damage
        }
        double damage = 0.0;
        double Attack = attacker.numericalProperties[skill.type];
        double Defense = defender.numericalProperties[skill.type + 2];
        if(attacker.level[skill.type] > 0)
        Attack = Attack * ((2 + attacker.level[skill.type]) / 2);
        else
        Attack = Attack * (2 / (2 + attacker.level[skill.type]));
        if(defender.level[skill.type + 2] > 0)
        Defense = Defense * ((2 + defender.level[skill.type + 2]) / 2);
        else
        Defense = Defense * (2 / (2 + defender.level[skill.type + 2]));
        damage = (0.84 * Attack / Defense * skill.power + 2)
                * calculateRestraintMultiples(attacker.elementalAttributes, defender.elementalAttributes)
                * (217 + rand() % 39) / 255;
        if(involve(attacker.elementalAttributes, skill.element)) {
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
        double multiples = 1.0;
        if(attacker[1] == 0 && defender[1] == 0) {  //1 v 1
            multiples = ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[0]];
        } else if(attacker[1] != 0 && defender[1] != 0) {   //2 v 2
            multiples = ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[0]] * ElementalAttributes::elementalAttributesRestraints[attacker[1]][defender[1]];
        } else if(attacker[1] != 0) {   //2 v 1
            int t1 = ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[0]];
            int t2 = ElementalAttributes::elementalAttributesRestraints[attacker[1]][defender[0]];
            if(t1 == 2 && t2 == 2)
                multiples = 4;
            else {
                multiples = (t1 + t2) / 2.0; // Average of the two resistances
                if(t1 == 0 || t2 == 0)
                    multiples /= 2.0;
            }
        } else if(defender[1] != 0) {   //1 v 2
            int t1 = ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[0]];
            int t2 = ElementalAttributes::elementalAttributesRestraints[attacker[0]][defender[1]];
            if(t1 == 2 && t2 == 2)
                multiples = 4;
            else {
                multiples = (t1 + t2) / 2.0; // Average of the two resistances
                if(t1 == 0 || t2 == 0)
                multiples /= 2.0;
            }
        }
        return multiples; // Placeholder for actual calculation logic
    }
    static inline bool involve(const int elf[2], const int skill[2]) {
        if(skill[1] == 0)
            return elf[0] == skill[0] || elf[1] == skill[0];
        return elf[0] == skill[0] && elf[1] == skill[1];
    }
};

#endif // CALCULATION_H