#ifndef NUMERICAL_PROPERTIES_H
#define NUMERICAL_PROPERTIES_H

#include <stdexcept>

enum class NumericalPropertyIndex {
    PHYSICAL_ATTACK = 0,
    SPECIAL_ATTACK = 1,
    DEFENSE = 2,
    SPECIAL_DEFENSE = 3,
    SPEED = 4,
    HP = 5
};

//精灵属性
class numerical_properties
{
// private:
public:
    // int hp;
    // int speed;
    // int attack;
    // int defense;
    // int specialAttack;
    // int specialDefense;
    mutable int arr[6] = {0};   // 0:attack 1:specialAttack 2:defense 3:specialDefense 4:speed 5:hp
    numerical_properties();
    ~numerical_properties();
    int & operator[](NumericalPropertyIndex i)const {
        int index = static_cast<int>(i);
        if (index < 0 || index >= 6) {
            throw std::out_of_range("Index out of range");
        }
        return arr[index];
    }
};

#endif // NUMERICAL_PROPERTIES_H