#ifndef BATTLE_H
#define BATTLE_H

#include <chrono>
#include <entities/seer-robot.h>
#include <effects/effect.h>
#include <battleFsm.h>

struct BattleContext {
};

class Battle {
public:
    Battle() = delete; // Default constructor
    Battle(const Battle&) = delete; // Copy constructor
    Battle& operator=(const Battle&) = delete; // Copy assignment operator
    Battle(Battle&&) = delete; // Move constructor
    Battle& operator=(Battle&&) = delete; // Move assignment operator
    Battle(const SeerRobot& seerRobot1, const SeerRobot& seerRobot2)
        : seerRobot1(seerRobot1), seerRobot2(seerRobot2) {}
    ~Battle() = default; // Default destructor
    void startBattle();
    void endBattle();
private:
    SeerRobot seerRobot1;
    SeerRobot seerRobot2;
    bool host;  //true-->seerRobot1 is the host, false-->seerRobot2 is the host
};

#endif // BATTLE_H