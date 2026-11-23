#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

// Shared window crop and screen-bounds calculation for wgc-capture and
// cursor-sampler. Keeping both binaries on this single helper prevents subtle
// parity/fallback divergence between recorded video frames and cursor bounds.

struct WindowRectPx {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;

    long width() const { return right - left; }
    long height() const { return bottom - top; }
    bool isValid() const { return right > left && bottom > top; }
};

struct WindowBoundsInput {
    bool disableCrop = false;
    bool isIconic = false;
    bool isMaximized = false;
    bool isResizable = false;

    bool hasDwmRect = false;
    WindowRectPx dwmRect{};

    bool hasWinRect = false;
    WindowRectPx winRect{};

    // Unrounded WGC GraphicsCaptureItem::Size() dimensions (0 when unavailable, e.g. cursor-sampler).
    int rawItemWidth = 0;
    int rawItemHeight = 0;

    int borderX = 0;
    int borderY = 0;
};

struct WindowCropResult {
    int rawWidth = 0;
    int rawHeight = 0;
    int width = 0;
    int height = 0;
    int cropLeft = 0;
    int cropTop = 0;
    bool hasCrop = false;

    bool hasSamplerBounds = false;
    long originX = 0;
    long originY = 0;
    int samplerWidth = 0;
    int samplerHeight = 0;
};

// H.264 encoding (and the RGB32->NV12 conversion feeding it) requires even
// frame dimensions. Rounding the WGC pool size up to even avoids CopyResource
// size mismatches and encoder MFT negotiation failures on odd-sized windows,
// while window crop trims down to the even visible extent inside that pool.
inline int roundUpToEvenSize(int value) {
    const int clamped = std::max(2, value);
    return (clamped % 2 == 0) ? clamped : clamped + 1;
}

namespace detail {

struct VisibleRegion {
    int width = 0;
    int height = 0;
    long originX = 0;
    long originY = 0;
    bool valid = false;
};

inline int alignEvenWithFallback(int vis, int raw) {
    const int clamped = std::max(0, (raw > 0) ? std::min(vis, raw) : vis);
    int aligned = (clamped / 2) * 2;
    if (aligned < 2 && raw >= 2) {
        aligned = (raw / 2) * 2;
    }
    if (aligned < 2 && raw > 0) {
        aligned = raw;
    }
    return aligned;
}

inline VisibleRegion computeVisibleRegion(
    const WindowBoundsInput& input,
    int outerW,
    int outerH,
    int rawW,
    int rawH,
    long baseOriginX,
    long baseOriginY,
    bool hasOuter) {
    VisibleRegion region{};

    if (!input.isIconic && input.hasDwmRect && input.dwmRect.isValid()) {
        const int dwmW = static_cast<int>(input.dwmRect.width());
        const int dwmH = static_cast<int>(input.dwmRect.height());
        region.width = alignEvenWithFallback(dwmW, rawW);
        region.height = alignEvenWithFallback(dwmH, rawH);
        region.originX = input.dwmRect.left;
        region.originY = input.dwmRect.top;
        region.valid = region.width >= 2 && region.height >= 2;
        return region;
    }

    if (!hasOuter || outerW <= 0 || outerH <= 0) {
        return region;
    }

    int visW = outerW;
    int visH = outerH;
    long originX = baseOriginX;
    bool shiftedX = false;

    if (!input.isIconic && !input.isMaximized && input.isResizable &&
        input.borderX > 0 && input.borderY > 0) {
        if (visW > 2 * input.borderX) {
            visW -= 2 * input.borderX;
            shiftedX = true;
        }
        if (visH > input.borderY) {
            visH -= input.borderY;
        }
    }

    // Shift originX only if the subtracted width remains >= 2px after clamping
    // and even-rounding (otherwise width falls back to rawW at the unshifted origin).
    const int clampedW = std::max(0, (rawW > 0) ? std::min(visW, rawW) : visW);
    if (shiftedX && (clampedW / 2) * 2 >= 2) {
        originX += static_cast<long>(input.borderX);
    }

    region.width = alignEvenWithFallback(visW, rawW);
    region.height = alignEvenWithFallback(visH, rawH);
    region.originX = originX;
    region.originY = baseOriginY;
    region.valid = region.width >= 2 && region.height >= 2;
    return region;
}

} // namespace detail

