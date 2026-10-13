#include "features/inventory/RepeatTradeState.h"
void check(bool, char const*);
void repeatTradeTests() {
    using namespace lamium::inventory::repeatTrades;
    check(!inputsCanReturn({{64, 0}, {64, 0}, {0, 0}, 0}), "refilling cannot drop inputs into a full inventory");
    check(inputsCanReturn({{64, 0}, {64, 0}, {0, 0}, 1}), "one empty slot can return a full input stack");
    check(inputsCanReturn({{64, 0}, {64, 0}, {64, 0}, 0}), "matching partial stacks can receive inputs without an empty slot");
    check(!inputsCanReturn({{64, 1}, {64, 64}, {0, 0}, 1}), "two different inputs cannot share one empty return slot");
    check(inputsCanReturn({{63, 1}, {64, 64}, {0, 0}, 1, true}), "matching inputs can share one return slot");
    check(!inputsCanReturn({{64, 1}, {64, 64}, {0, 0}, 1, true}), "matching input overflow still requires a second slot");
    check(!inputsCanReturn({{32, 32}, {64, 64}, {32, 32}, 0, true}), "matching inputs do not double count partial-stack room");
    Offer offer{15, 0, 1, 3, 16, true};
    check(materials(offer, {64, 0, 3}) && !materials(offer, {14, 0, 3}), "one trade requires the actual price");
    check(expected(offer, {64, 0, 3}) == Counts{49, 0, 4}, "one coal trade consumes 15 and adds one emerald");
    State state;
    state.start();
    state.prepared({64, 0, 3});
    state.acknowledge(Reply::Waiting, {64, 0, 3}, 3);
    check(state.phase() == Phase::WaitingPrepare, "predicted preparation never starts another request");
    state.acknowledge(Reply::Accepted, {64, 0, 3}, 3);
    check(state.submitted(offer, {64, 0, 3}, 61), "acknowledged preparation allows one request");
    state.acknowledge(Reply::Waiting, {49, 0, 4}, 4);
    check(state.phase() == Phase::WaitingTrade, "predicted inventory and uses do not advance a trade");
    state.acknowledge(Reply::Accepted, {49, 0, 4}, 4);
    offer.uses = 4;
    check(state.submitted(offer, {49, 0, 4}, 60), "a matching authoritative response permits the next round");
    state.acknowledge(Reply::Accepted, {34, 0, 4}, 5);
    check(state.phase() == Phase::Idle, "missing output stops even with a successful response");
    for (auto reply : {Reply::Failed, Reply::Accepted}) {
        state.start(); state.prepared({64, 0, 3});
        state.acknowledge(reply, {63, 0, 3}, 3);
        check(state.phase() == Phase::Idle, "rejected or externally changed preparation stops");
    }
    state.start(); state.prepared({64, 0, 3}); state.acknowledge(Reply::Accepted, {64, 0, 3}, 4);
    check(!state.submitted(offer, {64, 0, 3}, 0), "a full output destination cannot send a trade");
    Offer book{12, 1, 1, 0, 12, true};
    check(!materials(book, {12, 0, 0}) && materials(book, {12, 1, 0}), "two-input book trade needs both inputs");
    check(expected(book, {24, 2, 0}) == Counts{12, 1, 1}, "two-input trades verify both costs and the output");
    Offer same{3, 2, 1, 0, 12, true, true};
    check(!materials(same, {4, 4, 0}) && materials(same, {5, 5, 0}), "same-kind inputs share one budget");
    check(expected(same, {10, 10, 0}) == Counts{5, 5, 1}, "same-kind inputs are not counted as two sources");
    book.unlocked = false;
    check(!materials(book, {64, 64, 0}), "locked offers never run");
    offer.uses = offer.maxUses;
    check(!materials(offer, {64, 0, 0}), "sold-out offers never run");
    state.start(); state.prepared({64, 0, 0}); state.stop();
    state.acknowledge(Reply::Accepted, {64, 0, 0}, 0);
    check(state.phase() == Phase::Idle, "late completion after cancellation cannot restart");
    offer.uses = 3;
    state.start(); state.prepared({64, 0, 3}); state.acknowledge(Reply::Accepted, {64, 0, 3}, 3);
    state.submitted(offer, {64, 0, 3}, 61);
    state.acknowledge(Reply::Accepted, {49, 0, 4}, 5);
    check(state.phase() == Phase::Idle, "an unexpected offer-use change stops the session");
}
