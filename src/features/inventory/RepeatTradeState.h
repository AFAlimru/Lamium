#pragma once
#include <array>

namespace lamium::inventory::repeatTrades {
using Counts = std::array<int, 3>; // buy A, buy B, sell (matching kinds can overlap).
struct Offer {
    int costA = 0, costB = 0, output = 0, uses = 0, maxUses = 0;
    bool unlocked = false, sameInputs = false, sellMatchesA = false, sellMatchesB = false;
};
inline bool materials(Offer const& offer, Counts const& counts) {
    if (offer.costA <= 0 || offer.costA > 255 || offer.costB < 0 || offer.costB > 255
        || offer.output <= 0 || offer.output > 255 || offer.uses < 0
        || offer.maxUses <= offer.uses || !offer.unlocked) return false;
    return offer.sameInputs ? counts[0] >= offer.costA + offer.costB
        : counts[0] >= offer.costA && counts[1] >= offer.costB;
}
inline Counts expected(Offer const& offer, Counts before) {
    before[0] -= offer.costA + (offer.sameInputs ? offer.costB : 0);
    before[1] -= offer.costB + (offer.sameInputs ? offer.costA : 0);
    before[2] += offer.output - (offer.sellMatchesA ? offer.costA : 0) - (offer.sellMatchesB ? offer.costB : 0);
    if (offer.sellMatchesA) before[0] += offer.output;
    if (offer.sellMatchesB) before[1] += offer.output;
    return before;
}
enum class Phase { Idle, Prepare, WaitingPrepare, Trade, WaitingTrade };
enum class Reply { Waiting, Accepted, Failed };
class State {
    Phase current = Phase::Idle;
    Counts target{};
    int confirmedUses = -1;
public:
    Phase phase() const { return current; }
    void start() { current = Phase::Prepare; confirmedUses = -1; }
    void stop() { current = Phase::Idle; confirmedUses = -1; }
    void prepareAgain() { current = Phase::Prepare; }
    void prepared(Counts before, int uses = -1) {
        if (confirmedUses < 0) confirmedUses = uses;
        target = before; current = Phase::WaitingPrepare;
    }
    bool submitted(Offer const& offer, Counts before, int room) {
        if (current != Phase::Trade || !materials(offer, before) || room < offer.output
            || (confirmedUses >= 0 && offer.uses != confirmedUses)) { stop(); return false; }
        target = expected(offer, before);
        confirmedUses = offer.uses + 1;
        current = Phase::WaitingTrade;
        return true;
    }
    // Counts are read only after vanilla applied this request's responses.
    void acknowledge(Reply reply, Counts after, int uses) {
        if (current != Phase::WaitingPrepare && current != Phase::WaitingTrade) return;
        if (reply == Reply::Waiting) return;
        if (reply != Reply::Accepted || after != target
            || (confirmedUses >= 0 && uses != confirmedUses)) { stop(); return; }
        current = Phase::Trade;
    }
};
}
