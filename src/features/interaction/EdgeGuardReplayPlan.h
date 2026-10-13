#pragma once

namespace lamium::interaction {
template <class Input, class Directions>
Input replayInputCopy(Input const& live, Directions const& directions) {
    Input copy = live;
    for (auto index : directions)
        copy.mRawInputState->mFlagValues->set(index, live.mInputState->mFlagValues->test(index));
    *copy.mRawInputState->mAnalogMoveVector = *live.mInputState->mAnalogMoveVector;
    return copy;
}
}
