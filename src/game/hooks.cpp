// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "cpu/x86_ops.h"

// Wide and ultrawide: the game draws its 3D with a 4:3 projection. The camera (this in ecx) has a horizontal field
// of view at +0x88 and an integer rectangle at +0x9C / +0xA0 (640 x 480). Its projection takes the aspect from the
// rectangle and the vertical field as the horizontal one divided by that aspect; sub_000381B0 builds the culling planes
// from the same two values. Both are made `wide` times larger while they are built, so the screen shows more of the
// world at the sides with the vertical field kept, and nothing at the edges is culled. app::Aspect() is 4:3 (the factor
// is 1), 16:9, or the screen's shape. The camera is rebuilt when its parameters are set, so a change of the aspect
// setting applies at the next start.
//
// The function also sets the device viewport from that rectangle, through sub_00037D80, which has to stay 640 wide: the
// renderer stretches the viewport across the screen, so a wider one would stretch the picture and push it off to one side.
static uint32_t g_camera = 0, g_rect_width = 0;

X86_EXTERN_FUNC(__imp__sub_00037D80);
X86_FUNC(sub_00037D80) {
    uint32_t out = X86_MEM32(ctx.esp + 4);
    bool ours = g_camera && ctx.ecx == g_camera;
    __imp__sub_00037D80(ctx, base);
    if (ours) X86_MEM32(out + 8) = g_rect_width;
}

X86_EXTERN_FUNC(__imp__sub_000390F0);
X86_FUNC(sub_000390F0) {
    constexpr uint32_t kFov = 0x88, kRectWidth = 0x9C;
    uint32_t camera = ctx.ecx;
    int32_t width = int32_t(X86_MEM32(camera + kRectWidth));
    float fov = X86_MEMF32(camera + kFov);
    float wide = std::max(1.0f, app::Aspect() * 0.75f);
    X86_MEM32(camera + kRectWidth) = uint32_t(std::lround(width * wide));
    X86_MEMF32(camera + kFov) = fov * wide;
    g_camera = camera;
    g_rect_width = uint32_t(width);
    __imp__sub_000390F0(ctx, base);
    g_camera = 0;
    X86_MEM32(camera + kRectWidth) = uint32_t(width);
    X86_MEMF32(camera + kFov) = fov;
}
