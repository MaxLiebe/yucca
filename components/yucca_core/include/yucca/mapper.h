// Table-driven mapping from canonical buttons to a target's button bits.
#pragma once

#include <cstdint>

#include "yucca/gamepad.h"

namespace yucca {

enum class FaceLayout : uint8_t {
    Position,  // a console's buttons follow the controller's positions (default)
    Label,     // follow the labels of an Xbox-style controller (A south, B east)
};

struct ButtonRoute {
    Button from;
    uint32_t to;  // target-defined bit mask; several routes may share a bit
};

class Mapper {
public:
    // `routes` must outlive the mapper (normally a static table in the target).
    Mapper(const ButtonRoute *routes, uint8_t count) : routes_(routes), count_(count) {}

    void set_layout(FaceLayout layout) { layout_ = layout; }
    FaceLayout layout() const { return layout_; }

    // Target button mask for the pressed buttons in `state`.
    uint32_t map(const GamepadState &state) const;

    // The position a canonical face button takes after the layout is applied.
    Button face(Button b) const;

private:
    const ButtonRoute *routes_;
    uint8_t count_;
    FaceLayout layout_ = FaceLayout::Position;
};

}  // namespace yucca