inline WindowCropResult computeWindowCropBounds(const WindowBoundsInput& input) {
    WindowCropResult result{};

    const bool hasRawItem = input.rawItemWidth > 0 && input.rawItemHeight > 0;
    const int rawW = hasRawItem ? roundUpToEvenSize(input.rawItemWidth) : 0;
    const int rawH = hasRawItem ? roundUpToEvenSize(input.rawItemHeight) : 0;
    result.rawWidth = rawW;
    result.rawHeight = rawH;

    const bool hasValidWinRect = input.hasWinRect && input.winRect.isValid();
    const int winW = hasValidWinRect ? static_cast<int>(input.winRect.width()) : 0;
    const int winH = hasValidWinRect ? static_cast<int>(input.winRect.height()) : 0;

    if (input.disableCrop) {
        result.width = rawW;
        result.height = rawH;
        result.cropLeft = 0;
        result.cropTop = 0;
        result.hasCrop = false;

        if (hasValidWinRect) {
            result.hasSamplerBounds = true;
            result.originX = input.winRect.left;
            result.originY = input.winRect.top;
            result.samplerWidth = winW;
            result.samplerHeight = winH;
        }
        return result;
    }

    if (hasRawItem) {
        const detail::VisibleRegion captureRegion = detail::computeVisibleRegion(
            input,
            input.rawItemWidth,
            input.rawItemHeight,
            rawW,
            rawH,
            0,
            0,
            true);
        // In WGC window capture, the window's rendered pixels always start at (0, 0)
        // in the captured texture. Never offset left/top.
        result.cropLeft = 0;
        result.cropTop = 0;
        result.width = captureRegion.width;
        result.height = captureRegion.height;
        result.hasCrop = captureRegion.valid && (result.width < rawW || result.height < rawH);
    }

    const int samplerRawW = hasRawItem ? rawW : (hasValidWinRect ? roundUpToEvenSize(winW) : 0);
    const int samplerRawH = hasRawItem ? rawH : (hasValidWinRect ? roundUpToEvenSize(winH) : 0);
    const detail::VisibleRegion samplerRegion = detail::computeVisibleRegion(
        input,
        winW,
        winH,
        samplerRawW,
        samplerRawH,
        hasValidWinRect ? input.winRect.left : 0,
        hasValidWinRect ? input.winRect.top : 0,
        hasValidWinRect);
    if (samplerRegion.valid) {
        result.hasSamplerBounds = true;
        result.originX = samplerRegion.originX;
        result.originY = samplerRegion.originY;
        result.samplerWidth = samplerRegion.width;
        result.samplerHeight = samplerRegion.height;
    }

    return result;
}

#ifdef _WIN32

inline bool isWindowCropDisabledFromEnv() {
    char raw[32]{};
    const DWORD length = GetEnvironmentVariableA(
        "OPENSCREEN_WGC_DISABLE_WINDOW_CROP",
        raw,
        static_cast<DWORD>(sizeof(raw)));
    if (length == 0 || length >= sizeof(raw)) {
        return false;
    }
    return std::strcmp(raw, "1") == 0 ||
           std::strcmp(raw, "true") == 0 ||
           std::strcmp(raw, "TRUE") == 0;
}

