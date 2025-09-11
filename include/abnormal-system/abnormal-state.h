#ifndef ABNORMAL_STATE_H
#define ABNORMAL_STATE_H

#include <iostream>
#include <entities/numerical-properties.h>
#include <entities/elf-pet.h>

//异常状态类
class AbnormalState {
public:
    AbnormalState(const std::string& name, int duration)
        : name(name), duration(duration) {}
    AbnormalState(int i, int duration)
        : id(i), duration(duration) {}
    ~AbnormalState() = default;

    // void applyEffect(const ElfPet& target) const {
    //     switch (id) {
    //         // case 1:
    //         //     target.setSpeed(target.getSpeed() * 0.5);
    //         //     break;
    //         // case 2:
    //         //     target.setSpeed(target.getSpeed() * 2);
    //         //     break;
    //         // case 3:
    //         //     target.setSpeed(target.getSpeed() * 0.5);
    //         //     break;
    //         // case 4:
    //         //     target.setSpeed(target.getSpeed() * 2);
    //         //     break;
    //         default:
    //             std::cerr << "Unknown abnormal state ID: " << id << std::endl;
    //     }
    // }
    void loadAbnormalState();

// private:
    int id;
    std::string name;
    int duration;
    static std::unordered_map<int, std::string> abnormalStateMap;
};

#endif // ABNORMAL_STATE_H