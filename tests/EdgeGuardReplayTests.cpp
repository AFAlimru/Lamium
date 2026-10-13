#include "features/interaction/EdgeGuardReplayPlan.h"
#include "features/interaction/EdgeGuardPlan.h"
#include <array>
#include <bitset>
#include <optional>

void check(bool, char const*);

void edgeGuardReplayTests() {
    struct Vector { double x = 0, z = 0; bool operator==(Vector const&) const = default; };
    struct State {
        std::optional<std::bitset<10>> mFlagValues{std::in_place};
        std::optional<Vector> mAnalogMoveVector{std::in_place};
        int look = 0;
    };
    struct Input { std::optional<State> mInputState{std::in_place}, mRawInputState{std::in_place}; Vector move; };
    constexpr std::array<size_t, 8> directions{0,1,2,3,4,5,6,7};
    Input live;
    live.mRawInputState->mFlagValues->set();
    live.mRawInputState->look = 7;
    *live.mInputState->mAnalogMoveVector = live.move = {0.170, -0.145};
    auto copy = lamium::interaction::replayInputCopy(live, directions);
    check(live.mRawInputState->mFlagValues->all() && *live.mRawInputState->mAnalogMoveVector == Vector{},
          "creating captured input leaves live physical directions and analog input untouched");
    check(*copy.mRawInputState->mFlagValues == std::bitset<10>{0b1100000000}
          && *copy.mRawInputState->mAnalogMoveVector == live.move && copy.mRawInputState->look == 7,
          "only horizontal raw fields are replaced in the owned capture copy");
    copy.mRawInputState->mFlagValues->set(0);
    check(!live.mInputState->mFlagValues->test(0), "the snapshot never aliases live processed input");
    auto [brake, sideways] = lamium::interaction::guardInput(1, 0, .05, 0, .1, .546,
        [](double dx, double) { return .139 - dx > .10; });
    (void)sideways;
    *live.mInputState->mAnalogMoveVector = live.move = {brake, 0};
    auto history = lamium::interaction::replayInputCopy(live, directions);
    // A later key release/device refresh changes only live input.
    live.mRawInputState->mFlagValues->reset();
    *live.mInputState->mAnalogMoveVector = {};
    auto overlap = [](double move) { return .139 - .047 - (.05 * .546 + move * .1); };
    check(overlap(1) < 0 && overlap(history.mRawInputState->mAnalogMoveVector->x) > 0,
          "the captured brake survives later device changes and prevents the modeled correction replay from crossing the edge");
}
