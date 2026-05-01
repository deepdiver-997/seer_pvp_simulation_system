#include <entities/elemental-attributes.h>

std::vector<std::pair<std::string, int>> ElementalAttributes::elementalAttributes;
std::vector<std::vector<int>> ElementalAttributes::elementalAttributesRestraints(64, std::vector<int>(64, 1));

ElementalAttributes::ElementalAttributes() = default;

ElementalAttributes::~ElementalAttributes() = default;

void ElementalAttributes::loadElementalAttributes() {
    // TODO: load real restraint table from official data source.
    // Keep neutral multipliers (1.0) as a safe fallback.
}
