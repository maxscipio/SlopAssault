#include "common/enhance.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "core/log.h"
#include "gpu/ext.h"

namespace enhance {

bool ssao = false;
bool aa = false;
bool soft_shadows = false;
uint32_t ssao_strength = 100;
uint32_t ssao_radius = 1000;
uint32_t ssao_debug = 0;
uint32_t shadow_scale = 0;
uint32_t draw_distance = 100;

namespace {
using gpu::Context;
using gpu::State;
using gpu::Target;
using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kBgra = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr DXGI_FORMAT kDepthRead = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;  // the host depth targets are R32G8X24_TYPELESS
constexpr DXGI_FORMAT kAoFormat = DXGI_FORMAT_R16G16_FLOAT;            // occlusion, view depth
constexpr uint32_t kMZWriteEnable = 0x35C;
constexpr uint32_t kOpaqueDraws = 10;  // opaque world draws before the scene counts as a 3D frame

// The game's projection is z' = A w - n A with w the view depth: n 4.0, far 3035, A = f / (f - n) = 1.00132 (read off
// the world matrices in vsc[96..100] of a capture, with a focal length of 496 px over the 640 x 480 picture); the depth
// buffer holds the reversed z/w as a 32 bit float: 1 at the near plane, 0 at the far one.
const char* kAoHlsl = R"(
cbuffer P : register(b1) { float4 p0; float4 p1; };
Texture2D src0 : register(t0);
Texture2D src1 : register(t1);
SamplerState ls : register(s0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
static const float kNear = 4.0, kA = 1.0013199;
// ao: p0 = texel size, tan of the half field of view (x, y); p1 = radius (world units), strength, fade from, to
float Dep(float2 uv) {
    float r = src0.SampleLevel(ps, uv, 0).r;
    return kNear * kA / max(r + kA - 1, 1e-6);
}
float3 Pos(float2 uv, float d) { return float3((uv * float2(2, -2) + float2(-1, 1)) * p0.zw * d, d); }
float2 ao(O i) : SV_Target {
    float2 uv = i.uv;
    float d = Dep(uv);
    if (d >= p1.w) return float2(1, d);
    float3 P = Pos(uv, d);
    float2 px = p0.xy;
    float dl = Dep(uv - float2(px.x, 0)), dr = Dep(uv + float2(px.x, 0));
    float du = Dep(uv - float2(0, px.y)), dd = Dep(uv + float2(0, px.y));
    // the neighbours on the same surface: the nearer one in depth on each axis
    float3 tx = abs(dr - d) < abs(d - dl) ? Pos(uv + float2(px.x, 0), dr) - P : P - Pos(uv - float2(px.x, 0), dl);
    float3 ty = abs(dd - d) < abs(d - du) ? Pos(uv + float2(0, px.y), dd) - P : P - Pos(uv - float2(0, px.y), du);
    float3 n = normalize(cross(tx, ty));
    if (dot(n, P) > 0) n = -n;
    float2 ruv = 0.5 * p1.x / (d * p0.zw);
    float shrink = min(1.0, 0.12 / max(ruv.y, 1e-6));  // near the camera: at most an eighth of the screen,
    ruv *= shrink;                                     // and the radius in the world with it
    float r = p1.x * shrink;
    float noise = frac(52.9829189 * frac(dot(i.p.xy, float2(0.06711056, 0.00583715))));
    float occ = 0;
    const int N = 12;
    [unroll] for (int k = 0; k < N; k++) {
        float t = (k + noise) / N;
        float a = 2.39996323 * k + noise * 6.2831853;
        float2 suv = uv + float2(cos(a), sin(a)) * ruv * t;
        float3 v = (Pos(suv, Dep(suv)) - P) / r;
        float vv = dot(v, v);
        occ += saturate(dot(n, v) * rsqrt(vv + 1e-6) - 0.1) * saturate(1 - vv);
    }
    float A = saturate(1 - p1.y * occ * (2.0 / N));
    return float2(lerp(A, 1, saturate((d - p1.z) / max(p1.w - p1.z, 1e-6))), d);
}
// blur: p0.xy = one texel along the axis
float2 blur(O i) : SV_Target {
    float2 c = src0.SampleLevel(ps, i.uv, 0).rg;
    float sum = c.r, sw = 1;
    [unroll] for (int k = -4; k <= 4; k++) {
        if (k == 0) continue;
        float2 s = src0.SampleLevel(ps, i.uv + p0.xy * k, 0).rg;
        float w = exp(-k * k / 12.0) * saturate(1 - abs(s.g - c.g) / (0.03 * c.g + 1e-6));
        sum += s.r * w;
        sw += w;
    }
    return float2(sum / sw, c.g);
}
// apply: p0.x = 2 shows the occlusion alone, 3 only on the right half (a same frame comparison)
float4 apply(O i) : SV_Target {
    float4 c = src0.SampleLevel(ps, i.uv, 0);
    float a = src1.SampleLevel(ls, i.uv, 0).r;  // the occlusion is at half size: filtered up
    if (p0.x > 2.5) return i.uv.x < 0.5 ? c : abs(i.uv.x - 0.5) < 0.001 ? float4(1, 0, 0, c.a) : float4(c.rgb * a, c.a);
    if (p0.x > 1.5) return float4(a, a, a, c.a);
    return float4(c.rgb * a, c.a);
}
)";

// Edge anti-aliasing after FXAA: blend across luma edges by the distance to the edge's nearer end.
const char* kAaHlsl = R"(
cbuffer P : register(b1) { float4 p0; float4 p1; };
Texture2D src0 : register(t0);
SamplerState ls : register(s0);
SamplerState ps : register(s1);
struct O { float4 p : SV_Position; float2 uv : TEXCOORD0; };
O vs(uint id : SV_VertexID) {
    O o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.p = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float3 C(float2 uv) { return src0.SampleLevel(ls, uv, 0).rgb; }
float L(float2 uv) { return dot(C(uv), float3(0.299, 0.587, 0.114)); }
float4 aa(O i) : SV_Target {
    float2 uv = i.uv, px = p0.xy;
    float4 cm = src0.SampleLevel(ps, uv, 0);
    if (p1.x > 1.5 && uv.x < 0.5) return cm;
    float lm = dot(cm.rgb, float3(0.299, 0.587, 0.114));
    float ln = L(uv + float2(0, -px.y)), lsth = L(uv + float2(0, px.y));
    float le = L(uv + float2(px.x, 0)), lw = L(uv + float2(-px.x, 0));
    float hi = max(lm, max(max(ln, lsth), max(le, lw))), lo = min(lm, min(min(ln, lsth), min(le, lw)));
    float range = hi - lo;
    if (range < max(0.0312, hi * 0.125)) return cm;
    float lnw = L(uv - px), lse = L(uv + px);
    float lne = L(uv + float2(px.x, -px.y)), lsw = L(uv + float2(-px.x, px.y));
    float eh = abs(lnw + lne - 2 * ln) + 2 * abs(lw + le - 2 * lm) + abs(lsw + lse - 2 * lsth);
    float ev = abs(lnw + lsw - 2 * lw) + 2 * abs(ln + lsth - 2 * lm) + abs(lne + lse - 2 * le);
    bool horz = eh >= ev;
    float l1 = horz ? ln : lw, l2 = horz ? lsth : le;
    float g1 = abs(l1 - lm), g2 = abs(l2 - lm);
    float stepv = horz ? px.y : px.x;
    float ledge, grad;
    if (g1 >= g2) { stepv = -stepv; ledge = 0.5 * (l1 + lm); grad = g1; }
    else { ledge = 0.5 * (l2 + lm); grad = g2; }
    float2 e = uv + (horz ? float2(0, stepv * 0.5) : float2(stepv * 0.5, 0));
    float2 dir = horz ? float2(px.x, 0) : float2(0, px.y);
    float2 a = e - dir, b = e + dir;
    float da = L(a) - ledge, db = L(b) - ledge;
    bool ea = abs(da) >= grad * 0.25, eb = abs(db) >= grad * 0.25;
    const float steps[10] = {1, 1, 1, 1.5, 2, 2, 2, 4, 8, 8};
    [unroll] for (int k = 0; k < 10; k++) {
        if (ea && eb) break;
        if (!ea) { a -= dir * steps[k]; da = L(a) - ledge; ea = abs(da) >= grad * 0.25; }
        if (!eb) { b += dir * steps[k]; db = L(b) - ledge; eb = abs(db) >= grad * 0.25; }
    }
    float dist_a = horz ? uv.x - a.x : uv.y - a.y, dist_b = horz ? b.x - uv.x : b.y - uv.y;
    bool near_a = dist_a < dist_b;
    float off = 0.5 - min(dist_a, dist_b) / (dist_a + dist_b);
    if (((near_a ? da : db) < 0) == (lm < ledge)) off = 0;  // the nearer end is not where this edge ends
    float avg = (2 * (ln + lsth + le + lw) + lnw + lne + lsw + lse) / 12;
    float sub = saturate(abs(avg - lm) / range);
    sub = (-2 * sub + 3) * sub * sub;
    off = max(off, sub * sub * 0.75);
    float2 f = uv + (horz ? float2(0, off * stepv) : float2(off * stepv, 0));
    return float4(C(f), cm.a);
}
)";

// Soft shadows take a 4 x 4 grid of the sampler's own four texel compares; ext.x carries the setting, so it changes
// without new shaders.
struct ShadowHooks final : gpu::ShaderHooks {
    std::string Shadow() const override {
        return R"(float4 shadow(Texture2D<float> t, SamplerComparisonState s, float2 uv, float z, int fmt, float2 px, float2 size) {
    float mx = fmt == 2 ? 65535.0 : 16777215.0;
    float r = clamp(z, 0, mx) / mx;
    if (ext.x < 0.5 || px.x < ext.y) return t.SampleCmpLevelZero(s, uv, r).xxxx;
    float2 st = 1.0 / size;
    float sum = 0;
    [unroll] for (int y = 0; y < 4; y++)
        [unroll] for (int x = 0; x < 4; x++) sum += t.SampleCmpLevelZero(s, uv + (float2(x, y) - 1.5) * st, r);
    return (sum / 16).xxxx;
}
)";
    }
};

