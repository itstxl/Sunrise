#pragma once

#include <optional>

#include "contracts.h"
#include "ffa_match.h"

namespace sunrise::realm {

/** Bounded first-slice activity state owned exclusively by one headless host. */
class ActivityHostState {
public:
    [[nodiscard]] ActivityAllocationResult allocate(const ActivitySpec& spec) noexcept;
    [[nodiscard]] bool release(const Identifier& activityId) noexcept;

    [[nodiscard]] const ActivitySpec* activity() const noexcept;
    [[nodiscard]] FfaMatch* ffa_match() noexcept;
    [[nodiscard]] const FfaMatch* ffa_match() const noexcept;

private:
    std::optional<ActivitySpec> activity_{};
    std::optional<FfaMatch> ffaMatch_{};
};

} // namespace sunrise::realm
