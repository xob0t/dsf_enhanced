// Built and run by build.ps1 before packaging.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "../src/cinematic_fit.h"

struct Region { const void* data; size_t size; };
std::vector<Region> regions;
bool IsReadableMemory(const void* data, size_t size)
{
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    for (const auto& region : regions) {
        const auto start = reinterpret_cast<std::uintptr_t>(region.data);
        if (address >= start && address - start <= region.size &&
            size <= region.size - (address - start)) return true;
    }
    return false;
}
DWORD CallerRva(void* address)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= 0x400000 && value < 0x1800000 ? static_cast<DWORD>(value - 0x400000) : 0;
}
void* tracked_cinematic = nullptr;
bool IsTrackedCinematicHandle(void* handle) { return handle != nullptr && handle == tracked_cinematic; }
#include "../src/cinematic_movie_context.inl"

void* Address(DWORD rva) { return reinterpret_cast<void*>(0x400000 + rva); }
void Near(float actual, float expected) { assert(std::fabs(actual - expected) < 0.000002f); }
void Unchanged(cinematic::Rect rect, DWORD w, DWORD h, cinematic::Viewport viewport)
{
    const cinematic::Rect before = rect;
    assert(!cinematic::Fit(rect, w, h, viewport));
    assert(std::memcmp(&rect, &before, sizeof(rect)) == 0);
}

