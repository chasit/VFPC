#pragma once
#include "Constant.hpp"
#include <bitset>
#include <vector>
#include <unordered_map>
#include <string>

enum class ValidationCheck {
    SID_EXISTS,
    SID_DIRECTION,
    SID_AIRWAY_REQUIRED,
    FLIGHT_LEVEL_RESTRICTION,
    DIRECT_AFTER_SID,
    MAX_FLIGHTLEVEL,
    SID_NOT_ALLOWED_FOR_DEST,
    Count
};

constexpr size_t CHECK_COUNT = static_cast<size_t>(ValidationCheck::Count);

extern const std::unordered_map<ValidationCheck, std::string> failure_messages;

struct ValidationContext {
    std::bitset<CHECK_COUNT> results;
    std::vector<ValidationCheck> failed;

    ValidationContext();

    void fail(ValidationCheck check);
    void pass(ValidationCheck check);
    bool isValid() const;
    std::vector<std::string> failureMessages() const;
};
