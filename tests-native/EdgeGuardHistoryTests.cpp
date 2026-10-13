#include "features/interaction/EdgeGuardPlan.h"
#include "mc/entity/components/MoveInputComponent.h"
#include <stdexcept>

void check(bool, char const*);

void edgeGuardHistoryTests() {
    using F = MoveInputState::Flag;
    MoveInputComponent live{};
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) live.mRawInputState->mFlagValues->set(i);
    live.mRawInputState->mLookNormalDirField = 7;
    *live.mInputState->mAnalogMoveVector = Vec2{.03f, .04f};
    *live.mMove = Vec2{.03f, .04f};
    auto replacement = *live.mRawInputState;
    for (auto flag : {F::Up, F::Down, F::Left, F::Right, F::UpLeft, F::UpRight, F::DownLeft, F::DownRight})
        replacement.mFlagValues->set(static_cast<size_t>(flag), false);
    *replacement.mAnalogMoveVector = *live.mInputState->mAnalogMoveVector;
    auto snapshot = lamium::interaction::withReplayInput(*live.mRawInputState, replacement, [&] { return live; });
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) {
        check(snapshot.mRawInputState->mFlagValues->test(i) == replacement.mFlagValues->test(i),
              "the SDK history snapshot owns the guarded flags after live input restoration");
        check(live.mRawInputState->mFlagValues->test(i), "all physical input flags return after SDK snapshot capture");
    }
    check(snapshot.mRawInputState->mFlagValues->test(static_cast<size_t>(F::JumpDown))
          && snapshot.mRawInputState->mFlagValues->test(static_cast<size_t>(F::SneakDown))
          && snapshot.mRawInputState->mLookNormalDirField == 7, "native jump, sneak and look fields survive capture");
    check(snapshot.mRawInputState->mAnalogMoveVector->x == .03f && snapshot.mRawInputState->mAnalogMoveVector->z == .04f
          && live.mRawInputState->mAnalogMoveVector->x == 0 && live.mMove->x == .03f,
          "the SDK snapshot retains guarded analog input without changing live raw or processed movement");
    try {
        lamium::interaction::withReplayInput(*live.mRawInputState, replacement, [] { throw std::runtime_error("capture failure"); });
    } catch (std::runtime_error const&) {}
    check(live.mRawInputState->mFlagValues->test(static_cast<size_t>(F::Up)) && live.mRawInputState->mAnalogMoveVector->x == 0,
          "SDK physical input is restored when history capture unwinds");
}
