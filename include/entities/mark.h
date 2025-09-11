#ifndef MARK_H
#define MARK_H

#include <vector>
#include <string>
#include <functional>

#include <fsm/battleContext.h>

//特殊印记  e.g. 神印、战意
class Mark 
{
    public:
        Mark(const std::string& name, std::function<void(std::shared_ptr<BattleContext>)> onMark)
            : name(name), onMark(onMark) {};
        ~Mark() = default;
        std::string name;
        int count;
        std::function<void(std::shared_ptr<BattleContext>)> onMark;
};

#endif