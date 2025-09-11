#ifndef ELF_PET_H
#define ELF_PET_H

#include <iostream>
#include <vector>

#include <abnormal-system/abnormal-state.h>

#include <entities/soul_mark.h>
#include <entities/elemental-attributes.h>
#include <entities/numerical-properties.h>
#include <entities/skills.h>
#include <entities/mark.h>

class ElfPet {
public:
    ElfPet() = delete; // Deleting default constructor to prevent instantiation without parameters
    ElfPet(int id, const std::string& name, int element1, int element2, int soulSeal, const numerical_properties& numProps, int hp, const int level[6], int shield, int cover, bool is_locked, const Skills skills[5])
        : id(id), name(name), soulSeal(soulSeal), numericalProperties(numProps), hp(hp), shield(shield), cover(cover), is_locked(is_locked), soulMark(soulSeal, "", "")
    {
        elementalAttributes[0] = element1;
        elementalAttributes[1] = element2;
        if (level)
        for (int i = 0; i < 6; ++i) {
            this->level[i] = level[i];
        }
        if (skills)
            for (int i = 0; i < 5; ++i) {
                this->skills[i] = skills[i];
            }
        else
            throw std::invalid_argument("Skills array cannot be null");
    }
    ~ElfPet() = default;

// private:
    int elementalAttributes[2]; //元素属性
    int soulSeal;
    SoulMark soulMark;
    numerical_properties numericalProperties;   // 对局外的数值属性
    int hp;          // Health Points
    int level[6];   //攻击，防御，特攻，特防，速度，命中
    int shield;    //当前护盾值
    int cover;    //当前护罩值
    bool is_locked; //是否被封印
    Skills skills[5];
    std::vector<AbnormalState> abnormalStates; // Vector to hold abnormal states
    std::vector<std::unique_ptr<Mark>> marks; // Vector to hold marks

    int id;
    std::string name;
};

#endif // ELF_PET_H