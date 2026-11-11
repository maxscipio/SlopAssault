#pragma once
#include <cstdint>

// Renderer enhancements on the SDK's gpu::Extension (SSAO, anti-aliasing, soft and larger shadows), each switched
// in the F8 panel and kept in settings.toml.
namespace enhance {

extern bool ssao;
extern bool aa;
extern bool soft_shadows;
extern uint32_t ssao_strength;  // percent
extern uint32_t ssao_radius;    // hundredths of a world unit
extern uint32_t ssao_debug;     // settings file only: 2 shows the occlusion alone, 3 compares with and without (SSAO, AA), 4 the soft shadows
extern uint32_t shadow_scale;   // shadow map size as a multiple of the game's 256: 2, 4 or 8; 0 leaves it to the render scale

}  // namespace enhance
