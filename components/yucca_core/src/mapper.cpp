#include "yucca/mapper.h"

namespace yucca {

Button Mapper::face(Button b) const
{
    if (layout_ == FaceLayout::Position) {
        return b;
    }
    // Label layout: an Xbox-style controller's A (south) presses the console's
    // A, which Nintendo puts east; likewise B<->A and X<->Y.
    switch (b) {
    case Button::South: return Button::East;
    case Button::East: return Button::South;
    case Button::West: return Button::North;
    case Button::North: return Button::West;
    default: return b;
    }
}

uint32_t Mapper::map(const GamepadState &state) const
{
    uint32_t out = 0;
    for (uint8_t i = 0; i < count_; i++) {
        // routes_[i].from names the console's button position; find which
        // controller button lands there under the current layout.
        if (state.pressed(face(routes_[i].from))) {
            out |= routes_[i].to;
        }
    }
    return out;
}

}  // namespace yucca