class Extension final : public gpu::Extension {
public:
    bool Settings() override {
        bool remake = shadow_scale != shadow_made_;  // the shadow map is made again at its new size
        shadow_made_ = shadow_scale;
        return remake;
    }

    void Reset(Context&) override {
        for (auto& r : ao_) r.Reset();
        post_.Reset();
        scene_rt_ = scene_ds_ = nullptr;
    }

    const gpu::ShaderHooks* Shaders() const override { return &hooks_; }

    void Constants(float (*)[4], float* ext, const Target* rt, uint32_t) override {
        ext[0] = soft_shadows ? 1.0f : 0.0f;
        ext[1] = ssao_debug == 4 && rt ? rt->w * 0.5f : 0.0f;  // the right half only, for a same frame comparison
    }

    // the sun's shadow map: a square depth target (256 x 256 here) drawn with a colour target of its size
    void MakeTarget(const gpu::Surface&, bool depth, uint32_t gw, uint32_t gh, float& scale, DXGI_FORMAT&) override {
        if (gw == gh && gw >= kShadowMin && gw <= kShadowMax && shadow_scale) scale = float(shadow_scale);
    }

    bool Bound(Context& c, const State& s, Target* rt, Target* ds, bool pre) override {
        uint64_t frame = c.Presents();
        if (frame != frame_) {
            frame_ = frame;
            done_ = false;
            opaque_ = 0;
            scene_rt_ = scene_ds_ = nullptr;
        }
        if (!rt) return false;
        uint32_t gw, gh;
        c.GuestSize(gw, gh);
        bool scene = ds && rt->gw == gw && rt->gh == gh && ds->w == rt->w && ds->h == rt->h;
        if (!ssao || done_) return false;
        bool decal = s.rs[gpu::kRsZBias] != 0;  // the character shadows are drawn between the opaque draws
        if (scene && !pre && !decal && s.rs[gpu::kRsZEnable] && s.simple[kMZWriteEnable >> 2]) {
            opaque_++;
            scene_rt_ = rt;
            scene_ds_ = ds;
            return false;
        }
        // the first draw after the world that does not write depth (this game has no pretransformed vertices; its HUD
        // goes through vertex shaders): the depth buffer then holds everything opaque
        bool late = pre || (scene && !decal && !s.simple[kMZWriteEnable >> 2]);
        if (opaque_ < kOpaqueDraws || !scene_rt_ || !late) return false;
        done_ = true;
        return Occlude(c, *scene_rt_, *scene_ds_);
    }

