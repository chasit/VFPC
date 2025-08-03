#include "stdafx.h"
#include "validationContext.hpp"

const std::unordered_map<ValidationCheck, std::string> failure_messages = {
    { ValidationCheck::SID_EXISTS, "SID" },
    { ValidationCheck::SID_DIRECTION, "E/O" },
    { ValidationCheck::SID_AIRWAY_REQUIRED, "AWY" },
    { ValidationCheck::FLIGHT_LEVEL_RESTRICTION, "FLR" },
    { ValidationCheck::DIRECT_AFTER_SID, "DCT" },
    { ValidationCheck::MAX_FLIGHT_LEVEL, "MAX" },
    { ValidationCheck::SID_NOT_ALLOWED_FOR_DEST, "DST" },
    { ValidationCheck::FORBIDDEN_FLIGHT_LEVEL, "FLR" },
    
};

ValidationContext::ValidationContext() {
    results.set();  // All checks pass by default
}

void ValidationContext::fail(ValidationCheck check) {
    results.set(static_cast<size_t>(check), false);
    failed.push_back(check);
}

void ValidationContext::pass(ValidationCheck check) {
    results.set(static_cast<size_t>(check), true);
}

bool ValidationContext::isValid() const {
    return results.all();
}

std::vector<std::string> ValidationContext::failureMessages() const {
    std::vector<std::string> msgs;
    for (auto check : failed) {
        auto it = failure_messages.find(check);
        if (it != failure_messages.end()) {
            msgs.push_back(it->second);
        }
    }
    return msgs;
}
