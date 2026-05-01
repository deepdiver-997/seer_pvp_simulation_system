#ifndef ABNORMAL_TYPES_H
#define ABNORMAL_TYPES_H

// 当前工程按 battle_effects 的“异常状态”分组编号作为引擎侧主口径（0~35）。
// effect_des 用于补充展示描述；槽位数沿用 36。
constexpr int kOfficialAbnormalStatusMaxId = 35;
constexpr int kOfficialAbnormalStatusSlotCount = kOfficialAbnormalStatusMaxId + 1;

enum class AbnormalStatusId : int {
    Paralysis = 0,              // 麻痹
    Poison = 1,                 // 中毒
    Burn = 2,                   // 烧伤
    ParasitizeOpponent = 3,     // 寄生对手
    Parasite = 4,               // 寄生
    Frostbite = 5,              // 冻伤
    Fear = 6,                   // 害怕
    Fatigue = 7,                // 疲惫
    Sleep = 8,                  // 睡眠
    Petrify = 9,                // 石化
    Confusion = 10,             // 混乱
    Weakness = 11,              // 衰弱
    MountainGuardian = 12,      // 山神守护
    Flammable = 13,             // 易燃
    Berserk = 14,               // 狂暴
    Icebound = 15,              // 冰封
    Bleed = 16,                 // 流血
    Immunity = 17,              // 免疫
    ImmunityII = 18,            // 免疫（第二槽）
    Crippled = 19,              // 瘫痪
    Blind = 20,                 // 失明
    AbnormalImmunity = 21,      // 异常免疫
    Incinerate = 22,            // 焚烬
    Curse = 23,                 // 诅咒
    FlameCurse = 24,            // 烈焰诅咒
    DeathCurse = 25,            // 致命诅咒
    WeaknessCurse = 26,         // 虚弱诅咒
    Infection = 27,             // 感染
    Bind = 28,                  // 束缚
    Distraction = 29,           // 失神
    Silence = 30,               // 沉默
    Submission = 31,            // 臣服
    Stasis = 32,                // 凝滞
    StarBlessing = 33,          // 星赐
    StarWisdom = 34,            // 星哲
    Overclock = 35,             // 超频
};

// battle_effects.Efftype:
// 0 = 控制类异常, 1 = 弱化/属性变化类, 2 = 特殊 buff/保护类
enum class AbnormalStatusKind : int {
    Control = 0,
    Debuff = 1,
    Special = 2,
    Unknown = -1,
};

inline bool is_valid_abnormal_status_id(int status_id) {
    return status_id >= 0 && status_id <= kOfficialAbnormalStatusMaxId;
}

inline int abnormal_status_id(AbnormalStatusId status_id) {
    return static_cast<int>(status_id);
}

inline const char* abnormal_status_name_cn(AbnormalStatusId status_id) {
    switch (status_id) {
        case AbnormalStatusId::Paralysis: return "麻痹";
        case AbnormalStatusId::Poison: return "中毒";
        case AbnormalStatusId::Burn: return "烧伤";
        case AbnormalStatusId::ParasitizeOpponent: return "寄生对手";
        case AbnormalStatusId::Parasite: return "寄生";
        case AbnormalStatusId::Frostbite: return "冻伤";
        case AbnormalStatusId::Fear: return "害怕";
        case AbnormalStatusId::Fatigue: return "疲惫";
        case AbnormalStatusId::Sleep: return "睡眠";
        case AbnormalStatusId::Petrify: return "石化";
        case AbnormalStatusId::Confusion: return "混乱";
        case AbnormalStatusId::Weakness: return "衰弱";
        case AbnormalStatusId::MountainGuardian: return "山神守护";
        case AbnormalStatusId::Flammable: return "易燃";
        case AbnormalStatusId::Berserk: return "狂暴";
        case AbnormalStatusId::Icebound: return "冰封";
        case AbnormalStatusId::Bleed: return "流血";
        case AbnormalStatusId::Immunity: return "免疫";
        case AbnormalStatusId::ImmunityII: return "免疫（第二槽）";
        case AbnormalStatusId::Crippled: return "瘫痪";
        case AbnormalStatusId::Blind: return "失明";
        case AbnormalStatusId::AbnormalImmunity: return "异常免疫";
        case AbnormalStatusId::Incinerate: return "焚烬";
        case AbnormalStatusId::Curse: return "诅咒";
        case AbnormalStatusId::FlameCurse: return "烈焰诅咒";
        case AbnormalStatusId::DeathCurse: return "致命诅咒";
        case AbnormalStatusId::WeaknessCurse: return "虚弱诅咒";
        case AbnormalStatusId::Infection: return "感染";
        case AbnormalStatusId::Bind: return "束缚";
        case AbnormalStatusId::Distraction: return "失神";
        case AbnormalStatusId::Silence: return "沉默";
        case AbnormalStatusId::Submission: return "臣服";
        case AbnormalStatusId::Stasis: return "凝滞";
        case AbnormalStatusId::StarBlessing: return "星赐";
        case AbnormalStatusId::StarWisdom: return "星哲";
        case AbnormalStatusId::Overclock: return "超频";
        default: return "未知异常";
    }
}

