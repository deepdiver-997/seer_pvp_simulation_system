#ifndef RESISTANCE_SYSTEM_H
#define RESISTANCE_SYSTEM_H

#include <vector>
#include <cstdlib>
#include <abnormal-system/abnormal-state.h>

//抗性系统
class ResistanceSystem {
public:
    ResistanceSystem() = default;
    ~ResistanceSystem() = default;

    bool changeControl(std::pair<int, int> newControl[3]) {
        int total = 0;
        for (int i = 0; i < 3; ++i) {
            total += newControl[i].second;
            if(newControl[i].second < 0 || newControl[i].second > 100 || total > 100 || AbnormalState::abnormalStateMap.find(newControl[i].first) == AbnormalState::abnormalStateMap.end()) {
                return false; // Invalid control value
            }
            control[i] = newControl[i];
        }
        return true;
    }

    void checkCompleteImmunity() {
        if (complete_immunity < 0 || complete_immunity > 100) {
            complete_immunity = 0; // Reset to 0 if invalid
        }
    }
    
    void applyResistances(const std::vector<AbnormalState>& abnormalStates) const {
        for (const auto& state : abnormalStates) {
            if (!isResistantTo(state)) {
                // state.applyEffect();
            }
        }
    }

    bool isResistantTo(const AbnormalState& state) const {
        // Check if the resistance system has a resistance to the given state
        srand(time(0));
        for (const auto& res : control) {
            if (res.first == state.id) {
                return rand() % 100 < res.second;
            }
        }
        for (const auto& res : uncontrol) {
            if (res.first == state.id) {
                return rand() % 100 < res.second;
            }
        }
        if (uncontrol[0].second + uncontrol[1].second + uncontrol[2].second == 50) {
            return false; // Invalid uncontrol values
        }
        return false;
    }

private:
    std::pair<int, int> control[3];
    std::pair<int, int> uncontrol[3];
    int complete_immunity;
    int harm[3];    //0: baoji, 1: gushang, 2: baifengbi
};

#endif // RESISTANCE_SYSTEM_H