#ifndef ELEMENTAL_ATTRIBUTES_H
#define ELEMENTAL_ATTRIBUTES_H

#include <iostream>
#include <memory>

#include <db/db_service.h>

//元素克制关系
class ElementalAttributes {
public:
    // static 
    ElementalAttributes();
    ~ElementalAttributes();
    void loadElementalAttributes();

// private:
    static std::vector<std::pair<std::string, int> > elementalAttributes;
    //single elemental attributes
    static std::vector<std::vector<int> > elementalAttributesRestraints;
};

#endif // ELEMENTAL_ATTRIBUTES_H