struct WindowDpiProcs {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);

    GetDpiForWindowFn getDpiForWindow = nullptr;
    GetSystemMetricsForDpiFn getSystemMetricsForDpi = nullptr;
    bool disableCrop = false;

    static WindowDpiProcs load() {
        WindowDpiProcs procs;
        procs.disableCrop = isWindowCropDisabledFromEnv();
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            procs.getDpiForWindow =
                reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
            procs.getSystemMetricsForDpi = reinterpret_cast<GetSystemMetricsForDpiFn>(
                GetProcAddress(user32, "GetSystemMetricsForDpi"));
        }
        return procs;
    }

    void queryBorderMetrics(HWND hwnd, int& outBorderX, int& outBorderY) const {
        UINT dpi = 96;
        if (getDpiForWindow) {
            const UINT windowDpi = getDpiForWindow(hwnd);
            if (windowDpi > 0) {
                dpi = windowDpi;
            }
        }
        auto getMetric = [&](int index) -> int {
            if (getSystemMetricsForDpi && dpi > 0) {
                return getSystemMetricsForDpi(index, dpi);
            }
            return GetSystemMetrics(index);
        };
        outBorderX = getMetric(SM_CXSIZEFRAME) + getMetric(SM_CXPADDEDBORDER);
        outBorderY = getMetric(SM_CYSIZEFRAME) + getMetric(SM_CXPADDEDBORDER);
    }
};

inline HWND resolveRootCaptureWindow(HWND window) {
    if (!window) {
        return nullptr;
    }
    HWND root = GetAncestor(window, GA_ROOT);
    if (root && IsWindow(root)) {
        return root;
    }
    return window;
}

inline WindowCropResult queryWindowCropBounds(
    HWND hwnd,
    const WindowDpiProcs& procs,
    int rawItemWidth,
    int rawItemHeight) {
    WindowBoundsInput input;
    input.disableCrop = procs.disableCrop;
    input.rawItemWidth = rawItemWidth;
    input.rawItemHeight = rawItemHeight;

    const bool needWinRectForCaller = rawItemWidth <= 0 || rawItemHeight <= 0;
    if (procs.disableCrop) {
        if (needWinRectForCaller && hwnd) {
            RECT winRect{};
            if (GetWindowRect(hwnd, &winRect) != 0) {
                input.winRect = {winRect.left, winRect.top, winRect.right, winRect.bottom};
                input.hasWinRect = input.winRect.isValid();
            }
        }
        return computeWindowCropBounds(input);
    }

    if (hwnd) {
        input.isIconic = IsIconic(hwnd) != 0;
        if (!input.isIconic) {
            RECT dwmRect{};
            if (SUCCEEDED(DwmGetWindowAttribute(
                    hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwmRect, sizeof(dwmRect)))) {
                const WindowRectPx rect{dwmRect.left, dwmRect.top, dwmRect.right, dwmRect.bottom};
                if (rect.isValid()) {
                    input.dwmRect = rect;
                    input.hasDwmRect = true;
                }
            }
        }

        const bool dwmProvidesSamplerBounds =
            input.hasDwmRect && input.dwmRect.width() >= 2 && input.dwmRect.height() >= 2;
        if (!input.hasDwmRect || (needWinRectForCaller && !dwmProvidesSamplerBounds)) {
            if (needWinRectForCaller) {
                RECT winRect{};
                if (GetWindowRect(hwnd, &winRect) != 0) {
                    input.winRect = {winRect.left, winRect.top, winRect.right, winRect.bottom};
                    input.hasWinRect = input.winRect.isValid();
                }
            }

            if (!input.hasDwmRect && !input.isIconic) {
                WINDOWPLACEMENT wp{};
                wp.length = sizeof(wp);
                input.isMaximized =
                    GetWindowPlacement(hwnd, &wp) != 0 && wp.showCmd == SW_SHOWMAXIMIZED;
                const LONG style = GetWindowLong(hwnd, GWL_STYLE);
                input.isResizable = (style & WS_THICKFRAME) != 0;
                if (!input.isMaximized && input.isResizable) {
                    procs.queryBorderMetrics(hwnd, input.borderX, input.borderY);
                }
            }
        }
    }

    return computeWindowCropBounds(input);
}

#endif
