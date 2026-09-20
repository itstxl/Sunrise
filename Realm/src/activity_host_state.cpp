#include <limits>
#include <sunrise/realm/activity_host_state.h>

namespace sunrise::realm {
namespace {

[[nodiscard]] bool nonzero(const Identifier& identifier) noexcept {
    for (const std::byte value : identifier.bytes) {
        if (value != std::byte{}) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool valid(const ActivitySpec& spec) noexcept {
    return nonzero(spec.activityId) && spec.activityDefinitionHash != 0 && spec.scenarioTagHash != 0
           && spec.modeDefinitionHash != 0 && spec.maximumPlayers >= 2
           && spec.maximumPlayers <= kMaximumFfaPlayers && spec.scoreLimit != 0
           && spec.timeLimitSeconds != 0
           && spec.timeLimitSeconds <= (std::numeric_limits<std::uint32_t>::max)() / 1000
           && spec.respawnDelayMilliseconds != 0;
}

} // namespace

ActivityAllocationResult ActivityHostState::allocate(const ActivitySpec& spec) noexcept {
    if (!valid(spec)) {
        return {spec.activityId, ActivityAllocationStatus::invalidSpec};
    }
    if (activity_.has_value()) {
        return {spec.activityId,
                activity_->activityId == spec.activityId
                    ? ActivityAllocationStatus::duplicate
                    : ActivityAllocationStatus::capacityUnavailable};
    }
    const FfaRules rules{spec.maximumPlayers,
                         spec.scoreLimit,
                         spec.timeLimitSeconds * 1000,
                         spec.respawnDelayMilliseconds};
    try {
        activity_ = spec;
        ffaMatch_.emplace(rules);
    } catch (...) {
        activity_.reset();
        ffaMatch_.reset();
        return {spec.activityId, ActivityAllocationStatus::capacityUnavailable};
    }
    return {spec.activityId, ActivityAllocationStatus::accepted};
}

bool ActivityHostState::release(const Identifier& activityId) noexcept {
    if (!activity_.has_value() || activity_->activityId != activityId) {
        return false;
    }
    ffaMatch_.reset();
    activity_.reset();
    return true;
}

const ActivitySpec* ActivityHostState::activity() const noexcept {
    return activity_.has_value() ? &*activity_ : nullptr;
}

FfaMatch* ActivityHostState::ffa_match() noexcept {
    return ffaMatch_.has_value() ? &*ffaMatch_ : nullptr;
}

const FfaMatch* ActivityHostState::ffa_match() const noexcept {
    return ffaMatch_.has_value() ? &*ffaMatch_ : nullptr;
}

} // namespace sunrise::realm
