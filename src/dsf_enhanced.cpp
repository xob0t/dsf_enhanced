#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "cinematic_fit.h"

namespace {

bool g_log_enabled = true;
bool g_remove_vsync = false;
bool g_borderless_windowed = false;
bool g_ultrawide_enabled = true;
bool g_fov_scaling = true;
bool g_cinematic_draw_patch = true;
int g_anisotropic_filtering = 16;
bool g_force_trilinear_filtering = true;
bool g_force_highest_mip = true;
float g_texture_lod_bias = 0.0f;
float g_lod_distance_scale = 1.0f;
float g_lod_engine_threshold_scale = 0.25f;
float g_map_detail_lod_threshold_scale = 1.0f;
float g_imposter_draw_distance = 2800.0f;
float g_traffic_spawn_distance_scale = 1.0f;
float g_interesting_vehicle_min_distance_scale = 1.0f;
float g_interesting_vehicle_max_distance_scale = 1.0f;
int g_interesting_vehicle_slot_count = 0;
bool g_preload_all_traffic_vehicle_pools = false;
float g_traffic_candidate_window_scale = 1.0f;
float g_traffic_spawn_interval_scale = 1.0f;
bool g_skip_legal_screen = true;
bool g_suppress_ubisoft_server_lost_prompt = true;
float g_target_aspect = 0.0f;
float g_fov_scalar = 0.0f;
float g_current_aspect = 16.0f / 9.0f;
UINT g_backbuffer_width = 0;
UINT g_backbuffer_height = 0;
char g_log_path[MAX_PATH] = {};
bool g_aspect_constant_patched = false;
bool g_global_fov_scalar_patched = false;
LONG g_active_bink_count = 0;
LONG g_active_cinematic_bink_count = 0;
void* volatile g_last_d3d_device = nullptr;

constexpr DWORD kAspect16x9Rva = 0x009E50B0;
constexpr DWORD kGlobalFovScalarRva = 0x009D9A80;
constexpr DWORD kMovieDrawRva = 0x0036C5D0;
constexpr DWORD kLegalScreenWaitBranchRva = 0x00399C4A;
constexpr DWORD kErrorMessageUpdateRva = 0x0032AD80;
constexpr DWORD kLuaStateRva = 0x00E2B498;
constexpr DWORD kLuaLoadStringRva = 0x00207AB0;
constexpr DWORD kLuaPCallRva = 0x001F1CA0; // Native protected call, with its actual status return.
constexpr DWORD kEngineSingletonRva = 0x00BE7228;
constexpr DWORD kLodDirtyFlagRva = 0x00D8D6E0;
constexpr DWORD kEngineSetLodThresholdRva = 0x002A4F40;
constexpr DWORD kEngineGetLodThresholdRva = 0x002A4F70;
constexpr DWORD kImposterApplyDistanceRva = 0x002B3560;
constexpr DWORD kTrafficSettingsTableRva = 0x00BE7AC0;
constexpr DWORD kTrafficApplySettingsVehiclePoolRva = 0x0024E2D0;
constexpr DWORD kInterestingVehicleSetMinDistanceRva = 0x003EB3A0;
constexpr DWORD kInterestingVehicleSetMaxDistanceRva = 0x003EB3C0;
constexpr DWORD kInterestingVehicleSetSlotCountRva = 0x003EB3E0;
constexpr DWORD kTrafficCandidateLateralWindowRva = 0x00B2A278;
constexpr DWORD kTrafficCandidateForwardWindowRva = 0x00B2A27C;
constexpr DWORD kOriginalAspect16x9Bits = 0x3FE38E39;
constexpr DWORD kOriginalFovScalarBits = 0x3C8EFA35;
constexpr float kTrafficCandidateOriginalLateralWindow = 20.0f;
constexpr float kTrafficCandidateOriginalForwardWindow = 6.0f;

using Direct3DCreate9Fn = IDirect3D9* (WINAPI*)(UINT);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using SetSamplerStateFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using DrawIndexedPrimitiveFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using SetPixelShaderConstantFFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, const float*, UINT);
using EngineSetLodThresholdFn = void (__thiscall*)(void*, int, int, float);
using EngineGetLodThresholdFn = float (__thiscall*)(void*, int, int);
using ImposterApplyDistanceFn = void (__thiscall*)(void*, float, float);
using InterestingVehicleSetDistanceFn = void (__thiscall*)(void*, float);
using InterestingVehicleSetSlotCountFn = void (__thiscall*)(void*, int);
using TrafficApplySettingsVehiclePoolFn = bool (__thiscall*)(void*, void*);
using LuaLoadStringFn = int (__cdecl*)(void*, const char*);
using LuaPCallFn = int (__cdecl*)(void*, int, int, int);
using BinkOpenFn = void* (WINAPI*)(const char*, UINT);
using BinkCloseFn = void (WINAPI*)(void*);

Direct3DCreate9Fn g_real_direct3d_create9 = nullptr;
ResetFn g_real_reset = nullptr;
SetSamplerStateFn g_real_set_sampler_state = nullptr;
DrawIndexedPrimitiveFn g_real_draw_indexed_primitive = nullptr;
SetPixelShaderConstantFFn g_real_set_pixel_shader_constant_f = nullptr;
BinkOpenFn g_real_bink_open = nullptr;
BinkCloseFn g_real_bink_close = nullptr;
volatile LONG g_device_hooks_installed = 0;
volatile LONG g_cinematic_movie_draw_count = 0;
volatile LONG g_cinematic_movie_draw_log_count = 0;
volatile LONG g_sampler_quality_log_count = 0;
volatile LONG g_lod_distance_scale_log_count = 0;
void* g_map_lod_last_manager = nullptr;
bool g_map_lod_threshold_patch_applied = false;
volatile LONG g_traffic_radius_lua_attempt_count = 0;
volatile LONG g_traffic_radius_last_attempt_tick = 0;
bool g_traffic_radius_lua_executed = false;
bool g_traffic_radius_patch_applied = false;
volatile LONG g_lod_engine_log_count = 0;
void* g_lod_engine_last_manager = nullptr;
bool g_lod_engine_threshold_patch_applied = false;
volatile LONG g_imposter_log_count = 0;
void* g_imposter_last_manager = nullptr;
bool g_imposter_distance_patch_applied = false;
volatile LONG g_traffic_runtime_log_count = 0;
void* g_traffic_last_pool_manager = nullptr;
bool g_traffic_vehicle_pools_preloaded = false;
void* g_traffic_last_runtime_manager = nullptr;
volatile LONG g_interesting_vehicle_log_count = 0;
void* g_traffic_last_interesting_manager = nullptr;
bool g_interesting_vehicle_patch_applied = false;
void* g_movie_draw_continue = nullptr;
void* g_error_message_update_continue = nullptr;
volatile LONG g_ubisoft_server_lost_prompt_log_count = 0;

struct BinkHandleRecord
{
    void* handle;
    bool cinematic;
};

BinkHandleRecord g_bink_handles[16] = {};
SRWLOCK g_bink_handle_lock = SRWLOCK_INIT;

struct ModuleRange
{
    std::uint8_t* base;
    DWORD size;
};

void MarkEngineLodThresholdsDirty(const ModuleRange& range);

DWORD CallerRva(void* caller);
void HookDeviceMethods(IDirect3DDevice9* device);
void TrackLastD3DDevice(IDirect3DDevice9* device);
void RehookTrackedD3DDevice(const char* source);

void BuildGamePath(char* out, DWORD out_size, const char* name)
{
    char module_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, module_path, MAX_PATH);

    char* slash = std::strrchr(module_path, '\\');
    if (slash != nullptr) {
        slash[1] = '\0';
    } else {
        module_path[0] = '\0';
    }

    std::snprintf(out, out_size, "%s%s", module_path, name);
}

void Log(const char* fmt, ...)
{
    if (!g_log_enabled || g_log_path[0] == '\0') {
        return;
    }

    FILE* file = nullptr;
    fopen_s(&file, g_log_path, "a");
    if (file == nullptr) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(
        file,
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds
    );

    va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fprintf(file, "\n");
    std::fclose(file);
}

float ReadProfileFloat(const char* section, const char* key, float fallback, const char* ini_path)
{
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.6f", fallback);
    GetPrivateProfileStringA(section, key, buffer, buffer, sizeof(buffer), ini_path);
    return static_cast<float>(std::atof(buffer));
}

int ClampInt(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

void LoadConfig()
{
    char ini_path[MAX_PATH] = {};
    BuildGamePath(ini_path, MAX_PATH, "dsf_enhanced.ini");
    BuildGamePath(g_log_path, MAX_PATH, "dsf_enhanced.log");

    g_log_enabled = GetPrivateProfileIntA("General", "Log", 1, ini_path) != 0;
    g_remove_vsync = GetPrivateProfileIntA("Display", "RemoveVSync", 0, ini_path) != 0;
    g_borderless_windowed = GetPrivateProfileIntA("Display", "BorderlessWindowed", 0, ini_path) != 0;
    g_ultrawide_enabled = GetPrivateProfileIntA("Ultrawide", "Enabled", 1, ini_path) != 0;
    g_target_aspect = ReadProfileFloat("Ultrawide", "TargetAspect", 0.0f, ini_path);
    g_fov_scalar = ReadProfileFloat("Ultrawide", "FOVScalar", 0.0f, ini_path);
    g_fov_scaling = GetPrivateProfileIntA("Ultrawide", "FOVScaling", 1, ini_path) != 0;
    g_cinematic_draw_patch = GetPrivateProfileIntA("Ultrawide", "CinematicDrawPatch", 1, ini_path) != 0;
    g_anisotropic_filtering = ClampInt(GetPrivateProfileIntA("Quality", "AnisotropicFiltering", 16, ini_path), 0, 16);
    g_force_trilinear_filtering = GetPrivateProfileIntA("Quality", "ForceTrilinearFiltering", 0, ini_path) != 0;
    g_force_highest_mip = GetPrivateProfileIntA("Quality", "ForceHighestMip", 0, ini_path) != 0;
    g_texture_lod_bias = ReadProfileFloat("Quality", "TextureLODBias", 0.0f, ini_path);
    g_texture_lod_bias = std::max(-3.0f, std::min(g_texture_lod_bias, 3.0f));
    g_lod_distance_scale = ReadProfileFloat("Quality", "LODDistanceScale", 1.0f, ini_path);
    g_lod_distance_scale = std::max(0.25f, std::min(g_lod_distance_scale, 8.0f));
    g_lod_engine_threshold_scale = ReadProfileFloat("Quality", "LODEngineThresholdScale", 1.0f, ini_path);
    g_lod_engine_threshold_scale = std::max(0.25f, std::min(g_lod_engine_threshold_scale, 8.0f));
    g_map_detail_lod_threshold_scale = ReadProfileFloat("Quality", "MapDetailLODThresholdScale", 1.0f, ini_path);
    g_map_detail_lod_threshold_scale = std::max(0.25f, std::min(g_map_detail_lod_threshold_scale, 8.0f));
    g_traffic_spawn_distance_scale = ReadProfileFloat("Quality", "TrafficSpawnDistanceScale", 1.0f, ini_path);
    g_traffic_spawn_distance_scale = std::max(1.0f, std::min(g_traffic_spawn_distance_scale, 8.0f));
    g_imposter_draw_distance = ReadProfileFloat("Quality", "ImposterDrawDistance", 2800.0f, ini_path);
    g_imposter_draw_distance = std::max(-1.0f, std::min(g_imposter_draw_distance, 7000.0f));
    g_interesting_vehicle_min_distance_scale =
        ReadProfileFloat("Quality", "InterestingVehicleMinDistanceScale", 1.0f, ini_path);
    g_interesting_vehicle_min_distance_scale =
        std::max(0.25f, std::min(g_interesting_vehicle_min_distance_scale, 16.0f));
    g_interesting_vehicle_max_distance_scale =
        ReadProfileFloat("Quality", "InterestingVehicleMaxDistanceScale", 1.0f, ini_path);
    g_interesting_vehicle_max_distance_scale =
        std::max(0.25f, std::min(g_interesting_vehicle_max_distance_scale, 16.0f));
    g_interesting_vehicle_slot_count =
        ClampInt(GetPrivateProfileIntA("Quality", "InterestingVehicleSlotCount", 0, ini_path), 0, 10);
    g_preload_all_traffic_vehicle_pools = GetPrivateProfileIntA("Quality", "PreloadAllTrafficVehiclePools", 0, ini_path) != 0;
    g_traffic_candidate_window_scale = ReadProfileFloat("Quality", "TrafficCandidateWindowScale", 1.0f, ini_path);
    g_traffic_candidate_window_scale = std::max(1.0f, std::min(g_traffic_candidate_window_scale, 16.0f));
    g_traffic_spawn_interval_scale = ReadProfileFloat("Quality", "TrafficSpawnIntervalScale", 1.0f, ini_path);
    g_traffic_spawn_interval_scale = std::max(0.05f, std::min(g_traffic_spawn_interval_scale, 1.0f));
    g_skip_legal_screen = GetPrivateProfileIntA("Startup", "SkipLegalScreen", 1, ini_path) != 0;
    g_suppress_ubisoft_server_lost_prompt = GetPrivateProfileIntA("Startup", "SuppressUbisoftServerLostPrompt", 1, ini_path) != 0;
}

bool WriteMemory(void* address, const void* data, size_t size)
{
    DWORD old_protect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, old_protect, &ignored);
    return true;
}

