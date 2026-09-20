#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "contracts.h"

namespace sunrise::realm {

inline constexpr std::size_t kMaximumFfaPlayers = 16;

struct FfaRules {
    std::uint16_t maximumPlayers{2};
    std::uint16_t scoreLimit{5};
    std::uint32_t timeLimitMilliseconds{300000};
    std::uint32_t respawnDelayMilliseconds{3000};
};

enum class MatchPhase : std::uint8_t { waiting, running, complete };
enum class MatchCompletion : std::uint8_t { none, scoreLimit, timeLimit };
enum class AdmissionResult : std::uint8_t { accepted, duplicate, full, matchStarted };
enum class EliminationResult : std::uint8_t {
    accepted,
    matchNotRunning,
    unknownVictim,
    victimNotAlive,
    unknownAttacker,
    attackerNotAlive,
};

struct FfaStanding {
    Identifier playerId{};
    std::uint16_t kills{};
    std::uint16_t deaths{};
    std::uint64_t respawnAtMilliseconds{};
    bool occupied{};
    bool connected{};
    bool alive{};
};

/** Deterministic authority for one bounded free-for-all activity. */
class FfaMatch {
public:
    explicit constexpr FfaMatch(FfaRules rules) noexcept : rules_(rules) {}

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] AdmissionResult admit(const Identifier& player) noexcept;
    [[nodiscard]] bool disconnect(const Identifier& player) noexcept;
    [[nodiscard]] bool reconnect(const Identifier& player, std::uint64_t now) noexcept;
    [[nodiscard]] bool start(std::uint64_t now) noexcept;
    [[nodiscard]] EliminationResult eliminate(const std::optional<Identifier>& attacker,
                                              const Identifier& victim,
                                              std::uint64_t now) noexcept;
    void service(std::uint64_t now) noexcept;

    [[nodiscard]] MatchPhase phase() const noexcept {
        return phase_;
    }
    [[nodiscard]] MatchCompletion completion() const noexcept {
        return completion_;
    }
    [[nodiscard]] std::optional<Identifier> winner() const noexcept {
        return winner_;
    }
    [[nodiscard]] std::uint64_t started_at() const noexcept {
        return startedAt_;
    }
    [[nodiscard]] std::span<const FfaStanding> standings() const noexcept {
        return standings_;
    }

private:
    [[nodiscard]] FfaStanding* find(const Identifier& player) noexcept;
    [[nodiscard]] const FfaStanding* find(const Identifier& player) const noexcept;
    void complete_by_time() noexcept;

    FfaRules rules_{};
    MatchPhase phase_{MatchPhase::waiting};
    MatchCompletion completion_{MatchCompletion::none};
    std::uint64_t startedAt_{};
    std::optional<Identifier> winner_{};
    std::array<FfaStanding, kMaximumFfaPlayers> standings_{};
};

} // namespace sunrise::realm