    ID3D12Resource* Present(Context& c, Target& t, DXGI_FORMAT& fmt) override {
        if (!aa || t.depth) return nullptr;
        if (!EnsureTexture(c, post_, post_state_, t.w, t.h, t.fmt, 2)) {
            LOGR("enhance: no post texture for %ux%u", t.w, t.h);
            return nullptr;
        }
        c.Transition(t.res.Get(), t.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        c.Transition(post_.Get(), post_state_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* src[3] = {t.res.Get(), nullptr, nullptr};
        DXGI_FORMAT sf[3] = {t.fmt, kBgra, kBgra};
        float p[8] = {1.0f / t.w, 1.0f / t.h, 0, 0, ssao_debug == 3 ? 2.0f : 1.0f};
        c.Pass(kAaHlsl, "aa", t.fmt, Rtv(2), t.w, t.h, src, sf, p, -1);
        c.Unbind();
        c.Transition(post_.Get(), post_state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        fmt = t.fmt;
        return post_.Get();
    }

private:
    // a w x h render target of format f with its view at Rtv(slot); made again when the size changes
    bool EnsureTexture(Context& c, ComPtr<ID3D12Resource>& r, D3D12_RESOURCE_STATES& st, uint32_t w, uint32_t h, DXGI_FORMAT f,
                       int slot) {
        if (r) {
            D3D12_RESOURCE_DESC d = r->GetDesc();
            if (d.Width == w && d.Height == h && d.Format == f) return true;
            c.Release(r);
            r.Reset();
        }
        if (!EnsureHeap(c)) return false;
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = w;
        td.Height = h;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = f;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        st = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (FAILED(c.Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, st, nullptr, IID_PPV_ARGS(&r)))) return false;
        c.Device()->CreateRenderTargetView(r.Get(), nullptr, Rtv(slot));
        return true;
    }

    bool EnsureHeap(Context& c) {
        if (rtv_heap_) return true;
        D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvSlots};
        if (FAILED(c.Device()->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_)))) return false;
        rtv_inc_ = c.Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(int i) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += size_t(i) * rtv_inc_;
        return h;
    }

    // occlusion into ao_[0], blurred across and back, then the scene colour times it; true: rebind
    bool Occlude(Context& c, Target& rt, Target& ds) {
        uint32_t w = std::max(rt.w / 2, 1u), h = std::max(rt.h / 2, 1u);  // the occlusion is soft: half size is enough
        if (!EnsureTexture(c, ao_[0], ao_state_[0], w, h, kAoFormat, 0) ||
            !EnsureTexture(c, ao_[1], ao_state_[1], w, h, kAoFormat, 1))
            return false;
        c.Transition(ds.res.Get(), ds.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        c.Transition(ao_[0].Get(), ao_state_[0], D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* src[3] = {ds.res.Get(), nullptr, nullptr};
        DXGI_FORMAT fmt[3] = {kDepthRead, kBgra, kBgra};
        // the game's view over a 4:3 picture (focal length 496 px at 640 x 480); the occlusion fades out from 1500 to
        // 2400 units, and the radius is in world units too (a character is about 100 of them)
        float p[8] = {1.0f / w, 1.0f / h, 0.645f, 0.4839f, ssao_radius / 25.0f, ssao_strength / 100.0f, 1500, 2400};
        c.Pass(kAoHlsl, "ao", kAoFormat, Rtv(0), w, h, src, fmt, p, -1);
        for (int axis = 0; axis < 2; axis++) {
            int from = axis, to = 1 - axis;
            c.Transition(ao_[from].Get(), ao_state_[from], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            c.Transition(ao_[to].Get(), ao_state_[to], D3D12_RESOURCE_STATE_RENDER_TARGET);
            ID3D12Resource* bs[3] = {ao_[from].Get(), nullptr, nullptr};
            DXGI_FORMAT bf[3] = {kAoFormat, kBgra, kBgra};
            float bp[8] = {axis == 0 ? 1.0f / w : 0.0f, axis == 1 ? 1.0f / h : 0.0f};
            c.Pass(kAoHlsl, "blur", kAoFormat, Rtv(to), w, h, bs, bf, bp, -1);
        }
        c.Transition(ao_[0].Get(), ao_state_[0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ID3D12Resource* fb = c.Feedback(rt);
        if (!fb) return true;
        c.Transition(rt.res.Get(), rt.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource* as[3] = {fb, ao_[0].Get(), nullptr};
        DXGI_FORMAT af[3] = {rt.fmt, kAoFormat, kBgra};
        float ap[8] = {ssao_debug == 2 ? 2.0f : ssao_debug == 3 ? 3.0f : 1.0f};
        c.Pass(kAoHlsl, "apply", rt.fmt, c.View(rt), rt.w, rt.h, as, af, ap, -1);
        c.Unbind();
        return true;
    }

    uint64_t frame_ = ~0ull;
    uint32_t shadow_made_ = 0;
    ShadowHooks hooks_;
    static constexpr uint32_t kShadowMin = 128, kShadowMax = 1024;  // the game's own shadow map sizes
    bool done_ = false;
    uint32_t opaque_ = 0;
    Target* scene_rt_ = nullptr;
    Target* scene_ds_ = nullptr;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    UINT rtv_inc_ = 0;
    ComPtr<ID3D12Resource> ao_[2];
    D3D12_RESOURCE_STATES ao_state_[2]{};
    ComPtr<ID3D12Resource> post_;  // the anti-aliased picture
    D3D12_RESOURCE_STATES post_state_{};
    static constexpr UINT kRtvSlots = 3;  // the occlusion and its blur, the anti-aliased picture
};

// before the backend is made at CreateDevice
const bool g_registered = [] {
    gpu::SetExtension(std::make_unique<Extension>());
    return true;
}();
}  // namespace

}  // namespace enhance
