#pragma once

#include <cmath>
#include <cstdint>

namespace cinematic {

struct Rect { float x, y, width, height; };
struct Viewport { std::uint32_t x, y, width, height; };

// PC 1.04's render pass stores width/height fractions, x/y fractions,
// and target pixel dimensions. RVA 0x007AF510 truncates their products
// before calling D3D9 SetViewport. Use that pass, not the device's state
// while the movie is still being queued.
inline bool DecodeViewport(const float* pass, Viewport& viewport)
{
    for (int i = 0; i < 6; ++i) {
        if (!std::isfinite(pass[i]) || pass[i] < 0.0f) return false;
    }
    const double width = static_cast<double>(pass[0]) * pass[4];
    const double height = static_cast<double>(pass[1]) * pass[5];
    const double x = static_cast<double>(pass[2]) * pass[4];
    const double y = static_cast<double>(pass[3]) * pass[5];
    // Reject uninitialized or implausible dimensions before integer conversion.
    if (width < 1.0 || height < 1.0 || width > 65536.0 || height > 65536.0 ||
        x > 65536.0 || y > 65536.0 || pass[4] > 65536.0f || pass[5] > 65536.0f ||
        x + width > pass[4] + 1.0 || y + height > pass[5] + 1.0) return false;
    viewport = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return true;
}

// Contain the movie in its supplied rectangle, keeping the center and bounds.
// Already fitted rectangles are unchanged. No cached scale survives a resize,
// and repeated calls cannot progressively squeeze the picture.
inline bool Fit(Rect& rect, std::uint32_t movie_width, std::uint32_t movie_height,
    const Viewport& viewport)
{
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width <= 0.0f || rect.height <= 0.0f ||
        movie_width == 0 || movie_height == 0 || movie_width > 65536 || movie_height > 65536 ||
        viewport.width == 0 || viewport.height == 0) return false;

    const double source_aspect = static_cast<double>(movie_width) / movie_height;
    const double pixel_width = static_cast<double>(rect.width) * viewport.width;
    const double pixel_height = static_cast<double>(rect.height) * viewport.height;
    const double fitted_width = pixel_height * source_aspect;
    const double fitted_height = pixel_width / source_aspect;
    // A small subpixel tolerance absorbs float rounding without hiding a
    // visible aspect error or changing an already fitted rectangle.
    Rect fitted = rect;
    if (pixel_width > fitted_width + 0.01) {
        const float width = static_cast<float>(fitted_width / viewport.width);
        fitted.x += (rect.width - width) * 0.5f;
        fitted.width = width;
    } else if (pixel_height > fitted_height + 0.01) {
        const float height = static_cast<float>(fitted_height / viewport.height);
        fitted.y += (rect.height - height) * 0.5f;
        fitted.height = height;
    } else {
        return false;
    }
    if (!std::isfinite(fitted.x) || !std::isfinite(fitted.y) ||
        fitted.width <= 0.0f || fitted.height <= 0.0f) return false;
    rect = fitted;
    return true;
}

} // namespace cinematic
