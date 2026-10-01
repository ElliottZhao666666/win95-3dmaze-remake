#pragma once

namespace maze {

struct MapPosition { float x, y; };

// The D3D11 left-handed view's screen-right basis differs from an ordinary
// north-up X/Z plot. Reflect X for every map primitive, not just the arrow.
constexpr MapPosition projectMap(int size, float x, float z,
                                 float stepX, float stepY) noexcept {
    return {-.94f + (float(size) - x) * stepX, .9f - z * stepY};
}

} // namespace maze