DWORD FloatBits(float value)
{
    DWORD bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool IsCommittedWritableMemory(const void* address, size_t size)
{
    if (address == nullptr || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }

    const DWORD protect = mbi.Protect & 0xFF;
    if (protect == PAGE_NOACCESS || protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ || protect == PAGE_READONLY) {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    return begin >= region_begin && (begin + size) <= (region_begin + mbi.RegionSize);
}

bool IsReadableMemory(const void* address, size_t size)
{
    if (address == nullptr || size == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
        return false;
    }

    if ((mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }

    const DWORD protect = mbi.Protect & 0xFF;
    if (protect == PAGE_NOACCESS || protect == PAGE_EXECUTE) {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto region_begin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    return begin >= region_begin && (begin + size) <= (region_begin + mbi.RegionSize);
}

bool IsRvaInRange(const ModuleRange& range, DWORD rva, size_t size)
{
    return range.base != nullptr && size != 0 && rva <= range.size && size <= range.size - rva;
}

ModuleRange GetExeRange()
{
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
    if (base == nullptr) {
        return ModuleRange{};
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return ModuleRange{};
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return ModuleRange{};
    }

    return ModuleRange{base, nt->OptionalHeader.SizeOfImage};
}

bool ResolveEngineInstance(const ModuleRange& range, std::uint8_t** out_engine)
{
    if (out_engine == nullptr) {
        return false;
    }
    *out_engine = nullptr;

    if (!IsRvaInRange(range, kEngineSingletonRva, sizeof(void*))) {
        return false;
    }

    auto** engine_slot = reinterpret_cast<std::uint8_t**>(range.base + kEngineSingletonRva);
    if (!IsReadableMemory(engine_slot, sizeof(*engine_slot))) {
        return false;
    }

    std::uint8_t* engine = *engine_slot;
    if (engine == nullptr || !IsReadableMemory(engine, sizeof(void*))) {
        return false;
    }

    *out_engine = engine;
    return true;
}

bool TrySetInterestingVehicleDistance(InterestingVehicleSetDistanceFn setter, void* manager, float value)
{
    if (setter == nullptr || manager == nullptr) {
        return false;
    }

    __try {
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TrySetInterestingVehicleSlotCount(InterestingVehicleSetSlotCountFn setter, void* manager, int value)
{
    if (setter == nullptr || manager == nullptr) {
        return false;
    }

    __try {
        setter(manager, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ApplyInterestingVehiclePatch(const ModuleRange& range, const char* source)
{
    if (std::fabs(g_interesting_vehicle_min_distance_scale - 1.0f) < 0.001f &&
        std::fabs(g_interesting_vehicle_max_distance_scale - 1.0f) < 0.001f &&
        g_interesting_vehicle_slot_count <= 0) {
        return;
    }

    std::uint8_t* engine = nullptr;
    if (!ResolveEngineInstance(range, &engine) || engine == nullptr || !IsReadableMemory(engine + 0x6d8, sizeof(void*))) {
        return;
    }

    auto* manager = *reinterpret_cast<std::uint8_t**>(engine + 0x6d8);
    if (manager == nullptr || !IsReadableMemory(manager + 0x29b0, 1)) {
        return;
    }

    if (manager != g_traffic_last_interesting_manager) {
        g_traffic_last_interesting_manager = manager;
        g_interesting_vehicle_patch_applied = false;
        g_interesting_vehicle_log_count = 0;
        Log("InterestingVehicles: manager=0x%p from %s", manager, source != nullptr ? source : "Loop");
    }

    const float original_min = IsReadableMemory(manager + 0x29a0, sizeof(float)) ?
        *reinterpret_cast<float*>(manager + 0x29a0) : 0.0f;
    const float original_max = IsReadableMemory(manager + 0x29a4, sizeof(float)) ?
        *reinterpret_cast<float*>(manager + 0x29a4) : 0.0f;
    const int original_slots = IsReadableMemory(manager + 0x29a8, sizeof(int)) ?
        *reinterpret_cast<int*>(manager + 0x29a8) : 0;
    const int enabled = manager[0x29b0] ? 1 : 0;

    float target_min = original_min;
    float target_max = original_max;
    const float base_min = 128.0f;
    const float base_max = 256.0f;
    if (std::fabs(g_interesting_vehicle_min_distance_scale - 1.0f) >= 0.001f) {
        target_min = std::min(base_min * g_interesting_vehicle_min_distance_scale, 500000.0f);
    }
    if (std::fabs(g_interesting_vehicle_max_distance_scale - 1.0f) >= 0.001f) {
        target_max = std::min(base_max * g_interesting_vehicle_max_distance_scale, 500000.0f);
    }

    bool min_ok = true;
    bool max_ok = true;
    bool slots_ok = true;
    if (std::fabs(target_min - original_min) >= 0.001f) {
        auto setter = reinterpret_cast<InterestingVehicleSetDistanceFn>(range.base + kInterestingVehicleSetMinDistanceRva);
        min_ok = TrySetInterestingVehicleDistance(setter, manager, target_min);
    }
    if (std::fabs(target_max - original_max) >= 0.001f) {
        auto setter = reinterpret_cast<InterestingVehicleSetDistanceFn>(range.base + kInterestingVehicleSetMaxDistanceRva);
        max_ok = TrySetInterestingVehicleDistance(setter, manager, target_max);
    }
    if (g_interesting_vehicle_slot_count > 0 && original_slots != g_interesting_vehicle_slot_count) {
        auto setter = reinterpret_cast<InterestingVehicleSetSlotCountFn>(range.base + kInterestingVehicleSetSlotCountRva);
        slots_ok = TrySetInterestingVehicleSlotCount(setter, manager, g_interesting_vehicle_slot_count);
    }

    const float after_min = IsReadableMemory(manager + 0x29a0, sizeof(float)) ?
        *reinterpret_cast<float*>(manager + 0x29a0) : 0.0f;
    const float after_max = IsReadableMemory(manager + 0x29a4, sizeof(float)) ?
        *reinterpret_cast<float*>(manager + 0x29a4) : 0.0f;
    const int after_slots = IsReadableMemory(manager + 0x29a8, sizeof(int)) ?
        *reinterpret_cast<int*>(manager + 0x29a8) : 0;

    const bool changed =
        std::fabs(after_min - original_min) >= 0.001f ||
        std::fabs(after_max - original_max) >= 0.001f ||
        after_slots != original_slots ||
        !g_interesting_vehicle_patch_applied;
    if (changed) {
        const LONG count = InterlockedIncrement(&g_interesting_vehicle_log_count);
        if (count <= 12) {
            Log(
                "InterestingVehicles: minScale=%.3f maxScale=%.3f min%s %.3f -> %.3f max%s %.3f -> %.3f slots%s %d -> %d enabled=%d",
                static_cast<double>(g_interesting_vehicle_min_distance_scale),
                static_cast<double>(g_interesting_vehicle_max_distance_scale),
                min_ok ? "" : "_failed",
                static_cast<double>(original_min),
                static_cast<double>(after_min),
                max_ok ? "" : "_failed",
                static_cast<double>(original_max),
                static_cast<double>(after_max),
                slots_ok ? "" : "_failed",
                original_slots,
                after_slots,
                enabled
            );
        }
    }

    g_interesting_vehicle_patch_applied = min_ok && max_ok && slots_ok;
}

bool TryApplyTrafficSettingsVehiclePool(TrafficApplySettingsVehiclePoolFn apply_pool, void* manager, void* settings_record)
{
    if (apply_pool == nullptr || manager == nullptr || settings_record == nullptr) {
        return false;
    }

    __try {
        return apply_pool(manager, settings_record);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ApplyTrafficVehiclePoolPreload(const ModuleRange& range, const char* source)
{
    if (!g_preload_all_traffic_vehicle_pools) {
        return;
    }

    if (!IsRvaInRange(range, kTrafficSettingsTableRva, sizeof(void*)) ||
        !IsRvaInRange(range, kTrafficApplySettingsVehiclePoolRva, 1)) {
        return;
    }

    std::uint8_t* engine = nullptr;
    if (!ResolveEngineInstance(range, &engine) || engine == nullptr || !IsReadableMemory(engine + 0x758, sizeof(void*))) {
        return;
    }

    auto* manager = *reinterpret_cast<std::uint8_t**>(engine + 0x758);
    if (manager == nullptr || !IsReadableMemory(manager, sizeof(DWORD))) {
        return;
    }

    if (manager != g_traffic_last_pool_manager) {
        g_traffic_last_pool_manager = manager;
        g_traffic_vehicle_pools_preloaded = false;
        Log("TrafficPools: manager=0x%p from %s", manager, source != nullptr ? source : "Loop");
    }

    if (g_traffic_vehicle_pools_preloaded) {
        return;
    }

    auto** table_slot = reinterpret_cast<std::uint8_t**>(range.base + kTrafficSettingsTableRva);
    if (!IsReadableMemory(table_slot, sizeof(*table_slot))) {
        return;
    }

    auto* table = *table_slot;
    if (table == nullptr || !IsReadableMemory(table, 8)) {
        return;
    }

    const int raw_count = *reinterpret_cast<int*>(table);
    auto* records = *reinterpret_cast<std::uint8_t**>(table + 4);
    const int count = ClampInt(raw_count, 0, 256);
    if (records == nullptr || count == 0 || !IsReadableMemory(records, sizeof(DWORD))) {
        return;
    }

    auto apply_pool = reinterpret_cast<TrafficApplySettingsVehiclePoolFn>(range.base + kTrafficApplySettingsVehiclePoolRva);
    int applied = 0;
    int failed = 0;
    for (int index = 0; index < count; ++index) {
        auto* record = records + index * 0x34;
        if (!IsReadableMemory(record, 0x34)) {
            ++failed;
            continue;
        }

        if (TryApplyTrafficSettingsVehiclePool(apply_pool, manager, record)) {
            ++applied;
        } else {
            ++failed;
        }
    }

    Log(
        "TrafficPools: table=0x%p records=0x%p count=%d applied=%d failed=%d",
        table,
        records,
        count,
        applied,
        failed
    );
    g_traffic_vehicle_pools_preloaded = true;
}

void ApplyTrafficRuntimePatches(const ModuleRange& range, const char* source)
{
    const bool patch_candidate_window = g_traffic_candidate_window_scale > 1.001f;
    const bool patch_spawn_interval = g_traffic_spawn_interval_scale < 0.999f;
    if (!patch_candidate_window && !patch_spawn_interval) {
        return;
    }

    if (patch_candidate_window &&
        IsRvaInRange(range, kTrafficCandidateLateralWindowRva, sizeof(float)) &&
        IsRvaInRange(range, kTrafficCandidateForwardWindowRva, sizeof(float))) {
        auto* lateral_window = reinterpret_cast<float*>(range.base + kTrafficCandidateLateralWindowRva);
        auto* forward_window = reinterpret_cast<float*>(range.base + kTrafficCandidateForwardWindowRva);
        if (IsReadableMemory(lateral_window, sizeof(float)) &&
            IsReadableMemory(forward_window, sizeof(float))) {
            const float original_lateral = *lateral_window;
            const float original_forward = *forward_window;
            const float target_lateral = kTrafficCandidateOriginalLateralWindow * g_traffic_candidate_window_scale;
            const float target_forward = kTrafficCandidateOriginalForwardWindow * g_traffic_candidate_window_scale;
            if (std::fabs(original_lateral - target_lateral) > 0.001f) {
                WriteMemory(lateral_window, &target_lateral, sizeof(target_lateral));
            }
            if (std::fabs(original_forward - target_forward) > 0.001f) {
                WriteMemory(forward_window, &target_forward, sizeof(target_forward));
            }

            const LONG log_count = InterlockedIncrement(&g_traffic_runtime_log_count);
            if (log_count <= 8) {
                Log(
                    "TrafficRuntime: candidateWindow source=%s scale=%.3f lateral %.3f -> %.3f forward %.3f -> %.3f",
                    source != nullptr ? source : "Loop",
                    static_cast<double>(g_traffic_candidate_window_scale),
                    static_cast<double>(original_lateral),
                    static_cast<double>(*lateral_window),
                    static_cast<double>(original_forward),
                    static_cast<double>(*forward_window)
                );
            }
        }
    }

    std::uint8_t* engine = nullptr;
    if (!ResolveEngineInstance(range, &engine) || engine == nullptr || !IsReadableMemory(engine + 0x758, sizeof(void*))) {
        return;
    }

    auto* manager = *reinterpret_cast<std::uint8_t**>(engine + 0x758);
    if (manager == nullptr || !IsReadableMemory(manager + 0x5ca854, sizeof(DWORD))) {
        return;
    }

    if (manager != g_traffic_last_runtime_manager) {
        g_traffic_last_runtime_manager = manager;
        g_traffic_runtime_log_count = 0;
        Log("TrafficRuntime: manager=0x%p from %s", manager, source != nullptr ? source : "Loop");
    }

    float next_spawn_time = 0.0f;
    float last_update_time = 0.0f;
    std::uint8_t traffic_enabled = 0;
    std::uint8_t spawn_pending = 0;
    std::uint8_t spawn_scan_active = 0;
    std::uint8_t force_refresh = 0;
    DWORD active_road_count = 0;
    DWORD active_road_phase = 0;
    DWORD active_lane_phase = 0;
    DWORD pending_vehicle_count = 0;

    const bool can_read_runtime =
        IsReadableMemory(manager + 0x501818, sizeof(DWORD)) &&
        IsReadableMemory(manager + 0x50181c, sizeof(DWORD)) &&
        IsReadableMemory(manager + 0x501820, sizeof(DWORD)) &&
        IsReadableMemory(manager + 0x503640, sizeof(float)) &&
        IsReadableMemory(manager + 0x503644, sizeof(std::uint8_t)) &&
        IsReadableMemory(manager + 0x503645, sizeof(std::uint8_t)) &&
        IsReadableMemory(manager + 0x503646, sizeof(std::uint8_t)) &&
        IsReadableMemory(manager + 0x5ca84c, sizeof(std::uint8_t)) &&
        IsReadableMemory(manager + 0x5ca848, sizeof(float)) &&
        IsReadableMemory(manager + 0x5ca858, sizeof(DWORD));
    if (!can_read_runtime) {
        return;
    }

    active_road_count = *reinterpret_cast<DWORD*>(manager + 0x501818);
    active_road_phase = *reinterpret_cast<DWORD*>(manager + 0x50181c);
    active_lane_phase = *reinterpret_cast<DWORD*>(manager + 0x501820);
    next_spawn_time = *reinterpret_cast<float*>(manager + 0x503640);
    spawn_pending = manager[0x503644];
    spawn_scan_active = manager[0x503645];
    force_refresh = manager[0x503646];
    traffic_enabled = manager[0x5ca84c];
    last_update_time = *reinterpret_cast<float*>(manager + 0x5ca848);
    pending_vehicle_count = *reinterpret_cast<DWORD*>(manager + 0x5ca858);

    bool interval_changed = false;
    float target_next_spawn_time = next_spawn_time;
    if (patch_spawn_interval &&
        traffic_enabled != 0 &&
        std::isfinite(next_spawn_time) &&
        std::isfinite(last_update_time) &&
        next_spawn_time > last_update_time &&
        next_spawn_time > 0.0f &&
        last_update_time > 0.0f) {
        const float original_delta = next_spawn_time - last_update_time;
        target_next_spawn_time = last_update_time + original_delta * g_traffic_spawn_interval_scale;
        if (target_next_spawn_time < next_spawn_time) {
            WriteMemory(manager + 0x503640, &target_next_spawn_time, sizeof(target_next_spawn_time));
            interval_changed = true;
        }
    }

    if (interval_changed) {
        const LONG log_count = InterlockedIncrement(&g_traffic_runtime_log_count);
        if (log_count <= 40) {
            char interval_text[64] = {};
            if (interval_changed) {
                std::snprintf(
                    interval_text,
                    sizeof(interval_text),
                    " -> %.3f",
                    static_cast<double>(target_next_spawn_time)
                );
            }

            Log(
                "TrafficRuntime: enabled=%u activeRoads=%lu roadPhase=%lu lanePhase=%lu pending=%lu spawnFlags=%u/%u/%u next=%.3f last=%.3f intervalScale=%.3f%s",
                static_cast<unsigned>(traffic_enabled),
                static_cast<unsigned long>(active_road_count),
                static_cast<unsigned long>(active_road_phase),
                static_cast<unsigned long>(active_lane_phase),
                static_cast<unsigned long>(pending_vehicle_count),
                static_cast<unsigned>(spawn_pending),
                static_cast<unsigned>(spawn_scan_active),
                static_cast<unsigned>(force_refresh),
                static_cast<double>(next_spawn_time),
                static_cast<double>(last_update_time),
                static_cast<double>(g_traffic_spawn_interval_scale),
                interval_text
            );
        }
    }
}

bool ResolveEngineLodManager(const ModuleRange& range, void** out_manager)
{
    if (out_manager == nullptr) {
        return false;
    }
    *out_manager = nullptr;

    if (!IsRvaInRange(range, kEngineSingletonRva, sizeof(void*))) {
        return false;
    }

    auto** engine_slot = reinterpret_cast<std::uint8_t**>(range.base + kEngineSingletonRva);
    if (!IsReadableMemory(engine_slot, sizeof(*engine_slot))) {
        return false;
    }

    std::uint8_t* engine = *engine_slot;
    if (engine == nullptr) {
        return false;
    }
    if (!IsReadableMemory(engine + 0x334, sizeof(void*))) {
        return false;
    }

    auto* city_state = *reinterpret_cast<std::uint8_t**>(engine + 0x334);
    if (city_state == nullptr) {
        return false;
    }
    if (!IsReadableMemory(city_state + 0x10, sizeof(void*))) {
        return false;
    }

    auto* lod_manager = *reinterpret_cast<std::uint8_t**>(city_state + 0x10);
    if (lod_manager == nullptr) {
        return false;
    }
    if (!IsReadableMemory(lod_manager, sizeof(void*))) {
        return false;
    }

    *out_manager = lod_manager;
    return true;
}

bool TryGetEngineLodThreshold(EngineGetLodThresholdFn getter, void* manager, int category, int mask, float* out_value)
{
    if (getter == nullptr || manager == nullptr || out_value == nullptr) {
        return false;
    }

    __try {
        *out_value = getter(manager, category, mask);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TrySetEngineLodThreshold(EngineSetLodThresholdFn setter, void* manager, int category, int mask, float value)
{
    if (setter == nullptr || manager == nullptr) {
        return false;
    }

    __try {
        setter(manager, category, mask, value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void MarkEngineLodThresholdsDirty(const ModuleRange& range)
{
    if (!IsRvaInRange(range, kLodDirtyFlagRva, sizeof(std::uint8_t))) {
        return;
    }

    auto* dirty_flag = range.base + kLodDirtyFlagRva;
    const std::uint8_t dirty = 1;
    WriteMemory(dirty_flag, &dirty, sizeof(dirty));
}

void ApplyMapDetailLodThresholds(const ModuleRange& range, const char* source)
{
    if (std::fabs(g_map_detail_lod_threshold_scale - 1.0f) < 0.001f ||
        !IsRvaInRange(range, kEngineGetLodThresholdRva, 1) ||
        !IsRvaInRange(range, kEngineSetLodThresholdRva, 1)) {
        return;
    }

    void* manager = nullptr;
    if (!ResolveEngineLodManager(range, &manager)) {
        return;
    }

    if (manager != g_map_lod_last_manager) {
        g_map_lod_last_manager = manager;
        g_map_lod_threshold_patch_applied = false;
    }
    if (g_map_lod_threshold_patch_applied) {
        return;
    }

    auto getter = reinterpret_cast<EngineGetLodThresholdFn>(range.base + kEngineGetLodThresholdRva);
    auto setter = reinterpret_cast<EngineSetLodThresholdFn>(range.base + kEngineSetLodThresholdRva);
    constexpr int kStaticCategory = 0;
    constexpr int kMasks[] = {4, 8, 16, 1, 2};
    float original[5] = {};
    float patched[5] = {};
    bool have_value = false;

    for (int slot = 0; slot < 5; ++slot) {
        if (!TryGetEngineLodThreshold(getter, manager, kStaticCategory, kMasks[slot], &original[slot])) {
            return;
        }
        float target = original[slot];
        if (std::isfinite(target) && target > 0.0f) {
            have_value = true;
            target = std::min(target * g_map_detail_lod_threshold_scale, 500000.0f);
        }
        if (!TrySetEngineLodThreshold(setter, manager, kStaticCategory, kMasks[slot], target)) {
            return;
        }
        patched[slot] = target;
    }

    if (!have_value) {
        return;
    }

    MarkEngineLodThresholdsDirty(range);
    Log(
        "MapLOD: source=%s category=Static/0 scale=%.3f slots=[%.3f, %.3f, %.3f, %.3f, %.3f] -> [%.3f, %.3f, %.3f, %.3f, %.3f]",
        source != nullptr ? source : "Loop",
        static_cast<double>(g_map_detail_lod_threshold_scale),
        static_cast<double>(original[0]), static_cast<double>(original[1]),
        static_cast<double>(original[2]), static_cast<double>(original[3]), static_cast<double>(original[4]),
        static_cast<double>(patched[0]), static_cast<double>(patched[1]),
        static_cast<double>(patched[2]), static_cast<double>(patched[3]), static_cast<double>(patched[4])
    );
    g_map_lod_threshold_patch_applied = true;
}

void ApplyEngineLodThresholds(const ModuleRange& range, const char* source)
{
    if (std::fabs(g_lod_engine_threshold_scale - 1.0f) < 0.001f) {
        return;
    }

    if (!IsRvaInRange(range, kEngineGetLodThresholdRva, 1) || !IsRvaInRange(range, kEngineSetLodThresholdRva, 1)) {
        return;
    }

    void* manager = nullptr;
    if (!ResolveEngineLodManager(range, &manager)) {
        const LONG count = InterlockedIncrement(&g_lod_engine_log_count);
        if (count <= 3) {
            Log("LOD.Engine: manager not ready from %s", source != nullptr ? source : "Loop");
        }
        return;
    }

    if (manager != g_lod_engine_last_manager) {
        g_lod_engine_last_manager = manager;
        g_lod_engine_threshold_patch_applied = false;
        Log("LOD.Engine: manager=0x%p from %s", manager, source != nullptr ? source : "Loop");
    }

    if (g_lod_engine_threshold_patch_applied) {
        return;
    }

    auto getter = reinterpret_cast<EngineGetLodThresholdFn>(range.base + kEngineGetLodThresholdRva);
    auto setter = reinterpret_cast<EngineSetLodThresholdFn>(range.base + kEngineSetLodThresholdRva);
    // Preserves the verified original mapping: Vehicle, Ped, Driver, and the
    // fourth native LOD bank. Do not substitute the static-world category here.
    constexpr int kCategories[] = {1, 2, 3, 4};
    constexpr int kMasks[] = {4, 8, 16, 1, 2};
    float original[4][5] = {};
    bool have_values = false;

    for (int category_index = 0; category_index < 4; ++category_index) {
        for (int slot_index = 0; slot_index < 5; ++slot_index) {
            float value = 0.0f;
            if (!TryGetEngineLodThreshold(getter, manager, kCategories[category_index], kMasks[slot_index], &value)) {
                Log(
                    "LOD.Engine: getter failed category=%d slot=%d mask=%d",
                    kCategories[category_index],
                    slot_index + 1,
                    kMasks[slot_index]
                );
                return;
            }
            original[category_index][slot_index] = value;
            if (std::isfinite(value) && value > 0.0f) {
                have_values = true;
            }
        }
    }

    if (!have_values) {
        Log("LOD.Engine: threshold matrix is empty, retrying later");
        return;
    }

    for (int category_index = 0; category_index < 4; ++category_index) {
        Log(
            "LOD.Engine: original category=%d slots=[%.3f, %.3f, %.3f, %.3f, %.3f]",
            kCategories[category_index],
            static_cast<double>(original[category_index][0]),
            static_cast<double>(original[category_index][1]),
            static_cast<double>(original[category_index][2]),
            static_cast<double>(original[category_index][3]),
            static_cast<double>(original[category_index][4])
        );
    }

    float patched[4][5] = {};
    for (int category_index = 0; category_index < 4; ++category_index) {
        for (int slot_index = 0; slot_index < 5; ++slot_index) {
            const float current = original[category_index][slot_index];
            float target = current;
            if (std::isfinite(current) && current > 0.0f) {
                target = std::min(current * g_lod_engine_threshold_scale, 500000.0f);
            }

            if (!TrySetEngineLodThreshold(setter, manager, kCategories[category_index], kMasks[slot_index], target)) {
                Log(
                    "LOD.Engine: setter failed category=%d slot=%d mask=%d target=%.3f",
                    kCategories[category_index],
                    slot_index + 1,
                    kMasks[slot_index],
                    static_cast<double>(target)
                );
                return;
            }

            float after = 0.0f;
            if (TryGetEngineLodThreshold(getter, manager, kCategories[category_index], kMasks[slot_index], &after)) {
                patched[category_index][slot_index] = after;
            } else {
                patched[category_index][slot_index] = target;
            }
        }
    }

    MarkEngineLodThresholdsDirty(range);
    for (int category_index = 0; category_index < 4; ++category_index) {
        Log(
            "LOD.Engine: scaled category=%d scale=%.3f slots=[%.3f, %.3f, %.3f, %.3f, %.3f]",
            kCategories[category_index],
            static_cast<double>(g_lod_engine_threshold_scale),
            static_cast<double>(patched[category_index][0]),
            static_cast<double>(patched[category_index][1]),
            static_cast<double>(patched[category_index][2]),
            static_cast<double>(patched[category_index][3]),
            static_cast<double>(patched[category_index][4])
        );
    }
    g_lod_engine_threshold_patch_applied = true;
}

bool ResolveImposterManager(const ModuleRange& range, void** out_manager)
{
    if (out_manager == nullptr) {
        return false;
    }
    *out_manager = nullptr;

    if (!IsRvaInRange(range, kEngineSingletonRva, sizeof(void*))) {
        return false;
    }

    auto** engine_slot = reinterpret_cast<std::uint8_t**>(range.base + kEngineSingletonRva);
    if (!IsReadableMemory(engine_slot, sizeof(*engine_slot))) {
        return false;
    }

    std::uint8_t* engine = *engine_slot;
    if (engine == nullptr || !IsReadableMemory(engine + 0x18, sizeof(void*))) {
        return false;
    }

    auto* imposter_manager = *reinterpret_cast<std::uint8_t**>(engine + 0x18);
    if (imposter_manager == nullptr || !IsReadableMemory(imposter_manager, sizeof(void*))) {
        return false;
    }

    *out_manager = imposter_manager;
    return true;
}

bool TryApplyImposterDistance(ImposterApplyDistanceFn apply_distance, void* bank, float base_distance, float draw_distance)
{
    if (apply_distance == nullptr || bank == nullptr) {
        return false;
    }

    __try {
        apply_distance(bank, base_distance, draw_distance);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ApplyImposterDrawDistancePatch(const ModuleRange& range, const char* source)
{
    if (g_imposter_draw_distance < 0.0f) {
        return;
    }

    if (!IsRvaInRange(range, kImposterApplyDistanceRva, 1)) {
        return;
    }

    void* manager = nullptr;
    if (!ResolveImposterManager(range, &manager)) {
        const LONG count = InterlockedIncrement(&g_imposter_log_count);
        if (count <= 3) {
            Log("Imposter: manager not ready from %s", source != nullptr ? source : "Loop");
        }
        return;
    }

    if (manager != g_imposter_last_manager) {
        g_imposter_last_manager = manager;
        g_imposter_distance_patch_applied = false;
        Log("Imposter: manager=0x%p from %s", manager, source != nullptr ? source : "Loop");
    }

    if (g_imposter_distance_patch_applied) {
        return;
    }

    auto apply_distance = reinterpret_cast<ImposterApplyDistanceFn>(range.base + kImposterApplyDistanceRva);
    auto* base = reinterpret_cast<std::uint8_t*>(manager);
    constexpr int kBankCount = 2;
    constexpr int kBankStride = 0x43F0;
    constexpr int kActiveOffset = 0x43B9;
    constexpr int kCountOffset = 0x43C4;
    bool saw_active_bank = false;

    for (int bank_index = 0; bank_index < kBankCount; ++bank_index) {
        auto* bank = base + bank_index * kBankStride;
        if (!IsReadableMemory(bank + kActiveOffset, 1) || !IsReadableMemory(bank + kCountOffset, sizeof(int))) {
            continue;
        }

        const bool active = bank[kActiveOffset] != 0;
        const int raw_count = *reinterpret_cast<int*>(bank + kCountOffset);
        Log(
            "Imposter: bank=%d active=%d raw_count=%d",
            bank_index,
            active ? 1 : 0,
            raw_count
        );

        if (!active) {
            continue;
        }

        saw_active_bank = true;
        if (!TryApplyImposterDistance(apply_distance, bank, -1.0f, g_imposter_draw_distance)) {
            Log("Imposter: native setter failed bank=%d draw=%.3f", bank_index, static_cast<double>(g_imposter_draw_distance));
        } else {
            Log("Imposter: native setter bank=%d base=-1.000 draw=%.3f", bank_index, static_cast<double>(g_imposter_draw_distance));
        }
    }

    if (!saw_active_bank) {
        Log("Imposter: no active banks yet from %s, retrying", source != nullptr ? source : "Loop");
        return;
    }

    g_imposter_distance_patch_applied = true;
}

bool PatchImportByName(const ModuleRange& range, const char* dll_name, const char* import_name, void* replacement, void** original)
{
    if (range.base == nullptr) {
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(range.base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(range.base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports_dir.VirtualAddress == 0 || imports_dir.Size == 0) {
        return false;
    }

    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(range.base + imports_dir.VirtualAddress);
    for (; imports->Name != 0; ++imports) {
        const char* current_dll = reinterpret_cast<const char*>(range.base + imports->Name);
        if (_stricmp(current_dll, dll_name) != 0) {
            continue;
        }

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(range.base + imports->FirstThunk);
        auto* original_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(range.base + imports->OriginalFirstThunk);
        for (; original_thunk->u1.AddressOfData != 0; ++thunk, ++original_thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal)) {
                continue;
            }

            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(range.base + original_thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), import_name) != 0) {
                continue;
            }

            if (original != nullptr) {
                *original = reinterpret_cast<void*>(thunk->u1.Function);
            }

            void* patch_address = &thunk->u1.Function;
            return WriteMemory(patch_address, &replacement, sizeof(replacement));
        }
    }

    return false;
}

void UpdateBackbuffer(UINT width, UINT height, const char* source)
{
    if (width == 0 || height == 0) {
        return;
    }

    g_backbuffer_width = width;
    g_backbuffer_height = height;
    g_current_aspect = g_target_aspect > 0.0f ? g_target_aspect : static_cast<float>(width) / static_cast<float>(height);

    Log(
        "%s: backbuffer=%ux%u aspect=%.6f effective_aspect=%.6f",
        source,
        width,
        height,
        static_cast<double>(static_cast<float>(width) / static_cast<float>(height)),
        static_cast<double>(g_current_aspect)
    );
}

void ApplyAspectConstantPatch(const ModuleRange& range, const char* source)
{
    if (!g_ultrawide_enabled || g_current_aspect <= 0.0f || range.base == nullptr) {
        return;
    }

    if (!IsRvaInRange(range, kAspect16x9Rva, sizeof(float))) {
        Log("%s: aspect constant RVA 0x%08lX is outside the loaded image", source, static_cast<unsigned long>(kAspect16x9Rva));
        return;
    }

    auto* aspect_constant = reinterpret_cast<float*>(range.base + kAspect16x9Rva);
    const DWORD current_bits = *reinterpret_cast<DWORD*>(aspect_constant);
    if (current_bits != kOriginalAspect16x9Bits && !g_aspect_constant_patched) {
        Log(
            "%s: aspect constant mismatch at RVA 0x%08lX current=0x%08lX expected=0x%08lX",
            source,
            static_cast<unsigned long>(kAspect16x9Rva),
            static_cast<unsigned long>(current_bits),
            static_cast<unsigned long>(kOriginalAspect16x9Bits)
        );
        return;
    }

    if (WriteMemory(aspect_constant, &g_current_aspect, sizeof(g_current_aspect))) {
        if (!g_aspect_constant_patched || current_bits != FloatBits(g_current_aspect)) {
            Log(
                "%s: patched 16:9 aspect constant RVA 0x%08lX to %.6f",
                source,
                static_cast<unsigned long>(kAspect16x9Rva),
                static_cast<double>(g_current_aspect)
            );
        }
        g_aspect_constant_patched = true;
    } else {
        Log("%s: failed to patch aspect constant", source);
    }
}

float GetTargetFovScalar()
{
    if (!g_fov_scaling) {
        return 0.0f;
    }

    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    if (g_fov_scalar > 0.0f) {
        return g_fov_scalar;
    }

    const float aspect = g_current_aspect > 0.0f ? g_current_aspect : 16.0f / 9.0f;
    return kDegreesToRadians * (aspect / (16.0f / 9.0f));
}

void ApplyGlobalFovScalarPatch(const ModuleRange& range, const char* source)
{
    if (!g_ultrawide_enabled || !g_fov_scaling || range.base == nullptr) {
        return;
    }

    if (!IsRvaInRange(range, kGlobalFovScalarRva, sizeof(float))) {
        Log("%s: global FOV scalar RVA 0x%08lX is outside the loaded image", source, static_cast<unsigned long>(kGlobalFovScalarRva));
        return;
    }

    auto* fov_scalar = reinterpret_cast<float*>(range.base + kGlobalFovScalarRva);
    const float target = GetTargetFovScalar();
    if (target <= 0.0f) {
        return;
    }

    const DWORD current_bits = *reinterpret_cast<DWORD*>(fov_scalar);
    const DWORD target_bits = FloatBits(target);
    if (current_bits == target_bits) {
        return;
    }

    if (current_bits != kOriginalFovScalarBits && !g_global_fov_scalar_patched) {
        Log(
            "%s: global FOV scalar mismatch at RVA 0x%08lX current=0x%08lX expected=0x%08lX target=%.6f",
            source,
            static_cast<unsigned long>(kGlobalFovScalarRva),
            static_cast<unsigned long>(current_bits),
            static_cast<unsigned long>(kOriginalFovScalarBits),
            static_cast<double>(target)
        );
        return;
    }

    if (WriteMemory(fov_scalar, &target, sizeof(target))) {
        Log(
            "%s: patched global FOV scalar RVA 0x%08lX %.6f -> %.6f",
            source,
            static_cast<unsigned long>(kGlobalFovScalarRva),
            static_cast<double>(*reinterpret_cast<const float*>(&current_bits)),
            static_cast<double>(target)
        );
        g_global_fov_scalar_patched = true;
    } else {
        Log("%s: failed to patch global FOV scalar", source);
    }
}

bool ContainsInsensitive(const char* haystack, const char* needle)
{
    if (haystack == nullptr || needle == nullptr || needle[0] == '\0') {
        return false;
    }

    const size_t needle_len = std::strlen(needle);
    for (const char* cursor = haystack; *cursor != '\0'; ++cursor) {
        if (_strnicmp(cursor, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

bool IsCinematicBinkPath(const char* path)
{
    return ContainsInsensitive(path, "fmv/cinematics/") ||
        ContainsInsensitive(path, "fmv\\cinematics\\");
}

const char* BinkSourceForLog(const char* file_name, char* buffer, size_t buffer_size)
{
    if (file_name == nullptr) {
        return "(null)";
    }

    if (IsReadableMemory(file_name, 4)) {
        const auto* data = reinterpret_cast<const std::uint8_t*>(file_name);
        if (data[0] == 'B' && data[1] == 'I' && data[2] == 'K' && data[3] == 'i') {
            std::snprintf(buffer, buffer_size, "(memory:unknown-bik)");
            return buffer;
        }
    }

    return file_name;
}

DWORD RvaFromAddress(void* address, const ModuleRange& range)
{
    if (address == nullptr || range.base == nullptr) {
        return 0;
    }

    auto* pointer = reinterpret_cast<std::uint8_t*>(address);
    if (pointer < range.base || pointer >= range.base + range.size) {
        return 0;
    }

    return static_cast<DWORD>(pointer - range.base);
}

void TrackBinkHandle(void* handle, bool cinematic)
{
    if (handle == nullptr) return;
    AcquireSRWLockExclusive(&g_bink_handle_lock);
    for (auto& record : g_bink_handles) {
        if (record.handle == nullptr || record.handle == handle) {
            record = {handle, cinematic};
            ReleaseSRWLockExclusive(&g_bink_handle_lock);
            return;
        }
    }
    ReleaseSRWLockExclusive(&g_bink_handle_lock);
}

bool IsTrackedCinematicHandle(void* handle)
{
    bool cinematic = false;
    AcquireSRWLockShared(&g_bink_handle_lock);
    for (const auto& record : g_bink_handles) {
        if (record.handle == handle) {
            cinematic = record.cinematic;
            break;
        }
    }
    ReleaseSRWLockShared(&g_bink_handle_lock);
    return cinematic;
}

#include "cinematic_movie_context.inl"

bool LooksLikeDistanceValue(float value)
{
    const float magnitude = std::fabs(value);
    return std::isfinite(value) && magnitude >= 25.0f && magnitude <= 100000.0f;
}

bool ScalePixelShaderDistanceConstants(
    UINT start_register,
    const float* constant_data,
    UINT vector4f_count,
    float* patched_constants,
    size_t patched_capacity
)
{
    if (g_lod_distance_scale == 1.0f || constant_data == nullptr || patched_constants == nullptr || vector4f_count == 0) {
        return false;
    }

    const size_t total_floats = static_cast<size_t>(vector4f_count) * 4;
    if (total_floats > patched_capacity) {
        return false;
    }

    bool patched_any = false;
    std::memcpy(patched_constants, constant_data, total_floats * sizeof(float));
    for (UINT i = 0; i < vector4f_count; ++i) {
        const UINT reg = start_register + i;
        if (reg < 10 || reg > 15) {
            continue;
        }

        float* v = patched_constants + (static_cast<size_t>(i) * 4);
        const bool scalar_distance =
            LooksLikeDistanceValue(v[0]) &&
            std::fabs(v[1]) < 0.0001f &&
            std::fabs(v[2]) < 0.0001f &&
            std::fabs(v[3]) < 0.0001f;
        if (!scalar_distance) {
            continue;
        }

        const float old_value = v[0];
        v[0] *= g_lod_distance_scale;
        patched_any = true;

        const LONG index = InterlockedIncrement(&g_lod_distance_scale_log_count);
        if (index <= 96) {
            Log(
                "LOD.Scale[%ld]: PS r%u %.3f -> %.3f scale=%.3f",
                static_cast<long>(index),
                reg,
                static_cast<double>(old_value),
                static_cast<double>(v[0]),
                static_cast<double>(g_lod_distance_scale)
            );
        }
    }

    return patched_any;
}

void __stdcall PatchMovieDrawArgs(std::uintptr_t entry_esp, void* movie_player)
{
    if (!g_ultrawide_enabled || !g_cinematic_draw_patch) return;
    const MovieDrawContext movie = ReadMovieDrawContext(entry_esp, movie_player);
    if (!movie.cinematic_movie || !movie.screen_rectangle || !movie.viewport_known) return;

    auto* args = reinterpret_cast<cinematic::Rect*>(entry_esp + 0x08);
    if (!IsCommittedWritableMemory(args, sizeof(*args))) return;
    const cinematic::Rect before = *args;
    cinematic::Rect fitted = before;
    const LONG index = InterlockedIncrement(&g_cinematic_movie_draw_count);
    if (!FitMovieDrawRectangle(movie, fitted)) return;
    *args = fitted;

    if (InterlockedCompareExchange(&g_cinematic_movie_draw_log_count, 1, 0) == 0) {
        Log(
            "CinematicMovieDraw[%ld]: handle=0x%p source=%lux%lu destination=%u,%u,%u,%u rect=%.6f,%.6f,%.6f,%.6f fitted=%.6f,%.6f,%.6f,%.6f",
            static_cast<long>(index), movie.handle,
            static_cast<unsigned long>(movie.movie_width), static_cast<unsigned long>(movie.movie_height),
            movie.viewport.x, movie.viewport.y, movie.viewport.width, movie.viewport.height,
            static_cast<double>(before.x), static_cast<double>(before.y),
            static_cast<double>(before.width), static_cast<double>(before.height),
            static_cast<double>(fitted.x), static_cast<double>(fitted.y),
            static_cast<double>(fitted.width), static_cast<double>(fitted.height));
    }
}

void TryApplyTrafficSpawnRadiusLuaPatch()
{
    if (g_traffic_spawn_distance_scale <= 1.001f || g_traffic_radius_patch_applied) {
        return;
    }

    const LONG now = static_cast<LONG>(GetTickCount());
    const LONG previous = g_traffic_radius_last_attempt_tick;
    if (previous != 0 && static_cast<DWORD>(now - previous) < 1000) {
        return;
    }
    if (InterlockedCompareExchange(&g_traffic_radius_last_attempt_tick, now, previous) != previous) {
        return;
    }

    const LONG attempt = InterlockedIncrement(&g_traffic_radius_lua_attempt_count);

    const ModuleRange range = GetExeRange();
    if (!IsRvaInRange(range, kLuaStateRva, sizeof(void*)) ||
        !IsRvaInRange(range, kLuaLoadStringRva, 1) ||
        !IsRvaInRange(range, kLuaPCallRva, 1)) {
        return;
    }

    auto** state_slot = reinterpret_cast<void**>(range.base + kLuaStateRva);
    if (!IsReadableMemory(state_slot, sizeof(*state_slot)) || *state_slot == nullptr) {
        return;
    }

    char script[3072] = {};
    _snprintf_s(
        script,
        sizeof(script),
        _TRUNCATE,
        "local ok,result=pcall(function() "
        "if __dsfTrafficRadiusPatched then return -1 end "
        "if not atlas or not atlas.road then return 0 end "
        "local seen={} local changed=0 "
        "for _,road in next,atlas.road,nil do "
        "pcall(function() local p=road.trafficParameters "
        "if p and not seen[p] then seen[p]=true "
        "local pin=p.pingInRadius local pout=p.pingOutRadius "
        "if type(pin)=='number' and pin>0 then p.pingInRadius=pin*%.6f changed=changed+1 end "
        "if type(pout)=='number' and pout>0 then p.pingOutRadius=pout*%.6f end "
        "end end) end "
        "if changed>0 then __dsfTrafficRadiusPatched=changed end return changed "
        "end) if ok then return result or 0 else return -2 end",
        static_cast<double>(g_traffic_spawn_distance_scale),
        static_cast<double>(g_traffic_spawn_distance_scale)
    );

    auto load_string = reinterpret_cast<LuaLoadStringFn>(range.base + kLuaLoadStringRva);
    auto pcall = reinterpret_cast<LuaPCallFn>(range.base + kLuaPCallRva);
    int load_status = -1;
    int call_status = -1;
    __try {
        void* state = *state_slot;
        load_status = load_string(state, script);
        if (load_status == 0) {
            call_status = pcall(state, 0, 1, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("TrafficRadius: Lua dispatch raised a native exception");
        return;
    }

    float changed_count = -999.0f;
    if (load_status == 0 && call_status == 0) {
        auto* state = static_cast<std::uint8_t*>(*state_slot);
        if (IsReadableMemory(state + 0x08, sizeof(void*))) {
            auto** top_slot = reinterpret_cast<std::uint8_t**>(state + 0x08);
            auto* top = *top_slot;
            if (top != nullptr && IsReadableMemory(top - 8, 8)) {
                const int type = *reinterpret_cast<int*>(top - 4);
                if (type == 3) {
                    changed_count = *reinterpret_cast<float*>(top - 8);
                }
                *top_slot = top - 8;
            }
        }
    }

    if (changed_count > 0.0f || changed_count == -1.0f) {
        g_traffic_radius_patch_applied = true;
    }

    if (!g_traffic_radius_lua_executed || load_status != 0 || call_status != 0 || changed_count != 0.0f) {
        Log(
            "TrafficRadius: Lua dispatch attempt=%ld scale=%.3f load=%d call=%d changed=%.0f applied=%d",
            static_cast<long>(attempt),
            static_cast<double>(g_traffic_spawn_distance_scale),
            load_status,
            call_status,
            static_cast<double>(changed_count),
            g_traffic_radius_patch_applied ? 1 : 0
        );
        g_traffic_radius_lua_executed = load_status == 0 && call_status == 0;
    }
}

bool AutoAcknowledgeUbisoftServerLostPrompt(void* message_page)
{
    TryApplyTrafficSpawnRadiusLuaPatch();

    if (!g_suppress_ubisoft_server_lost_prompt || message_page == nullptr) {
        return false;
    }

    constexpr wchar_t message_id[] = L"ID:249127";
    auto* base = static_cast<std::uint8_t*>(message_page);
    auto* message = reinterpret_cast<const wchar_t*>(base + 0x218);
    if (!IsReadableMemory(message, sizeof(message_id)) || std::wcscmp(message, message_id) != 0) {
        return false;
    }

    using ResponseCallbackFn = void (__cdecl*)(void*, void*, int, void*);
    if (!IsReadableMemory(base + 0x04, 0x10)) {
        return false;
    }

    auto callback = *reinterpret_cast<ResponseCallbackFn*>(base + 0x10);
    if (callback == nullptr) {
        Log("Startup: matched obsolete Ubisoft server disconnect prompt but response callback was null");
        return false;
    }

    if (InterlockedIncrement(&g_ubisoft_server_lost_prompt_log_count) <= 4) {
        Log("Startup: auto-acknowledging obsolete Ubisoft server disconnect prompt ID:249127");
    }
    void* context = *reinterpret_cast<void**>(base + 0x04);
    callback(context, base + 0x08, 1, message_page);
    return true;
}

__declspec(naked) void ProxyErrorMessageUpdate()
{
    __asm {
        push ecx
        push ecx
        call AutoAcknowledgeUbisoftServerLostPrompt
        add esp, 4
        pop ecx
        test al, al
        jz pass_through
        mov eax, 3
        ret 4

    pass_through:
        push ecx
        push ebx
        push esi
        mov esi, ecx
        jmp dword ptr [g_error_message_update_continue]
    }
}

__declspec(naked) void ProxyMovieDraw()
{
    __asm {
        mov eax, esp
        pushad
        push ecx
        push eax
        call PatchMovieDrawArgs
        popad
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        jmp dword ptr [g_movie_draw_continue]
    }
}

void ResetCinematicDiagnostics()
{
    InterlockedExchange(&g_cinematic_movie_draw_count, 0);
    InterlockedExchange(&g_cinematic_movie_draw_log_count, 0);
}

void LogCinematicDiagnostics(const char* source)
{
    Log(
        "%s: MovieDraw calls=%ld",
        source,
        static_cast<long>(g_cinematic_movie_draw_count)
    );
}

bool UntrackBinkHandle(void* handle)
{
    if (handle == nullptr) return false;
    bool cinematic = false;
    AcquireSRWLockExclusive(&g_bink_handle_lock);
    for (auto& record : g_bink_handles) {
        if (record.handle == handle) {
            cinematic = record.cinematic;
            record = {};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_bink_handle_lock);
    return cinematic;
}

void* WINAPI ProxyBinkOpen(const char* file_name, UINT flags)
{
    void* handle = g_real_bink_open != nullptr ? g_real_bink_open(file_name, flags) : nullptr;
    const bool cinematic = IsCinematicBinkPath(file_name);
    if (handle != nullptr) {
        InterlockedIncrement(&g_active_bink_count);
        if (cinematic) {
            ResetCinematicDiagnostics();
            InterlockedIncrement(&g_active_cinematic_bink_count);
        }
        TrackBinkHandle(handle, cinematic);
    }

    DWORD width = 0;
    DWORD height = 0;
    if (IsReadableMemory(handle, sizeof(DWORD) * 2)) {
        width = *reinterpret_cast<DWORD*>(handle);
        height = *reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(handle) + sizeof(DWORD));
    }

    char source_buffer[128] = {};
    Log(
        "BinkOpen: handle=0x%p cinematic=%d active=%ld cinematic_active=%ld size=%lux%lu flags=0x%08lX file=%s",
        handle,
        cinematic ? 1 : 0,
        static_cast<long>(g_active_bink_count),
        static_cast<long>(g_active_cinematic_bink_count),
        static_cast<unsigned long>(width),
        static_cast<unsigned long>(height),
        static_cast<unsigned long>(flags),
        BinkSourceForLog(file_name, source_buffer, sizeof(source_buffer))
    );
    return handle;
}

void WINAPI ProxyBinkClose(void* handle)
{
    const bool cinematic = UntrackBinkHandle(handle);
    if (handle != nullptr) {
        InterlockedDecrement(&g_active_bink_count);
        if (cinematic) {
            InterlockedDecrement(&g_active_cinematic_bink_count);
        }
    }

    Log(
        "BinkClose: handle=0x%p cinematic=%d active=%ld cinematic_active=%ld",
        handle,
        cinematic ? 1 : 0,
        static_cast<long>(g_active_bink_count),
        static_cast<long>(g_active_cinematic_bink_count)
    );
    if (cinematic) {
        LogCinematicDiagnostics("BinkClose");
        RehookTrackedD3DDevice("BinkClose");
    }

    if (g_real_bink_close != nullptr) {
        g_real_bink_close(handle);
    }
}

HRESULT STDMETHODCALLTYPE ProxyDrawIndexedPrimitive(
    IDirect3DDevice9* device,
    D3DPRIMITIVETYPE primitive_type,
    INT base_vertex_index,
    UINT min_vertex_index,
    UINT num_vertices,
    UINT start_index,
    UINT primitive_count
)
{
    TryApplyTrafficSpawnRadiusLuaPatch();

    return g_real_draw_indexed_primitive(
        device,
        primitive_type,
        base_vertex_index,
        min_vertex_index,
        num_vertices,
        start_index,
        primitive_count
    );
}

HRESULT STDMETHODCALLTYPE ProxySetPixelShaderConstantF(
    IDirect3DDevice9* device,
    UINT start_register,
    const float* constant_data,
    UINT vector4f_count
)
{
    if (g_real_set_pixel_shader_constant_f == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    float patched_constants[256] = {};
    if (ScalePixelShaderDistanceConstants(
            start_register,
            constant_data,
            vector4f_count,
            patched_constants,
            sizeof(patched_constants) / sizeof(patched_constants[0]))) {
        return g_real_set_pixel_shader_constant_f(device, start_register, patched_constants, vector4f_count);
    }

    return g_real_set_pixel_shader_constant_f(device, start_register, constant_data, vector4f_count);
}

HWND GetPresentationWindow(D3DPRESENT_PARAMETERS* presentation_parameters, HWND fallback)
{
    if (presentation_parameters != nullptr && presentation_parameters->hDeviceWindow != nullptr) {
        return presentation_parameters->hDeviceWindow;
    }
    if (fallback != nullptr) {
        return fallback;
    }
    return GetActiveWindow();
}

void ApplyBorderlessWindow(HWND window, D3DPRESENT_PARAMETERS* presentation_parameters)
{
    if (!g_borderless_windowed || window == nullptr || presentation_parameters == nullptr) {
        return;
    }

    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    if (monitor == nullptr || !GetMonitorInfoA(monitor, &monitor_info)) {
        monitor_info.rcMonitor.left = 0;
        monitor_info.rcMonitor.top = 0;
        monitor_info.rcMonitor.right = GetSystemMetrics(SM_CXSCREEN);
        monitor_info.rcMonitor.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    const RECT rect = monitor_info.rcMonitor;
    const UINT width = static_cast<UINT>(rect.right - rect.left);
    const UINT height = static_cast<UINT>(rect.bottom - rect.top);

    presentation_parameters->Windowed = TRUE;
    presentation_parameters->FullScreen_RefreshRateInHz = 0;
    presentation_parameters->BackBufferWidth = width;
    presentation_parameters->BackBufferHeight = height;
    if (presentation_parameters->hDeviceWindow == nullptr) {
        presentation_parameters->hDeviceWindow = window;
    }

    SetMenu(window, nullptr);
    SetWindowLongA(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongA(window, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(
        window,
        HWND_TOP,
        rect.left,
        rect.top,
        static_cast<int>(width),
        static_cast<int>(height),
        SWP_FRAMECHANGED | SWP_SHOWWINDOW
    );
    ShowWindow(window, SW_SHOW);

    Log("BorderlessWindowed: applied %ux%u at (%ld,%ld)", width, height, rect.left, rect.top);
}

void ApplyPresentationTweaks(D3DPRESENT_PARAMETERS* presentation_parameters, HWND window, const char* source)
{
    if (presentation_parameters == nullptr) {
        return;
    }

    if (g_borderless_windowed) {
        ApplyBorderlessWindow(window, presentation_parameters);
    }

    if (g_remove_vsync) {
        const UINT old_interval = presentation_parameters->PresentationInterval;
        presentation_parameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Log("%s: PresentationInterval %u -> %u", source, old_interval, presentation_parameters->PresentationInterval);
    }

    UpdateBackbuffer(presentation_parameters->BackBufferWidth, presentation_parameters->BackBufferHeight, source);
}

void ApplyUltrawideInstrumentation()
{
    if (!g_ultrawide_enabled) {
        Log("Ultrawide: disabled");
        return;
    }

    Log(
        "Ultrawide: instrumentation active FOVScaling=%d target_aspect=%.6f current_aspect=%.6f",
        g_fov_scaling ? 1 : 0,
        static_cast<double>(g_target_aspect),
        static_cast<double>(g_current_aspect)
    );

    const ModuleRange exe = GetExeRange();
    ApplyAspectConstantPatch(exe, "Ultrawide");
    ApplyGlobalFovScalarPatch(exe, "Ultrawide");
}

DWORD CallerRva(void* caller)
{
    return RvaFromAddress(caller, GetExeRange());
}


void TrackLastD3DDevice(IDirect3DDevice9* device)
{
    if (device != nullptr) {
        device->AddRef();
    }

    auto* old_device = static_cast<IDirect3DDevice9*>(
        InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(&g_last_d3d_device),
            device
        )
    );
    if (old_device != nullptr) {
        old_device->Release();
    }
}

void RehookTrackedD3DDevice(const char* source)
{
    auto* device = static_cast<IDirect3DDevice9*>(g_last_d3d_device);
    if (device == nullptr) {
        Log("%s: no tracked D3D device to rehook", source != nullptr ? source : "D3D");
        return;
    }

    device->AddRef();
    HookDeviceMethods(device);
    device->Release();
}

void ApplySamplerQuality(IDirect3DDevice9* device, const char* source)
{
    if (device == nullptr || g_real_set_sampler_state == nullptr) {
        return;
    }

    const DWORD anisotropy = static_cast<DWORD>(g_anisotropic_filtering);
    for (DWORD sampler = 0; sampler < 16; ++sampler) {
        if (g_anisotropic_filtering > 1) {
            g_real_set_sampler_state(device, sampler, D3DSAMP_MAXANISOTROPY, anisotropy);
        }

        if (g_force_trilinear_filtering) {
            if (g_anisotropic_filtering > 1) {
                g_real_set_sampler_state(device, sampler, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
                g_real_set_sampler_state(device, sampler, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
            } else {
                g_real_set_sampler_state(device, sampler, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                g_real_set_sampler_state(device, sampler, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }
            g_real_set_sampler_state(device, sampler, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        }

        if (g_force_highest_mip) {
            g_real_set_sampler_state(device, sampler, D3DSAMP_MAXMIPLEVEL, 0);
        }

        if (g_texture_lod_bias != 0.0f) {
            g_real_set_sampler_state(device, sampler, D3DSAMP_MIPMAPLODBIAS, FloatBits(g_texture_lod_bias));
        }
    }

    const LONG count = InterlockedIncrement(&g_sampler_quality_log_count);
    if (count <= 8) {
        Log(
            "%s: applied sampler quality anisotropy=%lu trilinear=%d highest_mip=%d lod_bias=%.3f to samplers 0-15",
            source,
            static_cast<unsigned long>(anisotropy),
            g_force_trilinear_filtering ? 1 : 0,
            g_force_highest_mip ? 1 : 0,
            static_cast<double>(g_texture_lod_bias)
        );
    }
}

HRESULT STDMETHODCALLTYPE ProxySetSamplerState(
    IDirect3DDevice9* device,
    DWORD sampler,
    D3DSAMPLERSTATETYPE type,
    DWORD value
)
{
    if (g_real_set_sampler_state == nullptr) {
        return D3DERR_INVALIDCALL;
    }

    if (sampler < 16) {
        const DWORD anisotropy = static_cast<DWORD>(g_anisotropic_filtering);
        if (g_anisotropic_filtering > 1) {
            if (type == D3DSAMP_MAXANISOTROPY && value < anisotropy) {
                value = anisotropy;
            } else if (type == D3DSAMP_MINFILTER) {
                if (value == D3DTEXF_LINEAR || value == D3DTEXF_ANISOTROPIC ||
                    (g_force_trilinear_filtering && value == D3DTEXF_POINT)) {
                    value = D3DTEXF_ANISOTROPIC;
                }
            } else if (g_force_trilinear_filtering && type == D3DSAMP_MAGFILTER &&
                (value == D3DTEXF_POINT || value == D3DTEXF_LINEAR || value == D3DTEXF_ANISOTROPIC)) {
                value = D3DTEXF_ANISOTROPIC;
            }
        } else if (g_force_trilinear_filtering &&
            (type == D3DSAMP_MINFILTER || type == D3DSAMP_MAGFILTER) &&
            value == D3DTEXF_POINT) {
            value = D3DTEXF_LINEAR;
        }

        if (g_force_trilinear_filtering && type == D3DSAMP_MIPFILTER &&
            (value == D3DTEXF_NONE || value == D3DTEXF_POINT ||
             value == D3DTEXF_LINEAR || value == D3DTEXF_ANISOTROPIC)) {
            value = D3DTEXF_LINEAR;
        }

        if (g_force_highest_mip && type == D3DSAMP_MAXMIPLEVEL && value != 0) {
            value = 0;
        }

        if (g_texture_lod_bias != 0.0f && type == D3DSAMP_MIPMAPLODBIAS) {
            value = FloatBits(g_texture_lod_bias);
        }
    }

    return g_real_set_sampler_state(device, sampler, type, value);
}

HRESULT STDMETHODCALLTYPE ProxyReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* presentation_parameters)
{
    TrackLastD3DDevice(device);
    ApplyPresentationTweaks(presentation_parameters, GetPresentationWindow(presentation_parameters, nullptr), "Reset");
    ApplyUltrawideInstrumentation();
    const HRESULT result = g_real_reset(device, presentation_parameters);
    if (SUCCEEDED(result)) {
        ApplySamplerQuality(device, "Reset");
    }
    return result;
}

bool HookDeviceVTable(void** vtable, size_t index, void* replacement, void** original, const char* name)
{
    if (vtable == nullptr || replacement == nullptr || original == nullptr) {
        return false;
    }

    if (vtable[index] == replacement) {
        Log("D3D: IDirect3DDevice9::%s already hooked", name);
        return false;
    }

    *original = vtable[index];
    if (!WriteMemory(&vtable[index], &replacement, sizeof(replacement))) {
        *original = nullptr;
        Log("D3D: failed to hook IDirect3DDevice9::%s", name);
        return false;
    }

    Log("D3D: hooked IDirect3DDevice9::%s original=0x%p", name, *original);
    return true;
}

void HookDeviceMethods(IDirect3DDevice9* device)
{
    if (device == nullptr) {
        return;
    }

    auto*** object = reinterpret_cast<void***>(device);
    void** vtable = *object;

    void* reset_replacement = reinterpret_cast<void*>(&ProxyReset);
    void* set_sampler_state_replacement = reinterpret_cast<void*>(&ProxySetSamplerState);
    void* draw_indexed_primitive_replacement = reinterpret_cast<void*>(&ProxyDrawIndexedPrimitive);
    void* set_pixel_shader_constant_f_replacement = reinterpret_cast<void*>(&ProxySetPixelShaderConstantF);

    if (vtable != nullptr && vtable[16] == reset_replacement) {
        Log("D3D: device hooks already installed");
        return;
    }

    void* original_reset = nullptr;
    if (!HookDeviceVTable(vtable, 16, reset_replacement, &original_reset, "Reset")) {
        return;
    }
    g_real_reset = reinterpret_cast<ResetFn>(original_reset);
    InterlockedIncrement(&g_device_hooks_installed);

    void* original_set_sampler_state = nullptr;
    if (HookDeviceVTable(vtable, 69, set_sampler_state_replacement, &original_set_sampler_state, "SetSamplerState")) {
        g_real_set_sampler_state = reinterpret_cast<SetSamplerStateFn>(original_set_sampler_state);
        ApplySamplerQuality(device, "CreateDevice");
    }

    void* original_draw_indexed_primitive = nullptr;
    if (HookDeviceVTable(vtable, 82, draw_indexed_primitive_replacement, &original_draw_indexed_primitive, "DrawIndexedPrimitive")) {
        g_real_draw_indexed_primitive = reinterpret_cast<DrawIndexedPrimitiveFn>(original_draw_indexed_primitive);
    }

    void* original_set_pixel_shader_constant_f = nullptr;
    if (HookDeviceVTable(
            vtable,
            109,
            set_pixel_shader_constant_f_replacement,
            &original_set_pixel_shader_constant_f,
            "SetPixelShaderConstantF")) {
        g_real_set_pixel_shader_constant_f = reinterpret_cast<SetPixelShaderConstantFFn>(original_set_pixel_shader_constant_f);
    }
}

class Direct3D9Proxy final : public IDirect3D9
{
public:
    explicit Direct3D9Proxy(IDirect3D9* real) : m_real(real) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (ppvObj == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDirect3D9) {
            *ppvObj = static_cast<IDirect3D9*>(this);
            AddRef();
            return S_OK;
        }
        return m_real->QueryInterface(riid, ppvObj);
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        m_real->AddRef();
        return InterlockedIncrement(&m_refs);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        m_real->Release();
        const ULONG refs = InterlockedDecrement(&m_refs);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) override { return m_real->RegisterSoftwareDevice(pInitializeFunction); }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return m_real->GetAdapterCount(); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override { return m_real->GetAdapterIdentifier(Adapter, Flags, pIdentifier); }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override { return m_real->GetAdapterModeCount(Adapter, Format); }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override { return m_real->EnumAdapterModes(Adapter, Format, Mode, pMode); }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override { return m_real->GetAdapterDisplayMode(Adapter, pMode); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) override { return m_real->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override { return m_real->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) override { return m_real->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override { return m_real->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override { return m_real->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override { return m_real->GetDeviceCaps(Adapter, DeviceType, pCaps); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override { return m_real->GetAdapterMonitor(Adapter); }

    HRESULT STDMETHODCALLTYPE CreateDevice(
        UINT Adapter,
        D3DDEVTYPE DeviceType,
        HWND hFocusWindow,
        DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS* pPresentationParameters,
        IDirect3DDevice9** ppReturnedDeviceInterface
    ) override
    {
        ApplyPresentationTweaks(pPresentationParameters, GetPresentationWindow(pPresentationParameters, hFocusWindow), "CreateDevice");
        ApplyUltrawideInstrumentation();

        const HRESULT result = m_real->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
        if (SUCCEEDED(result) && ppReturnedDeviceInterface != nullptr) {
            TrackLastD3DDevice(*ppReturnedDeviceInterface);
            HookDeviceMethods(*ppReturnedDeviceInterface);
        } else {
            Log("CreateDevice: failed hr=0x%08lX", static_cast<unsigned long>(result));
        }
        return result;
    }

private:
    IDirect3D9* m_real = nullptr;
    volatile LONG m_refs = 1;
};

IDirect3D9* WINAPI ProxyDirect3DCreate9(UINT sdk_version)
{
    if (g_real_direct3d_create9 == nullptr) {
        return nullptr;
    }

    IDirect3D9* real = g_real_direct3d_create9(sdk_version);
    if (real == nullptr) {
        return nullptr;
    }

    Log("D3D: wrapping IDirect3D9 from Direct3DCreate9(%u)", sdk_version);
    return new Direct3D9Proxy(real);
}

bool PatchDirect3DCreate9Import(const ModuleRange& range)
{
    void* original = nullptr;
    if (!PatchImportByName(range, "d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(&ProxyDirect3DCreate9), &original)) {
        Log("D3D: failed to patch Direct3DCreate9 import");
        return false;
    }

    g_real_direct3d_create9 = reinterpret_cast<Direct3DCreate9Fn>(original);
    Log("D3D: patched Direct3DCreate9 import original=0x%p", original);
    return true;
}

bool ApplyMovieDrawPatch(const ModuleRange& range)
{
    if (!g_ultrawide_enabled || !g_cinematic_draw_patch) {
        return false;
    }

    constexpr size_t patch_size = 6;
    if (!IsRvaInRange(range, kMovieDrawRva, patch_size)) {
        Log("MovieDraw: RVA 0x%08lX is outside the loaded image", static_cast<unsigned long>(kMovieDrawRva));
        return false;
    }

    auto* target = range.base + kMovieDrawRva;
    const std::uint8_t expected[patch_size] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
    if (std::memcmp(target, expected, patch_size) != 0) {
        Log(
            "MovieDraw: prologue mismatch at RVA 0x%08lX bytes=%02X %02X %02X %02X %02X %02X",
            static_cast<unsigned long>(kMovieDrawRva),
            target[0],
            target[1],
            target[2],
            target[3],
            target[4],
            target[5]
        );
        return false;
    }

    g_movie_draw_continue = target + patch_size;
    std::uint8_t patch[patch_size] = {};
    patch[0] = 0xE9;
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::uint8_t*>(&ProxyMovieDraw) - (target + 5)
    );
    std::memcpy(&patch[1], &relative, sizeof(relative));
    patch[5] = 0x90;

    if (!WriteMemory(target, patch, sizeof(patch))) {
        Log("MovieDraw: failed to patch RVA 0x%08lX", static_cast<unsigned long>(kMovieDrawRva));
        return false;
    }

    Log(
        "MovieDraw: patched RVA 0x%08lX target=0x%p hook=0x%p continue=0x%p",
        static_cast<unsigned long>(kMovieDrawRva),
        target,
        &ProxyMovieDraw,
        g_movie_draw_continue
    );
    return true;
}

void ApplyStartupPatches(const ModuleRange& range)
{
    if (!g_skip_legal_screen) {
        return;
    }

    // State 4 computes elapsed time and uses this branch to keep waiting while
    // elapsed < 5.0f. Removing only that branch preserves the legal screen's
    // setup and teardown without rewriting the widely shared 5.0f constant.
    constexpr std::uint8_t expected[] = { 0x0F, 0x82, 0x89, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t patch[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

    if (!IsRvaInRange(range, kLegalScreenWaitBranchRva, sizeof(patch))) {
        Log("Startup: legal screen wait branch RVA 0x%08lX is outside the loaded image", static_cast<unsigned long>(kLegalScreenWaitBranchRva));
        return;
    }

    auto* target = range.base + kLegalScreenWaitBranchRva;
    if (std::memcmp(target, patch, sizeof(patch)) == 0) {
        return;
    }

    if (std::memcmp(target, expected, sizeof(expected)) != 0) {
        Log(
            "Startup: legal screen wait branch mismatch at RVA 0x%08lX bytes=%02X %02X %02X %02X %02X %02X",
            static_cast<unsigned long>(kLegalScreenWaitBranchRva),
            target[0], target[1], target[2], target[3], target[4], target[5]
        );
        return;
    }

    if (WriteMemory(target, patch, sizeof(patch))) {
        Log(
            "Startup: bypassed legal screen wait branch at RVA 0x%08lX",
            static_cast<unsigned long>(kLegalScreenWaitBranchRva)
        );
    } else {
        Log("Startup: failed to patch legal screen wait branch");
    }
}

void ApplyUbisoftServerLostPromptPatch(const ModuleRange& range)
{
    if (!g_suppress_ubisoft_server_lost_prompt && g_traffic_spawn_distance_scale <= 1.001f) {
        return;
    }

    constexpr size_t patch_size = 5;
    constexpr std::uint8_t expected[patch_size] = { 0x51, 0x53, 0x56, 0x8B, 0xF1 };
    if (!IsRvaInRange(range, kErrorMessageUpdateRva, patch_size)) {
        Log("Startup: error-message update RVA 0x%08lX is outside the loaded image", static_cast<unsigned long>(kErrorMessageUpdateRva));
        return;
    }

    auto* target = range.base + kErrorMessageUpdateRva;
    if (target[0] == 0xE9) {
        return;
    }

    if (std::memcmp(target, expected, patch_size) != 0) {
        Log(
            "Startup: error-message update mismatch at RVA 0x%08lX bytes=%02X %02X %02X %02X %02X",
            static_cast<unsigned long>(kErrorMessageUpdateRva),
            target[0], target[1], target[2], target[3], target[4]
        );
        return;
    }

    g_error_message_update_continue = target + patch_size;
    std::uint8_t patch[patch_size] = { 0xE9, 0, 0, 0, 0 };
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::uint8_t*>(&ProxyErrorMessageUpdate) - (target + patch_size)
    );
    std::memcpy(&patch[1], &relative, sizeof(relative));

    if (WriteMemory(target, patch, sizeof(patch))) {
        Log(
            "Startup: installed frontend update hook for server-prompt and traffic-radius patches at RVA 0x%08lX",
            static_cast<unsigned long>(kErrorMessageUpdateRva)
        );
    } else {
        Log("Startup: failed to install frontend update hook");
        g_error_message_update_continue = nullptr;
    }
}

void PatchBinkImports(const ModuleRange& range)
{
    void* original_open = nullptr;
    if (PatchImportByName(range, "binkw32.dll", "_BinkOpen@8", reinterpret_cast<void*>(&ProxyBinkOpen), &original_open)) {
        g_real_bink_open = reinterpret_cast<BinkOpenFn>(original_open);
        Log("Bink: patched BinkOpen original=0x%p", original_open);
    } else {
        Log("Bink: failed to patch BinkOpen");
    }

    void* original_close = nullptr;
    if (PatchImportByName(range, "binkw32.dll", "_BinkClose@4", reinterpret_cast<void*>(&ProxyBinkClose), &original_close)) {
        g_real_bink_close = reinterpret_cast<BinkCloseFn>(original_close);
        Log("Bink: patched BinkClose original=0x%p", original_close);
    } else {
        Log("Bink: failed to patch BinkClose");
    }

}

void ApplyPatches()
{
    const ModuleRange exe = GetExeRange();
    Log("DSF Enhanced attached: exe=0x%p size=0x%lX", exe.base, static_cast<unsigned long>(exe.size));
    Log(
        "Config: Log=%d RemoveVSync=%d BorderlessWindowed=%d SkipLegalScreen=%d SuppressUbisoftServerLostPrompt=%d Ultrawide=%d TargetAspect=%.6f FOVScaling=%d FOVScalar=%.6f CinematicDrawPatch=%d AnisotropicFiltering=%d ForceTrilinearFiltering=%d ForceHighestMip=%d TextureLODBias=%.3f LODDistanceScale=%.3f LODEngineThresholdScale=%.3f ImposterDrawDistance=%.3f InterestingVehicleMinDistanceScale=%.3f InterestingVehicleMaxDistanceScale=%.3f InterestingVehicleSlotCount=%d PreloadAllTrafficVehiclePools=%d TrafficCandidateWindowScale=%.3f TrafficSpawnIntervalScale=%.3f",
        g_log_enabled ? 1 : 0,
        g_remove_vsync ? 1 : 0,
        g_borderless_windowed ? 1 : 0,
        g_skip_legal_screen ? 1 : 0,
        g_suppress_ubisoft_server_lost_prompt ? 1 : 0,
        g_ultrawide_enabled ? 1 : 0,
        static_cast<double>(g_target_aspect),
        g_fov_scaling ? 1 : 0,
        static_cast<double>(g_fov_scalar),
        g_cinematic_draw_patch ? 1 : 0,
        g_anisotropic_filtering,
        g_force_trilinear_filtering ? 1 : 0,
        g_force_highest_mip ? 1 : 0,
        static_cast<double>(g_texture_lod_bias),
        static_cast<double>(g_lod_distance_scale),
        static_cast<double>(g_lod_engine_threshold_scale),
        static_cast<double>(g_imposter_draw_distance),
        static_cast<double>(g_interesting_vehicle_min_distance_scale),
        static_cast<double>(g_interesting_vehicle_max_distance_scale),
        g_interesting_vehicle_slot_count,
        g_preload_all_traffic_vehicle_pools ? 1 : 0,
        static_cast<double>(g_traffic_candidate_window_scale),
        static_cast<double>(g_traffic_spawn_interval_scale)
    );

    PatchDirect3DCreate9Import(exe);
    PatchBinkImports(exe);
    ApplyStartupPatches(exe);
    ApplyUbisoftServerLostPromptPatch(exe);
    ApplyMovieDrawPatch(exe);
    ApplyAspectConstantPatch(exe, "Init");
    ApplyGlobalFovScalarPatch(exe, "Init");
    Log("Config: MapDetailLODThresholdScale=%.3f TrafficSpawnDistanceScale=%.3f",
        static_cast<double>(g_map_detail_lod_threshold_scale),
        static_cast<double>(g_traffic_spawn_distance_scale));
}

DWORD WINAPI InitThread(LPVOID)
{
    LoadConfig();
    ApplyPatches();
    for (;;) {
        const ModuleRange exe = GetExeRange();
        ApplyAspectConstantPatch(exe, "Loop");
        ApplyGlobalFovScalarPatch(exe, "Loop");
        ApplyMapDetailLodThresholds(exe, "Loop");
        ApplyEngineLodThresholds(exe, "Loop");
        ApplyImposterDrawDistancePatch(exe, "Loop");
        ApplyInterestingVehiclePatch(exe, "Loop");
        ApplyTrafficVehiclePoolPreload(exe, "Loop");
        ApplyTrafficRuntimePatches(exe, "Loop");
        Sleep(1000);
    }
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
