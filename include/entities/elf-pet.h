#ifndef ELF_PET_H
#define ELF_PET_H

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <entities/elemental-attributes.h>
#include <entities/mark.h>
#include <entities/numerical-properties.h>
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
        , id(other.id)
        , name(other.name) {}

    ElfPet(ElfPet&& other) noexcept
        : elementalAttributes(std::move(other.elementalAttributes))
        , soulSeal(other.soulSeal)
        , gender(other.gender)
        , soulMark(std::move(other.soulMark))
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
        numericalBase = other.numericalBase;
        numericalProperties = other.numericalProperties;
        levels = other.levels;
        speed_priority = other.speed_priority;
        shield = other.shield;
        cover = other.cover;
        is_locked = other.is_locked;
        skills = other.skills;
        marks = other.marks;
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
        numericalBase = other.numericalBase;
        numericalProperties = other.numericalProperties;
        levels = std::move(other.levels);
        speed_priority = other.speed_priority;
        shield = other.shield;
        cover = other.cover;
        is_locked = other.is_locked;
        skills = std::move(other.skills);
        marks = std::move(other.marks);
        id = other.id;
        name = std::move(other.name);
        return *this;
    }

    ~ElfPet() = default;

    std::array<int, 2> elementalAttributes{};
    int soulSeal = 0;
    Gender gender = Gender::NONE;
    SoulMark soulMark;
    numerical_properties numericalBase, numericalProperties;
    int& hp = numericalProperties[NumericalPropertyIndex::HP];
    std::array<int, 6> levels{};
    int speed_priority = 0;
    int shield = 0;
    int cover = 0;
    bool is_locked = false;
    std::array<Skills, 5> skills;
    std::vector<Mark> marks;

    int id = -1;
    std::string name;
};

#endif // ELF_PET_H
