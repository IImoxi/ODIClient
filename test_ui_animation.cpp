#include "ui_animation.h"
#include <cassert>
#include <cmath>

int main() {
    const float endpoints[] = {-1.0f, 0.0f, 1.0f, 2.0f};
    for (float t : endpoints) {
        float expected = t <= 0 ? 0 : 1;
        assert(ui_animation::ease_out_quart(t) == expected);
        // Endpoints must not need the optional host math adapter.
        assert(ui_animation::ease_out_exponential(t, nullptr) == expected);
    }
    assert(ui_animation::ease_out_quart(0.5f) == 0.9375f);
    assert(ui_animation::ease_out_exponential(0.5f, std::exp2) == 0.96875f);
    float previousQuart = 0, previousExponential = 0;
    for (int i = 0; i <= 100; ++i) {
        float t = i / 100.0f;
        float quart = ui_animation::ease_out_quart(t);
        float exponential = ui_animation::ease_out_exponential(t, std::exp2);
        assert(quart >= previousQuart && quart <= 1);
        assert(exponential >= previousExponential && exponential <= 1);
        previousQuart = quart;
        previousExponential = exponential;
    }
}
