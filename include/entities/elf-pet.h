#ifndef ELF_PET_H
#define ELF_PET_H

#include <any>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <entities/common_trait.h>
#include <entities/elemental-attributes.h>
#include <entities/mark.h>
#include <abnormal-system/resistance-system.h>
#include <entities/numerical-properties.h>
#include <entities/shield_bank.h>
#include <entities/skills.h>
#include <entities/soul_mark.h>

enum class Gender {
    MALE = 0,
    FEMALE = 1,
    NONE = 2
};

class ElfPet {
public:
    ElfPet() = delete;

    ElfPet(int id,
           std::string name,
           std::array<int, 2> elemental_attributes,
           int soul_seal,
           Gender gender,
           SoulMark soul_mark,
           CommonTrait common_trait,
           numerical_properties numerical_base,
           int initial_hp,
           std::array<int, 6> levels,
           int shield,
           int cover,
           bool is_locked,
           std::array<Skills, 5> skills)
        : elementalAttributes(std::move(elemental_attributes))
        , soulSeal(soul_seal)
        , gender(gender)
        , soulMark(std::move(soul_mark))
        , commonTrait(std::move(common_trait))
        , numericalBase(numerical_base)
        , numericalProperties(numerical_base)
        , hp(numericalProperties[NumericalPropertyIndex::HP])
        , levels(std::move(levels))
        , speed_priority(0)
        , shield(shield)
        , cover(cover)
        , is_locked(is_locked)
        , skills(std::move(skills))
        , id(id)
        , name(std::move(name)) {
        hp = initial_hp;
    }

    ElfPet(const ElfPet& other)
        : elementalAttributes(other.elementalAttributes)
        , soulSeal(other.soulSeal)
        , gender(other.gender)
        , soulMark(other.soulMark)
        , commonTrait(other.commonTrait)
        , numericalBase(other.numericalBase)
        , numericalProperties(other.numericalProperties)
        , hp(numericalProperties[NumericalPropertyIndex::HP])
        , levels(other.levels)
        , speed_priority(other.speed_priority)
        , shield(other.shield)
        , cover(other.cover)
        , is_locked(other.is_locked)
        , skills(other.skills)
        , marks(other.marks)
        , soulmark_storage(other.soulmark_storage)
        , resistance(other.resistance)
        , id(other.id)
        , name(other.name) {}

    ElfPet(ElfPet&& other) noexcept
        : elementalAttributes(std::move(other.elementalAttributes))
        , soulSeal(other.soulSeal)
        , gender(other.gender)
        , soulMark(std::move(other.soulMark))
        , commonTrait(std::move(other.commonTrait))
        , numericalBase(other.numericalBase)
        , numericalProperties(other.numericalProperties)
        , hp(numericalProperties[NumericalPropertyIndex::HP])
        , levels(std::move(other.levels))
        , speed_priority(other.speed_priority)
        , shield(other.shield)
        , cover(other.cover)
        , is_locked(other.is_locked)
        , skills(std::move(other.skills))
        , marks(std::move(other.marks))
        , soulmark_storage(std::move(other.soulmark_storage))
        , resistance(std::move(other.resistance))
        , id(other.id)
        , name(std::move(other.name)) {}

    ElfPet& operator=(const ElfPet& other) {
        if (this == &other) {
            return *this;
        }
        elementalAttributes = other.elementalAttributes;
        soulSeal = other.soulSeal;
        gender = other.gender;
        soulMark = other.soulMark;
        commonTrait = other.commonTrait;
        numericalBase = other.numericalBase;
        numericalProperties = other.numericalProperties;
        levels = other.levels;
        speed_priority = other.speed_priority;
        shield = other.shield;
        cover = other.cover;
        is_locked = other.is_locked;
        skills = other.skills;
        marks = other.marks;
        soulmark_storage = other.soulmark_storage;
        resistance = other.resistance;
        id = other.id;
        name = other.name;
        return *this;
    }

    ElfPet& operator=(ElfPet&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        elementalAttributes = std::move(other.elementalAttributes);
        soulSeal = other.soulSeal;
        gender = other.gender;
        soulMark = std::move(other.soulMark);
        commonTrait = std::move(other.commonTrait);
        numericalBase = other.numericalBase;
        numericalProperties = other.numericalProperties;
        levels = std::move(other.levels);
        speed_priority = other.speed_priority;
        shield = other.shield;
        cover = other.cover;
        is_locked = other.is_locked;
        skills = std::move(other.skills);
        marks = std::move(other.marks);
        soulmark_storage = std::move(other.soulmark_storage);
        resistance = std::move(other.resistance);
        id = other.id;
        name = std::move(other.name);
        return *this;
    }

    ~ElfPet() = default;

    std::array<int, 2> elementalAttributes{};
    int soulSeal = 0;
    Gender gender = Gender::NONE;
    SoulMark soulMark;
    CommonTrait commonTrait;
    ResistanceSystem resistance;  // 异常抗性（训练刷出的概率抵抗；apply_anomaly 在魂免前 roll）
    numerical_properties numericalBase, numericalProperties;
    int& hp = numericalProperties[NumericalPropertyIndex::HP];
    std::array<int, 6> levels{};
    int speed_priority = 0;
    int shield = 0;      // 旧字段，暂留（未参与伤害计算）
    int cover = 0;
    ShieldBank shield_bank_;  // 护盾槽（只响应红伤 NORMAL；多来源 + 优先级消耗 + 每回合刷新）
    ShieldBank hood_bank_;    // 护罩槽（只响应粉伤 FIXED/PERCENT；结构同护盾，独立实体）
    int damage_suppress_mask = 0;  // 伤害效果抑制掩码（bit = DamageEffectCategory，被抑制类别在管线 walk 时跳过）
    bool is_locked = false;
    std::array<Skills, 5> skills;
    std::vector<Mark> marks;

    // 魂印持久私有存储：按魂印 id 分槽，内容由魂印自解释（引擎不关心布局）。
    // 随 pet 对象存活 → 天然跨切换/下场保留；精灵阵亡/战斗结束随 pet 销毁。
    // 典型用途：无相谛蓄力（万相乖离已取消条件数 + 威力提升）。
    std::map<int, std::any> soulmark_storage;

    int id = -1;
    std::string name;
};

#endif // ELF_PET_H
