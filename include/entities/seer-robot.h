#ifndef SEER_ROBOT_H
#define SEER_ROBOT_H

#include <elf-pet.h>
#define MEDICINES_SIZE 10

class SeerRobot
{
    public:
    SeerRobot(std::array<ElfPet, 6> elfPets_, std::array<int, MEDICINES_SIZE> medicines_)
     :elfPets(elfPets_), medicines(medicines_) {}
    std::array<ElfPet, 6> elfPets;
    std::array<int, MEDICINES_SIZE> medicines;
    int allive() {
        int alive = 0;
        for (const auto &pet : elfPets) {
            if (pet.hp > 0)
                ++alive;
        }
        return alive;
    }
    bool use_medicine(ElfPet &pet, int medicine_index) {
        if (medicine_index < 0 || medicine_index >= MEDICINES_SIZE) {
            std::cerr << "Invalid medicine index: " << medicine_index << std::endl;
            return false;
        }
        if (medicines[medicine_index] > 0) {
            pet.hp += 20;  // Heal the pet
            medicines[medicine_index]--;
            std::cout << "Used medicine " << medicine_index << " on pet " << pet.name << "." << std::endl;
            return true;
        } else {
            std::cerr << "No medicine available at index: " << medicine_index << std::endl;
            return false;
        }
    }
};

#endif // SEER_ROBOT_H