#include "features/interaction/EdgeGuardPlan.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/RawMoveInputComponent.h"
#include <array>

void check(bool, char const*);

void edgeGuardHistoryTests() {
    using F = MoveInputState::Flag;
    constexpr std::array directions{
        static_cast<size_t>(F::Up), static_cast<size_t>(F::Down),
        static_cast<size_t>(F::Left), static_cast<size_t>(F::Right),
        static_cast<size_t>(F::UpLeft), static_cast<size_t>(F::UpRight),
        static_cast<size_t>(F::DownLeft), static_cast<size_t>(F::DownRight)};
    MoveInputComponent live{};
    RawMoveInputComponent device{};
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) live.mRawInputState->mFlagValues->set(i);
    live.mRawInputState->mLookNormalDirField = 7;
    *device.mRawInput = *live.mRawInputState;
    *device.mRawMove = Vec2{1, -1};
    *live.mInputState->mAnalogMoveVector = Vec2{.03f, .04f};
    *live.mMove = Vec2{.03f, .04f};
    lamium::interaction::copyHorizontalInput(*live.mRawInputState->mFlagValues, *live.mRawInputState->mAnalogMoveVector,
        *live.mInputState->mFlagValues, *live.mInputState->mAnalogMoveVector, directions);
    auto snapshot = live;
    // A later device refresh must not change the owned history snapshot.
    *live.mRawInputState = *device.mRawInput;
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) {
        bool horizontal = false;
        for (auto index : directions) horizontal |= i == index;
        check(snapshot.mRawInputState->mFlagValues->test(i) == !horizontal,
              "history synchronization preserves every non-horizontal physical flag");
        check(device.mRawInput->mFlagValues->test(i) && live.mRawInputState->mFlagValues->test(i),
              "physical device input stays intact and can refresh the next frame");
    }
    check(snapshot.mRawInputState->mLookNormalDirField == 7, "history synchronization preserves look fields");
    check(snapshot.mRawInputState->mAnalogMoveVector->x == .03f && snapshot.mRawInputState->mAnalogMoveVector->z == .04f
          && snapshot.mMove->x == .03f && device.mRawMove->x == 1 && device.mRawMove->z == -1,
          "delayed SDK captures keep guarded analog movement without changing physical device movement");
}
