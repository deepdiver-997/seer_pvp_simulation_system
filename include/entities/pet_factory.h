#ifndef PET_FACTORY_H
#define PET_FACTORY_H

#include <array>
#include <string>
#include <vector>

#include <db/official_data_repository.h>
#include <entities/elf-pet.h>
#include <entities/seer-robot.h>

struct BattlePetMessage {
    int pet_id = 0;
    std::array<int, 5> chosen_skills_id{};
    std::array<int, 6> numerical_base{};
};

struct BattleCreateRequest {
    std::array<BattlePetMessage, 6> side1{};
    std::array<BattlePetMessage, 6> side2{};
};

BattleCreateRequest decode_battle_create_request(const std::vector<char>& payload);

class PetFactory {
public:
    static bool initialize_runtime_data(const std::string& db_path = official_data::kDefaultOfficialDatabasePath);
    static ElfPet create_pet(const BattlePetMessage& message);

private:
    static std::array<Skills, 5> create_skills_for_pet(
        const official_data::MonsterRecord& monster,
        const std::array<int, 5>& chosen_skill_ids
    );
    static SoulMark create_soul_mark_for_pet(const official_data::MonsterRecord& monster);
    static numerical_properties create_numerical_base(
        const official_data::MonsterRecord& monster,
        const std::array<int, 6>& requested_base
    );
};

class SeerRobotFactory {
public:
    static SeerRobot create_robot(
        const std::array<BattlePetMessage, 6>& party,
        const std::array<int, MEDICINES_SIZE>& medicines = {}
    );
};

#endif // PET_FACTORY_H
