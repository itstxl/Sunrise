#include <cstddef>
#include <iostream>
#include <optional>
#include <sunrise/realm/ffa_match.h>

namespace {

using namespace sunrise::realm;

int g_failures{};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

Identifier player(unsigned char value) {
    Identifier result{};
    result.bytes.fill(static_cast<std::byte>(value));
    return result;
}

const FfaStanding* standing(const FfaMatch& match, const Identifier& id) {
    for (const FfaStanding& candidate : match.standings()) {
        if (candidate.occupied && candidate.playerId == id) {
            return &candidate;
        }
    }
    return nullptr;
}

void score_limit_and_respawn() {
    const Identifier first = player(1);
    const Identifier second = player(2);
    FfaMatch match({2, 2, 300000, 3000});
    expect(match.valid(), "first-slice rules are valid");
    expect(match.admit(first) == AdmissionResult::accepted, "first player is admitted");
    expect(match.admit(second) == AdmissionResult::accepted, "second player is admitted");
    expect(match.admit(first) == AdmissionResult::duplicate, "duplicate identity is rejected");
    expect(match.start(1000), "two-player match starts");
    expect(match.eliminate(first, second, 2000) == EliminationResult::accepted,
           "first elimination is accepted");
    expect(standing(match, first)->kills == 1, "attacker receives kill credit");
    expect(!standing(match, second)->alive, "victim is dead before respawn");
    match.service(4999);
    expect(!standing(match, second)->alive, "victim does not respawn early");
    match.service(5000);
    expect(standing(match, second)->alive, "victim respawns after three seconds");
    expect(match.eliminate(first, second, 6000) == EliminationResult::accepted,
           "winning elimination is accepted");
    expect(match.phase() == MatchPhase::complete, "score limit completes match");
    expect(match.completion() == MatchCompletion::scoreLimit, "score completion is reported");
    expect(match.winner() == first, "score leader wins");
}

void time_limit_and_draw() {
    const Identifier first = player(3);
    const Identifier second = player(4);
    FfaMatch match({2, 5, 5000, 3000});
    expect(match.admit(first) == AdmissionResult::accepted, "time test admits first player");
    expect(match.admit(second) == AdmissionResult::accepted, "time test admits second player");
    expect(match.start(100), "time test starts");
    match.service(5099);
    expect(match.phase() == MatchPhase::running, "match runs until exact deadline");
    match.service(5100);
    expect(match.phase() == MatchPhase::complete, "deadline completes match");
    expect(match.completion() == MatchCompletion::timeLimit, "time completion is reported");
    expect(!match.winner().has_value(), "equal scores produce a draw");
}

void invalid_eliminations_and_reconnect() {
    const Identifier first = player(5);
    const Identifier second = player(6);
    const Identifier unknown = player(7);
    FfaMatch match({2, 5, 300000, 3000});
    expect(match.admit(first) == AdmissionResult::accepted, "reconnect test admits first player");
    expect(match.admit(second) == AdmissionResult::accepted, "reconnect test admits second player");
    expect(match.start(0), "reconnect test starts");
    expect(match.eliminate(first, unknown, 10) == EliminationResult::unknownVictim,
           "unknown victim is rejected");
    expect(match.disconnect(first), "known player disconnects");
    expect(match.eliminate(first, second, 20) == EliminationResult::attackerNotAlive,
           "disconnected attacker cannot score");
    expect(match.reconnect(first, 30), "known player reconnects");
    expect(match.eliminate(first, second, 40) == EliminationResult::accepted,
           "reconnected attacker can score");
    expect(match.eliminate(first, second, 50) == EliminationResult::victimNotAlive,
           "dead victim cannot be eliminated twice");
}

} // namespace

int activity_host_state_tests();

int main() {
    score_limit_and_respawn();
    time_limit_and_draw();
    invalid_eliminations_and_reconnect();
    g_failures += activity_host_state_tests();
    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "sunrise realm activity tests passed\n";
    return 0;
}
