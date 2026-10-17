#include "yucca/gamepad.h"

#include <cmath>

namespace yucca {

void apply_radial_deadzone(float &x, float &y, float deadzone)
{
    const float mag = std::sqrt(x * x + y * y);
    if (mag < deadzone) {
        x = 0;
        y = 0;
        return;
    }
    float scale = (mag - deadzone) / (1.0f - deadzone) / mag;
    if (mag * scale > 1.0f) {
        scale = 1.0f / mag;
    }
    x *= scale;
    y *= scale;
}

}  // namespace yucca
