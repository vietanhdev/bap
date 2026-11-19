#pragma once
// Cleans up the raw readings of the touch controller before they become gestures.
//   - A finger counts as lifted only after several empty readings in a row. The controller
//     sometimes reports nothing for one reading while the finger is still down, which used to end
//     a drag or turn one tap into two.
//   - A single reading far from the finger is held back until the next reading confirms it.
// No device headers here, so the rules are tested on the host (firmware/tests/test_touch_filter.cc).

namespace assistant {

class TouchFilter {
public:
    enum class Event { kNone, kDown, kMove, kUp };
    struct Result {
        Event event = Event::kNone;
        int x = 0, y = 0;
    };

    static constexpr int kLiftReadings = 3;  // 75 ms at the 25 ms polling period
    static constexpr int kJumpPx = 90;

    bool down() const { return down_; }

    Result Feed(bool touched, int x, int y) {
        if (!touched) {
            if (!down_ || ++missed_ < kLiftReadings)
                return {};
            down_ = false;
            pending_ = false;
            missed_ = 0;
            return {Event::kUp, x_, y_};
        }
        missed_ = 0;
        if (!down_) {
            down_ = true;
            pending_ = false;
            x_ = x;
            y_ = y;
            return {Event::kDown, x, y};
        }
        if (Far(x, y, x_, y_) && !(pending_ && !Far(x, y, pending_x_, pending_y_))) {
            pending_ = true;
            pending_x_ = x;
            pending_y_ = y;
            return {};
        }
        pending_ = false;
        x_ = x;
        y_ = y;
        return {Event::kMove, x, y};
    }

private:
    static bool Far(int x1, int y1, int x2, int y2) {
        const int dx = x1 - x2, dy = y1 - y2;
        return dx * dx + dy * dy > kJumpPx * kJumpPx;
    }

    bool down_ = false, pending_ = false;
    int missed_ = 0;
    int x_ = 0, y_ = 0, pending_x_ = 0, pending_y_ = 0;
};

}  // namespace assistant