int main()
{
    static_assert(sizeof(void*) == 4, "Native movie context test requires x86");
    static_assert(sizeof(cinematic::Rect) == 16);
    const cinematic::Viewport wide = {0, 0, 2560, 1080};
    cinematic::Rect rect = {0, 0, 1, 1};
    assert(cinematic::Fit(rect, 1280, 720, wide));
    Near(rect.x, 0.125f); Near(rect.y, 0); Near(rect.width, 0.75f); Near(rect.height, 1);
    Unchanged(rect, 1280, 720, wide);
    Unchanged({0, 0, 1, 1}, 1280, 720, {320, 0, 1920, 1080});
    Unchanged({0.1f, 0.2f, 0.6f, 0.8f}, 1280, 720, wide);
    rect = {-0.5f, -0.5f, 1, 1};
    assert(cinematic::Fit(rect, 640, 480, wide));
    Near(rect.x, -0.28125f); Near(rect.width, 0.5625f); Near(rect.y, -0.5f);
    rect = {0.1f, 0.2f, 0.8f, 0.6f};
    assert(cinematic::Fit(rect, 1280, 720, {0, 0, 1080, 1920}));
    Near(rect.x, 0.1f); Near(rect.width, 0.8f); Near(rect.height, 0.253125f);
    Near(rect.y, 0.3734375f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Unchanged({nan, 0, 1, 1}, 1280, 720, wide);
    Unchanged({0, 0, inf, 1}, 1280, 720, wide);
    Unchanged({0, 0, -1, 1}, 1280, 720, wide);
    Unchanged({0, 0, 1, 0}, 1280, 720, wide);
    Unchanged({0, 0, 1, 1}, 0, 720, wide);
    Unchanged({0, 0, 1, 1}, 1280, 0, wide);
    Unchanged({0, 0, 1, 1}, 1280, 720, {0, 0, 0, 1080});
    // Vary source aspect, viewport, and pre-existing rectangles. The result
    // must preserve the center, stay inside the input, and be stable on repeat.
    for (DWORD width : {640U, 1280U, 1920U, 3440U}) {
        for (DWORD height : {480U, 720U, 1080U, 1440U}) {
            for (DWORD source_width : {640U, 1280U, 1920U}) {
                for (DWORD source_height : {480U, 720U, 1080U}) {
                    for (float size : {0.25f, 0.6f, 1.0f}) {
                        const cinematic::Rect before = {0.1f, -0.2f, size, 0.75f};
                        rect = before;
                        const cinematic::Viewport viewport = {100, 50, width, height};
                        cinematic::Fit(rect, source_width, source_height, viewport);
                        Near(rect.x + rect.width / 2, before.x + before.width / 2);
                        Near(rect.y + rect.height / 2, before.y + before.height / 2);
                        assert(rect.width <= before.width && rect.height <= before.height);
                        assert(std::fabs(rect.width * width -
                            rect.height * height * static_cast<double>(source_width) / source_height) < 0.01);
                        Unchanged(rect, source_width, source_height, viewport);
                    }
                }
            }
        }
    }

    alignas(void*) std::uint8_t stack[0x80] = {}, player[0x94] = {}, source[0x4C0] = {};
    DWORD bink[2] = {1280, 720};
    void* table[9] = {};
    void* context[3] = {table, nullptr, nullptr};
    float pass[6] = {0.75f, 1, 0.125f, 0, 2560, 1080};
    context[2] = pass;
    table[3] = Address(0x00299CA0); table[8] = Address(0x00299950);
    *reinterpret_cast<void**>(stack) = Address(0x0036CBAC);
    *reinterpret_cast<void**>(stack + 4) = context;
    *reinterpret_cast<void**>(stack + 0x68) = Address(0x0042A212);
    *reinterpret_cast<void**>(player + 0x90) = source;
    *reinterpret_cast<void**>(source + 0x4BC) = bink;
    regions = {{stack, sizeof(stack)}, {player, sizeof(player)}, {source, sizeof(source)},
        {bink, sizeof(bink)}, {table, sizeof(table)}, {context, sizeof(context)}, {pass, sizeof(pass)}};
    const auto esp = reinterpret_cast<std::uintptr_t>(stack);
    auto movie = ReadMovieDrawContext(esp, player);
    assert(movie.cinematic_movie && movie.screen_rectangle && movie.viewport_known);
    assert(movie.movie_width == 1280 && movie.movie_height == 720);
    assert(movie.viewport.x == 320 && movie.viewport.width == 1920 && movie.viewport.height == 1080);
    rect = {0, 0, 1, 1};
    assert(!FitMovieDrawRectangle(movie, rect)); // Already 16:9 pass, memory-backed movie.
    pass[0] = 1; pass[2] = 0;
    movie = ReadMovieDrawContext(esp, player);
    assert(FitMovieDrawRectangle(movie, rect)); Near(rect.width, 0.75f);
    assert(!FitMovieDrawRectangle(movie, rect));

    // A menu draw stays untouched even while a DIFFERENT cinematic is tracked.
    *reinterpret_cast<void**>(stack + 0x68) = Address(0x002DDBCE);
    tracked_cinematic = player;
    movie = ReadMovieDrawContext(esp, player);
    assert(!movie.cinematic_movie);
    rect = {0, 0, 1, 1}; assert(!FitMovieDrawRectangle(movie, rect));
    tracked_cinematic = bink;
    movie = ReadMovieDrawContext(esp, player);
    assert(movie.cinematic_movie && FitMovieDrawRectangle(movie, rect));
    *reinterpret_cast<void**>(stack) = Address(0x0036CBF3);
    movie = ReadMovieDrawContext(esp, player);
    rect = {-0.5f, -0.5f, 1, 1}; assert(!FitMovieDrawRectangle(movie, rect)); // Arbitrary transform.
    *reinterpret_cast<void**>(stack) = Address(0x0036CBAC);
    table[3] = nullptr;
    assert(!ReadMovieDrawContext(esp, player).viewport_known);
    table[3] = Address(0x00299CA0);
    for (float invalid : {0.0f, -1.0f, nan, inf}) {
        pass[0] = invalid;
        assert(!ReadMovieDrawContext(esp, player).viewport_known);
    }
    pass[0] = 1; pass[2] = 0.5f; // Outside the target.
    assert(!ReadMovieDrawContext(esp, player).viewport_known);
    pass[0] = 0.7501f; pass[2] = 0;
    assert(ReadMovieDrawContext(esp, player).viewport.width == 1920); // Native truncation.
    regions[2].size = 0x4BC;
    assert(!ReadMovieDrawContext(esp, player).cinematic_movie);
    assert(!ReadMovieDrawContext(esp, nullptr).cinematic_movie);
    regions[0].size = 0x10;
    assert(!ReadMovieDrawContext(esp, player).screen_rectangle);
    std::puts("PASS: 432 fitting combinations, aspect/centering/idempotence, native viewport and per-movie scope");
}
