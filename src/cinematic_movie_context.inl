// Native PC 1.04 layout used by MovieDraw at RVA 0x0036C5D0.
// Included here and by the x86 regression test so the pointer/stack reads
// exercised by the test are the ones used in the game.
struct MovieDrawContext
{
    void* handle = nullptr;
    DWORD movie_width = 0;
    DWORD movie_height = 0;
    DWORD caller_rva = 0;
    DWORD wrapper_caller_rva = 0;
    cinematic::Viewport viewport = {};
    bool viewport_known = false;
    bool cinematic_movie = false;
    bool screen_rectangle = false;
};

MovieDrawContext ReadMovieDrawContext(std::uintptr_t entry_esp, void* movie_player)
{
    MovieDrawContext result;
    if (!IsReadableMemory(reinterpret_cast<void*>(entry_esp), 0x18)) return result;
    result.caller_rva = CallerRva(*reinterpret_cast<void**>(entry_esp));
    // The rectangular wrapper creates the native screen transform itself.
    // The centered wrapper accepts an arbitrary caller transform, potentially
    // a world-space movie; do not assume its coordinates span the viewport.
    result.screen_rectangle = result.caller_rva == 0x0036CBAC;
    const size_t return_offset = result.screen_rectangle ? 0x68 :
        (result.caller_rva == 0x0036CBF3 ? 0x24 : 0);
    if (return_offset != 0 && IsReadableMemory(
            reinterpret_cast<void*>(entry_esp + return_offset), sizeof(void*))) {
        result.wrapper_caller_rva = CallerRva(*reinterpret_cast<void**>(entry_esp + return_offset));
    }

    auto* player = static_cast<std::uint8_t*>(movie_player);
    if (player == nullptr || !IsReadableMemory(player + 0x90, sizeof(void*))) return result;
    auto* source = *reinterpret_cast<std::uint8_t**>(player + 0x90);
    if (source == nullptr || !IsReadableMemory(source + 0x4BC, sizeof(void*))) return result;
    result.handle = *reinterpret_cast<void**>(source + 0x4BC);
    if (!IsReadableMemory(result.handle, 2 * sizeof(DWORD))) return result;
    result.movie_width = *static_cast<DWORD*>(result.handle);
    result.movie_height = *(static_cast<DWORD*>(result.handle) + 1);

    // This caller is the native cinematic player's draw, including movies
    // opened from memory. Filename classification remains useful for other
    // cinematic users of the same screen wrapper. Never use an active count.
    result.cinematic_movie = result.wrapper_caller_rva == 0x0042A212 ||
        IsTrackedCinematicHandle(result.handle);

    auto* context = *reinterpret_cast<std::uint8_t**>(entry_esp + 4);
    if (!IsReadableMemory(context, 3 * sizeof(void*))) return result;
    auto* table = *reinterpret_cast<std::uint8_t**>(context);
    // Validate the native layout by its getter and quad-submission methods.
    // Both base and derived contexts use these implementations in PC 1.04.
    if (!IsReadableMemory(table, 9 * sizeof(void*)) ||
        CallerRva(*reinterpret_cast<void**>(table + 3 * sizeof(void*))) != 0x00299CA0 ||
        CallerRva(*reinterpret_cast<void**>(table + 8 * sizeof(void*))) != 0x00299950) return result;
    auto* pass = *reinterpret_cast<const float**>(context + 8);
    if (IsReadableMemory(pass, 6 * sizeof(float))) {
        result.viewport_known = cinematic::DecodeViewport(pass, result.viewport);
    }
    return result;
}

bool FitMovieDrawRectangle(const MovieDrawContext& context, cinematic::Rect& rect)
{
    return context.screen_rectangle && context.cinematic_movie && context.viewport_known &&
        cinematic::Fit(rect, context.movie_width, context.movie_height, context.viewport);
}
