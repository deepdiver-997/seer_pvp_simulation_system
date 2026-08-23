#include <entities/elemental-attributes.h>

#include <db/official_data_repository.h>

std::vector<std::pair<std::string, int>> ElementalAttributes::elementalAttributes;
std::vector<std::vector<double>> ElementalAttributes::elementalAttributesRestraints(256, std::vector<double>(256, 1.0));

ElementalAttributes::ElementalAttributes() = default;

ElementalAttributes::~ElementalAttributes() = default;

void ElementalAttributes::loadElementalAttributes() {
    // 克制表来自官方 types_relation（新 Unity 数据，H5 缺失）。
    // 单属性 id 最大 226（混沌/虚空等），矩阵取 256 保证下标不越界。
    elementalAttributesRestraints.assign(256, std::vector<double>(256, 1.0));

    auto& store = official_data::OfficialDataStore::instance();
    if (!store.ready()) {
        return;
    }
    store.repository().load_elemental_restraints(elementalAttributesRestraints);
}
