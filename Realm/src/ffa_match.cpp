#include <algorithm>
#include <limits>
#include <sunrise/realm/ffa_match.h>

namespace sunrise::realm {

bool FfaMatch::valid() const noexcept {
    return rules_.maximumPlayers >= 2 && rules_.maximumPlayers <= kMaximumFfaPlayers
           && rules_.scoreLimit != 0 && rules_.timeLimitMilliseconds != 0
           && rules_.respawnDelayMilliseconds != 0;
}

FfaStanding* FfaMatch::find(const Identifier& player) noexcept {
    const auto found = std::ranges::find_if(standings_, [&player](const FfaStanding& standing) {
        return standing.occupied && standing.playerId == player;
    });
    return found == standings_.end() ? nullptr : &*found;
}

const FfaStanding* FfaMatch::find(const Identifier& player) const noexcept {
    const auto found = std::ranges::find_if(standings_, [&player](const FfaStanding& standing) {
        return standing.occupied && standing.playerId == player;
    });
    return found == standings_.end() ? nullptr : &*found;
}

AdmissionResult FfaMatch::admit(const Identifier& player) noexcept {
    if (phase_ != MatchPhase::waiting) {
        return AdmissionResult::matchStarted;
    }
    if (find(player) != nullptr) {
        return AdmissionResult::duplicate;
    }
    const std::size_t occupied = static_cast<std::size_t>(std::ranges::count_if(
        standings_, [](const FfaStanding& standing) { return standing.occupied; }));
    if (occupied >= rules_.maximumPlayers) {
        return AdmissionResult::full;
    }
    const auto free = std::ranges::find_if(
        standings_, [](const FfaStanding& standing) { return !standing.occupied; });
    if (free == standings_.end()) {
        return AdmissionResult::full;
    }
    *free = FfaStanding{player, 0, 0, 0, true, true, false};
    return AdmissionResult::accepted;
}

bool FfaMatch::disconnect(const Identifier& player) noexcept {
    FfaStanding* standing = find(player);
    if (standing == nullptr) {
        return false;
    }
    if (phase_ == MatchPhase::waiting) {
        *standing = {};
    } else {
        standing->connected = false;
        standing->alive = false;
    }
    return true;
}

bool FfaMatch::reconnect(const Identifier& player, std::uint64_t now) noexcept {
    FfaStanding* standing = find(player);
    if (standing == nullptr || phase_ == MatchPhase::complete) {
        return false;
    }
    standing->connected = true;
    if (phase_ == MatchPhase::running && now >= standing->respawnAtMilliseconds) {
        standing->alive = true;
        standing->respawnAtMilliseconds = 0;
    }
    return true;
}

bool FfaMatch::start(std::uint64_t now) noexcept {
    if (!valid() || phase_ != MatchPhase::waiting) {
        return false;
    }
    const std::size_t connected =
        static_cast<std::size_t>(std::ranges::count_if(standings_, [](const FfaStanding& standing) {
            return standing.occupied && standing.connected;
        }));
    if (connected < 2) {
        return false;
    }
    for (FfaStanding& standing : standings_) {
        if (standing.occupied && standing.connected) {
            standing.alive = true;
        }
    }
    phase_ = MatchPhase::running;
    startedAt_ = now;
    return true;
}

EliminationResult FfaMatch::eliminate(const std::optional<Identifier>& attacker,
                                      const Identifier& victim,
                                      std::uint64_t now) noexcept {
    service(now);
    if (phase_ != MatchPhase::running) {
        return EliminationResult::matchNotRunning;
    }
    FfaStanding* victimStanding = find(victim);
    if (victimStanding == nullptr) {
        return EliminationResult::unknownVictim;
    }
    if (!victimStanding->connected || !victimStanding->alive) {
        return EliminationResult::victimNotAlive;
    }
    FfaStanding* attackerStanding{};
    if (attacker.has_value() && *attacker != victim) {
        attackerStanding = find(*attacker);
        if (attackerStanding == nullptr) {
            return EliminationResult::unknownAttacker;
        }
        if (!attackerStanding->connected || !attackerStanding->alive) {
            return EliminationResult::attackerNotAlive;
        }
    }
    victimStanding->alive = false;
    ++victimStanding->deaths;
    victimStanding->respawnAtMilliseconds =
        now > (std::numeric_limits<std::uint64_t>::max)() - rules_.respawnDelayMilliseconds
            ? (std::numeric_limits<std::uint64_t>::max)()
            : now + rules_.respawnDelayMilliseconds;
    if (attackerStanding != nullptr) {
        ++attackerStanding->kills;
        if (attackerStanding->kills >= rules_.scoreLimit) {
            phase_ = MatchPhase::complete;
            completion_ = MatchCompletion::scoreLimit;
            winner_ = attackerStanding->playerId;
        }
    }
    return EliminationResult::accepted;
}

void FfaMatch::service(std::uint64_t now) noexcept {
    if (phase_ != MatchPhase::running) {
        return;
    }
    if (now >= startedAt_ && now - startedAt_ >= rules_.timeLimitMilliseconds) {
        complete_by_time();
        return;
    }
    for (FfaStanding& standing : standings_) {
        if (standing.occupied && standing.connected && !standing.alive
            && now >= standing.respawnAtMilliseconds) {
            standing.alive = true;
            standing.respawnAtMilliseconds = 0;
        }
    }
}

void FfaMatch::complete_by_time() noexcept {
    phase_ = MatchPhase::complete;
    completion_ = MatchCompletion::timeLimit;
    std::uint16_t highest{};
    const FfaStanding* leader{};
    bool tied{};
    for (const FfaStanding& standing : standings_) {
        if (!standing.occupied) {
            continue;
        }
        if (leader == nullptr || standing.kills > highest) {
            highest = standing.kills;
            leader = &standing;
            tied = false;
        } else if (standing.kills == highest) {
            tied = true;
        }
    }
    if (leader != nullptr && !tied) {
        winner_ = leader->playerId;
    }
}

} // namespace sunrise::realm
