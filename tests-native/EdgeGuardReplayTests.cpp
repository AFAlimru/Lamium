#include "features/interaction/EdgeGuardReplayPlan.h"
#include "features/interaction/EdgeGuardPacketPlan.h"
#include "mc/entity/components/MoveInputComponent.h"
#include <array>

void check(bool, char const*);

void nativeEdgeGuardReplayTests() {
    using F = MoveInputState::Flag;
    constexpr std::array directions{
        static_cast<size_t>(F::Up), static_cast<size_t>(F::Down),
        static_cast<size_t>(F::Left), static_cast<size_t>(F::Right),
        static_cast<size_t>(F::UpLeft), static_cast<size_t>(F::UpRight),
        static_cast<size_t>(F::DownLeft), static_cast<size_t>(F::DownRight)};
    MoveInputComponent live{};
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) live.mRawInputState->mFlagValues->set(i);
    live.mRawInputState->mLookNormalDirField = 7;
    live.mHoldAutoJumpInWaterTicks = 5;
    *live.mLookDelta = Vec2{3, 4};
    *live.mInputState->mAnalogMoveVector = Vec2{.170f, -.145f};
    *live.mMove = Vec2{.170f, -.145f};
    auto captured = lamium::interaction::replayInputCopy(live, directions);
    for (size_t i = 0; i < static_cast<size_t>(F::Count); ++i) {
        bool horizontal = false;
        for (auto index : directions) horizontal |= i == index;
        check(captured.mRawInputState->mFlagValues->test(i) == !horizontal,
              "the SDK capture copy preserves every non-horizontal raw flag");
        check(live.mRawInputState->mFlagValues->test(i), "capture never clears any live physical flag");
    }
    check(captured.mRawInputState->mAnalogMoveVector->x == .170f && captured.mRawInputState->mAnalogMoveVector->z == -.145f
          && live.mRawInputState->mAnalogMoveVector->x == 0 && live.mRawInputState->mAnalogMoveVector->z == 0,
          "the captured guarded analog vector is independent of live physical input");
    check(captured.mRawInputState->mLookNormalDirField == 7 && captured.mHoldAutoJumpInWaterTicks == 5
          && captured.mLookDelta->x == 3 && captured.mMove->z == -.145f,
          "look, component state and processed movement survive the SDK capture copy");
    live.mRawInputState->mFlagValues->set(static_cast<size_t>(F::Down), false);
    *live.mInputState->mAnalogMoveVector = Vec2{};
    check(captured.mRawInputState->mAnalogMoveVector->x == .170f && captured.mInputState->mAnalogMoveVector->x == .170f,
          "later device refreshes cannot change either captured SDK input state");
    MoveInputComponent pad{};
    *pad.mRawInputState->mAnalogMoveVector = Vec2{1, -1};
    *pad.mInputState->mAnalogMoveVector = Vec2{.2f, -.3f};
    auto padCapture = lamium::interaction::replayInputCopy(pad, directions);
    check(padCapture.mRawInputState->mAnalogMoveVector->x == .2f && pad.mRawInputState->mAnalogMoveVector->x == 1,
          "controller-style analog capture also leaves live device input untouched");
    Vec2 packetRaw{1, -1}, packetMove{.06f, -.09f}, packetAnalog = *pad.mInputState->mAnalogMoveVector;
    *pad.mMove = packetMove;
    check(lamium::interaction::alignGuardedPacketRaw(packetRaw, packetMove, packetAnalog,
              *pad.mMove, *pad.mInputState->mAnalogMoveVector, false, false)
          && packetRaw.x == .2f && packetRaw.z == -.3f
          && pad.mRawInputState->mAnalogMoveVector->x == 1 && pad.mRawInputState->mAnalogMoveVector->z == -1,
          "aligning the SDK packet vector never feeds guarded analog movement into live device input");
    packetRaw = Vec2{1, -1};
    packetMove.x += .01f;
    check(!lamium::interaction::alignGuardedPacketRaw(packetRaw, packetMove, packetAnalog,
              *pad.mMove, *pad.mInputState->mAnalogMoveVector, false, false) && packetRaw.x == 1,
          "a mismatched SDK packet move is passed through without writes");
}