inline const char* abnormal_status_name_cn(int status_id) {
    if (!is_valid_abnormal_status_id(status_id)) {
        return "未知异常";
    }
    return abnormal_status_name_cn(static_cast<AbnormalStatusId>(status_id));
}

inline AbnormalStatusKind abnormal_status_kind(AbnormalStatusId status_id) {
    switch (status_id) {
        case AbnormalStatusId::Paralysis:
        case AbnormalStatusId::ParasitizeOpponent:
        case AbnormalStatusId::Fear:
        case AbnormalStatusId::Fatigue:
        case AbnormalStatusId::Sleep:
        case AbnormalStatusId::Petrify:
        case AbnormalStatusId::Icebound:
        case AbnormalStatusId::Crippled:
        case AbnormalStatusId::Incinerate:
        case AbnormalStatusId::Curse:
        case AbnormalStatusId::Infection:
            return AbnormalStatusKind::Control;
        case AbnormalStatusId::Poison:
        case AbnormalStatusId::Burn:
        case AbnormalStatusId::Parasite:
        case AbnormalStatusId::Frostbite:
        case AbnormalStatusId::Confusion:
        case AbnormalStatusId::Weakness:
        case AbnormalStatusId::Flammable:
        case AbnormalStatusId::Bleed:
        case AbnormalStatusId::Blind:
        case AbnormalStatusId::Bind:
        case AbnormalStatusId::Distraction:
        case AbnormalStatusId::Silence:
        case AbnormalStatusId::Submission:
        case AbnormalStatusId::Stasis:
            return AbnormalStatusKind::Debuff;
        case AbnormalStatusId::MountainGuardian:
        case AbnormalStatusId::Berserk:
        case AbnormalStatusId::Immunity:
        case AbnormalStatusId::ImmunityII:
        case AbnormalStatusId::AbnormalImmunity:
        case AbnormalStatusId::FlameCurse:
        case AbnormalStatusId::DeathCurse:
        case AbnormalStatusId::WeaknessCurse:
        case AbnormalStatusId::StarBlessing:
        case AbnormalStatusId::StarWisdom:
        case AbnormalStatusId::Overclock:
            return AbnormalStatusKind::Special;
        default:
            return AbnormalStatusKind::Unknown;
    }
}

inline AbnormalStatusKind abnormal_status_kind(int status_id) {
    if (!is_valid_abnormal_status_id(status_id)) {
        return AbnormalStatusKind::Unknown;
    }
    return abnormal_status_kind(static_cast<AbnormalStatusId>(status_id));
}

inline bool is_control_abnormal_status(AbnormalStatusId status_id) {
    switch (status_id) {
        case AbnormalStatusId::Paralysis:
        case AbnormalStatusId::ParasitizeOpponent:
        case AbnormalStatusId::Fatigue:
        case AbnormalStatusId::Fear:
        case AbnormalStatusId::Sleep:
        case AbnormalStatusId::Petrify:
        case AbnormalStatusId::Infection:
        case AbnormalStatusId::Icebound:
        case AbnormalStatusId::Crippled:
        case AbnormalStatusId::Incinerate:
        case AbnormalStatusId::Curse:
            return true;
        default:
            return false;
    }
}

inline bool deals_damage_at_action_start(AbnormalStatusId status_id) {
    switch (status_id) {
        case AbnormalStatusId::Poison:
        case AbnormalStatusId::Frostbite:
        case AbnormalStatusId::Burn:
        case AbnormalStatusId::Bleed:
        case AbnormalStatusId::Parasite:
            return true;
        default:
            return false;
    }
}

#endif // ABNORMAL_TYPES_H
