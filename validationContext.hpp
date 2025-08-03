#pragma once
#include "Constant.hpp"
#include <bitset>
#include <vector>
#include <unordered_map>
#include <string>

enum class ValidationCheck
{
    SID_ERROR,
    ROUTE_ERROR,
    LEVEL_ERROR,
    Count
};

constexpr size_t CHECK_COUNT = static_cast<size_t>(ValidationCheck::Count);

extern const std::unordered_map<ValidationCheck, std::string> failure_messages;

struct ValidationContext
{
    std::bitset<CHECK_COUNT> results;
    std::vector<ValidationCheck> failed;

    ValidationContext();

    void fail(ValidationCheck check);
    void pass(ValidationCheck check);
    bool isValid() const;
    std::vector<std::string> failureMessages() const;
};
