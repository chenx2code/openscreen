#include "window_crop_bounds.h"

#include <cstdio>

namespace {

int failures = 0;

void expectEqual(const char* label, long actual, long expected) {
    if (actual == expected) {
        return;
    }
    std::printf("FAIL %s: expected %ld, got %ld\n", label, expected, actual);
    ++failures;
}

void expectBool(const char* label, bool actual, bool expected) {
    if (actual == expected) {
        return;
    }
    std::printf(
        "FAIL %s: expected %s, got %s\n",
        label,
        expected ? "true" : "false",
        actual ? "true" : "false");
    ++failures;
}

} // namespace

int main() {
    // 1. Standard resizable window with DWM bounds smaller than WGC item size.
    {
        WindowBoundsInput input;
        input.isResizable = true;
        input.hasDwmRect = true;
        input.dwmRect = {107, 200, 1373, 992}; // 1266 x 792
        input.hasWinRect = true;
        input.winRect = {100, 200, 1380, 1000}; // 1280 x 800
        input.rawItemWidth = 1280;
        input.rawItemHeight = 800;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("dwm rawWidth", res.rawWidth, 1280);
        expectEqual("dwm rawHeight", res.rawHeight, 800);
        expectEqual("dwm width", res.width, 1266);
        expectEqual("dwm height", res.height, 792);
        expectEqual("dwm cropLeft", res.cropLeft, 0);
        expectEqual("dwm cropTop", res.cropTop, 0);
        expectBool("dwm hasCrop", res.hasCrop, true);
        expectBool("dwm hasSamplerBounds", res.hasSamplerBounds, true);
        expectEqual("dwm originX", res.originX, 107);
        expectEqual("dwm originY", res.originY, 200);
        expectEqual("dwm samplerWidth matches capture", res.samplerWidth, res.width);
        expectEqual("dwm samplerHeight matches capture", res.samplerHeight, res.height);
    }

    // 2. Odd DWM dimensions round down to even; odd raw dimensions round up to even.
    {
        WindowBoundsInput input;
        input.isResizable = true;
        input.hasDwmRect = true;
        input.dwmRect = {107, 50, 1108, 751}; // 1001 x 701
        input.hasWinRect = true;
        input.winRect = {100, 50, 1115, 757}; // 1015 x 707
        input.rawItemWidth = 1015;
        input.rawItemHeight = 707;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("odd dwm rawWidth", res.rawWidth, 1016);
        expectEqual("odd dwm rawHeight", res.rawHeight, 708);
        expectEqual("odd dwm width", res.width, 1000);
        expectEqual("odd dwm height", res.height, 700);
        expectBool("odd dwm hasCrop", res.hasCrop, true);
        expectEqual("odd dwm samplerWidth matches capture", res.samplerWidth, res.width);
        expectEqual("odd dwm samplerHeight matches capture", res.samplerHeight, res.height);
    }

    // 3. Odd non-resizable window where DWM equals outer size still crops 1px padding.
    {
        WindowBoundsInput input;
        input.hasDwmRect = true;
        input.dwmRect = {10, 20, 1011, 721}; // 1001 x 701
        input.hasWinRect = true;
        input.winRect = {10, 20, 1011, 721};
        input.rawItemWidth = 1001;
        input.rawItemHeight = 701;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("odd fixed rawWidth", res.rawWidth, 1002);
        expectEqual("odd fixed rawHeight", res.rawHeight, 702);
        expectEqual("odd fixed width", res.width, 1000);
        expectEqual("odd fixed height", res.height, 700);
        expectBool("odd fixed hasCrop", res.hasCrop, true);
        expectEqual("odd fixed samplerWidth matches capture", res.samplerWidth, res.width);
        expectEqual("odd fixed samplerHeight matches capture", res.samplerHeight, res.height);
    }

    // 4. DWM dimensions larger than raw dimensions are clamped to raw dimensions.
    {
        WindowBoundsInput input;
        input.hasDwmRect = true;
        input.dwmRect = {0, 0, 820, 610};
        input.hasWinRect = true;
        input.winRect = {0, 0, 800, 600};
        input.rawItemWidth = 800;
        input.rawItemHeight = 600;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("clamped width", res.width, 800);
        expectEqual("clamped height", res.height, 600);
        expectBool("clamped hasCrop", res.hasCrop, false);
        expectEqual("clamped samplerWidth matches capture", res.samplerWidth, res.width);
        expectEqual("clamped samplerHeight matches capture", res.samplerHeight, res.height);
    }

    // 5. < 2px visible size falls back cleanly to (raw / 2) * 2 without zero output.
    {
        WindowBoundsInput input;
        input.isResizable = true;
        input.hasDwmRect = true;
        input.dwmRect = {108, 200, 109, 201}; // 1 x 1
        input.hasWinRect = true;
        input.winRect = {100, 200, 116, 300}; // 16 x 100
        input.rawItemWidth = 16;
        input.rawItemHeight = 100;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("tiny fallback width", res.width, 16);
        expectEqual("tiny fallback height", res.height, 100);
        expectBool("tiny fallback hasCrop", res.hasCrop, false);
        expectEqual("tiny fallback samplerWidth matches", res.samplerWidth, res.width);
        expectEqual("tiny fallback samplerHeight matches", res.samplerHeight, res.height);
    }

    // 6. Fallback to DPI border metrics when DWM unavailable: !isMaximized && isResizable, odd parity.
    {
        WindowBoundsInput captureInput;
        captureInput.isResizable = true;
        captureInput.rawItemWidth = 1001;
        captureInput.rawItemHeight = 701;
        captureInput.borderX = 8;
        captureInput.borderY = 8;

        WindowBoundsInput samplerInput;
        samplerInput.isResizable = true;
        samplerInput.hasWinRect = true;
        samplerInput.winRect = {100, 200, 1101, 901}; // 1001 x 701
        samplerInput.borderX = 8;
        samplerInput.borderY = 8;

        const WindowCropResult cap = computeWindowCropBounds(captureInput);
        const WindowCropResult samp = computeWindowCropBounds(samplerInput);
        expectEqual("fallback odd rawWidth", cap.rawWidth, 1002);
        expectEqual("fallback odd rawHeight", cap.rawHeight, 702);
        // 1001 - 16 = 985 -> 984; 701 - 8 = 693 -> 692 (parity must match sampler!)
        expectEqual("fallback odd capture width", cap.width, 984);
        expectEqual("fallback odd capture height", cap.height, 692);
        expectBool("fallback odd hasCrop", cap.hasCrop, true);
        expectEqual("fallback odd sampler width matches capture", samp.samplerWidth, cap.width);
        expectEqual("fallback odd sampler height matches capture", samp.samplerHeight, cap.height);
        expectEqual("fallback odd sampler originX", samp.originX, 108);
        expectEqual("fallback odd sampler originY", samp.originY, 200);
    }

    // 7. Border fallback skipped when maximized, iconic, or non-resizable.
    {
        WindowBoundsInput maxInput;
        maxInput.isMaximized = true;
        maxInput.isResizable = true;
        maxInput.hasWinRect = true;
        maxInput.winRect = {0, 0, 1920, 1040};
        maxInput.rawItemWidth = 1920;
        maxInput.rawItemHeight = 1040;
        maxInput.borderX = 8;
        maxInput.borderY = 8;

        const WindowCropResult maxRes = computeWindowCropBounds(maxInput);
        expectEqual("maximized width", maxRes.width, 1920);
        expectEqual("maximized height", maxRes.height, 1040);
        expectBool("maximized hasCrop", maxRes.hasCrop, false);
        expectEqual("maximized sampler originX", maxRes.originX, 0);
        expectEqual("maximized sampler width", maxRes.samplerWidth, 1920);

        WindowBoundsInput iconicInput;
        iconicInput.isIconic = true;
        iconicInput.isResizable = true;
        iconicInput.hasDwmRect = true; // Must be ignored when iconic!
        iconicInput.dwmRect = {10, 10, 500, 500};
        iconicInput.hasWinRect = true;
        iconicInput.winRect = {-32000, -32000, -31840, -31972}; // 160 x 28
        iconicInput.rawItemWidth = 160;
        iconicInput.rawItemHeight = 28;
        iconicInput.borderX = 8;
        iconicInput.borderY = 8;

        const WindowCropResult iconicRes = computeWindowCropBounds(iconicInput);
        expectEqual("iconic capture width", iconicRes.width, 160);
        expectEqual("iconic capture height", iconicRes.height, 28);
        expectBool("iconic hasCrop", iconicRes.hasCrop, false);
        expectEqual("iconic sampler originX", iconicRes.originX, -32000);
        expectEqual("iconic sampler originY", iconicRes.originY, -32000);
        expectEqual("iconic sampler width", iconicRes.samplerWidth, 160);
        expectEqual("iconic sampler height", iconicRes.samplerHeight, 28);

        WindowBoundsInput fixedInput;
        fixedInput.isResizable = false;
        fixedInput.hasWinRect = true;
        fixedInput.winRect = {50, 60, 850, 660}; // 800 x 600
        fixedInput.rawItemWidth = 800;
        fixedInput.rawItemHeight = 600;
        fixedInput.borderX = 8;
        fixedInput.borderY = 8;

        const WindowCropResult fixedRes = computeWindowCropBounds(fixedInput);
        expectEqual("non-resizable width", fixedRes.width, 800);
        expectEqual("non-resizable height", fixedRes.height, 600);
        expectBool("non-resizable hasCrop", fixedRes.hasCrop, false);
        expectEqual("non-resizable sampler originX", fixedRes.originX, 50);
    }

    // 8. disableCrop keeps capture uncropped (rawWidth) and preserves winRect verbatim for sampler.
    {
        WindowBoundsInput input;
        input.disableCrop = true;
        input.isResizable = true;
        input.hasDwmRect = true;
        input.dwmRect = {107, 200, 1093, 893};
        input.hasWinRect = true;
        input.winRect = {100, 200, 1101, 901}; // 1001 x 701
        input.rawItemWidth = 1001;
        input.rawItemHeight = 701;
        input.borderX = 8;
        input.borderY = 8;

        const WindowCropResult res = computeWindowCropBounds(input);
        expectEqual("disableCrop rawWidth", res.rawWidth, 1002);
        expectEqual("disableCrop rawHeight", res.rawHeight, 702);
        expectEqual("disableCrop capture width", res.width, 1002);
        expectEqual("disableCrop capture height", res.height, 702);
        expectBool("disableCrop hasCrop", res.hasCrop, false);
        expectBool("disableCrop hasSamplerBounds", res.hasSamplerBounds, true);
        expectEqual("disableCrop sampler originX", res.originX, 100);
        expectEqual("disableCrop sampler originY", res.originY, 200);
        expectEqual("disableCrop sampler width preserves odd winRect", res.samplerWidth, 1001);
        expectEqual("disableCrop sampler height preserves odd winRect", res.samplerHeight, 701);
    }

    // 9. Border fallback with tiny outer width where subtraction falls below 2px keeps unshifted origin.
    {
        WindowBoundsInput input;
        input.isResizable = true;
        input.hasWinRect = true;
        input.winRect = {100, 200, 117, 300}; // 17 x 100
        input.rawItemWidth = 17;
        input.rawItemHeight = 100;
        input.borderX = 8;
        input.borderY = 8;

        const WindowCropResult res = computeWindowCropBounds(input);
        // 17 - 16 = 1 -> aligned 0 < 2 -> fallback to (18 / 2) * 2 = 18; originX stays 100.
        expectEqual("tiny border fallback width", res.width, 18);
        expectEqual("tiny border fallback originX", res.originX, 100);
        expectEqual("tiny border fallback samplerWidth", res.samplerWidth, 18);
    }

    // 10. Sampler without WGC item size and without winRect (only DWM) works cleanly.
    {
        WindowBoundsInput input;
        input.hasDwmRect = true;
        input.dwmRect = {300, 400, 1101, 1001}; // 801 x 601

        const WindowCropResult res = computeWindowCropBounds(input);
        expectBool("dwm-only sampler hasSamplerBounds", res.hasSamplerBounds, true);
        expectEqual("dwm-only sampler originX", res.originX, 300);
        expectEqual("dwm-only sampler originY", res.originY, 400);
        expectEqual("dwm-only sampler width", res.samplerWidth, 800);
        expectEqual("dwm-only sampler height", res.samplerHeight, 600);
    }

    if (failures == 0) {
        std::printf("window_crop_bounds_test: all assertions passed\n");
        return 0;
    }
    std::printf("window_crop_bounds_test: %d assertion(s) failed\n", failures);
    return 1;
}
