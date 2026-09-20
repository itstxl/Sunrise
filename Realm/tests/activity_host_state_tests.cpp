#include <cstddef>
#include <iostream>
#include <sunrise/realm/activity_host_state.h>

namespace {

using namespace sunrise::realm;

int g_failures{};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

Identifier identifier(unsigned char value) {
    Identifier result{};
    result.bytes.fill(static_cast<std::byte>(value));
    return result;
}

ActivitySpec valid_spec(unsigned char identity) {
    return {identifier(identity), 0x1000U, 0x80B60015U, 0x813206E7U, 2, 5, 300, 3000};
}

void allocation_lifecycle() {
    ActivityHostState host;
    const ActivitySpec first = valid_spec(1);
    const ActivitySpec second = valid_spec(2);
    expect(host.allocate(first).status == ActivityAllocationStatus::accepted,
           "valid FFA allocation is accepted");
    expect(host.activity() != nullptr && *host.activity() == first,
           "accepted activity is retained");
    expect(host.ffa_match() != nullptr && host.ffa_match()->valid(),
           "accepted activity creates valid FFA authority");
    expect(host.allocate(first).status == ActivityAllocationStatus::duplicate,
           "duplicate activity is idempotently rejected");
    expect(host.allocate(second).status == ActivityAllocationStatus::capacityUnavailable,
           "second activity cannot replace a live allocation");
    expect(!host.release(second.activityId), "unknown activity cannot release allocation");
    expect(host.release(first.activityId), "owner activity releases allocation");
    expect(host.activity() == nullptr && host.ffa_match() == nullptr,
           "release clears activity state");
}

void invalid_spec_is_rejected() {
    ActivityHostState host;
    ActivitySpec invalid = valid_spec(3);
    invalid.maximumPlayers = 1;
    expect(host.allocate(invalid).status == ActivityAllocationStatus::invalidSpec,
           "single-player FFA allocation is invalid");
    invalid = valid_spec(3);
    invalid.scenarioTagHash = 0;
    expect(host.allocate(invalid).status == ActivityAllocationStatus::invalidSpec,
           "missing scenario is invalid");
}

} // namespace

int activity_host_state_tests() {
    allocation_lifecycle();
    invalid_spec_is_rejected();
    return g_failures;
}
