#ifndef SEER_ROBOT_H
#define SEER_ROBOT_H

#include <array>
#include <entities/elf-pet.h>
#define MEDICINES_SIZE 10

class BattleContext;

// 嗑药类型（medicines 槽位 index 0..6 各对应一种；medicines[slot]=该种数量）。
//   kMedicineHealAmount / kMedicineRestorePP 为空值位（官方暂无药品数据, 先占位）。
enum class MedicineType : int {
    HEAL_HP = 0,          // 1 恢复 x 点体力
    RESTORE_PP = 1,       // 2 恢复所有技能 x 点 pp
    CLEAR_DROPS = 2,      // 3 解除自己能力下降
    CLEAR_ANOMALY = 3,    // 4 解除自己所有异常状态
    HP_PP = 4,            // 5 = 1+2
    PP_CLEAR_DROPS = 5,   // 6 = 2+3
    HP_CLEAR_ANOMALY = 6, // 7 = 1+4
};

class SeerRobot
{
    public:
    SeerRobot(std::array<ElfPet, 6> elfPets_, std::array<int, MEDICINES_SIZE> medicines_)
     :elfPets(elfPets_), medicines(medicines_) {}
    ~SeerRobot() = default;
    // 使用药品：按槽位(类型)应用效果。需 BattleContext(清除异常在 ctx 上)。实现见 seer-robot.cpp。
    bool use_medicine(BattleContext* ctx, int robot_id, int medicine_index);
    std::array<ElfPet, 6> elfPets;
    std::array<int, MEDICINES_SIZE> medicines;
    int allive() const {
        int alive = 0;
        for (const auto &pet : elfPets) {
            if (pet.hp > 0)
                ++alive;
        }
        return alive;
    }
    };

#endif // SEER_ROBOT_H
