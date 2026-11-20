#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBIW_ASSERT(x) ((void)0)
#include "stb_image.h"
#include "stb_image_write.h"
#include "imgconv.h"
#include "core/attachments.h"
#include "core/textutil.h"
#include <algorithm>
#include <cmath>

namespace he {

bool decodeImage(const std::vector<uint8_t>& bytes, Image& out, std::string& err) {
    int w = 0, h = 0, c = 0;
    if (bytes.empty()) { err = "empty image"; return false; }
    unsigned char* px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &c, 4);
    if (!px) { err = std::string("cannot decode image: ") + stbi_failure_reason(); return false; }
    if ((long long)w * h > 64ll * 1024 * 1024) { stbi_image_free(px); err = "image too large"; return false; }
    out.w = w; out.h = h; out.hadAlpha = (c == 4 || c == 2);
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return true;
}

// Premultiplied bilinear sampling with box prefilter for big downscales.
static Image resizeTo(const Image& s, int nw, int nh) {
    Image cur = s;
    // halve while the source is > 2x the target (cheap area averaging)
    while (cur.w >= nw * 2 || cur.h >= nh * 2) {
        int hw = cur.w >= nw * 2 ? cur.w / 2 : cur.w, hh = cur.h >= nh * 2 ? cur.h / 2 : cur.h;
        Image n; n.w = hw; n.h = hh; n.hadAlpha = cur.hadAlpha; n.rgba.resize((size_t)hw * hh * 4);
        int fx = cur.w / hw, fy = cur.h / hh;
        for (int y = 0; y < hh; ++y) for (int x = 0; x < hw; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < fy; ++j) for (int i = 0; i < fx; ++i) {
                const uint8_t* p = &cur.rgba[((size_t)(y * fy + j) * cur.w + (x * fx + i)) * 4];
                float a = p[3] / 255.f; acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
            }
            uint8_t* o = &n.rgba[((size_t)y * hw + x) * 4];
            float cnt = (float)(fx * fy);
            if (acc[3] > 0) { o[0] = (uint8_t)(acc[0] / acc[3] + .5f); o[1] = (uint8_t)(acc[1] / acc[3] + .5f); o[2] = (uint8_t)(acc[2] / acc[3] + .5f); }
            else o[0] = o[1] = o[2] = 0;
            o[3] = (uint8_t)(acc[3] / cnt * 255.f + .5f);
        }
        cur = n;
    }
    Image out; out.w = nw; out.h = nh; out.hadAlpha = s.hadAlpha; out.rgba.resize((size_t)nw * nh * 4);
    for (int y = 0; y < nh; ++y) {
        float fy = (y + 0.5f) * cur.h / nh - 0.5f; int y0 = (int)std::floor(fy); float ty = fy - y0;
        int ya = std::max(0, std::min(cur.h - 1, y0)), yb = std::max(0, std::min(cur.h - 1, y0 + 1));
        for (int x = 0; x < nw; ++x) {
            float fx = (x + 0.5f) * cur.w / nw - 0.5f; int x0 = (int)std::floor(fx); float tx = fx - x0;
            int xa = std::max(0, std::min(cur.w - 1, x0)), xb = std::max(0, std::min(cur.w - 1, x0 + 1));
            const uint8_t* p[4] = {&cur.rgba[((size_t)ya * cur.w + xa) * 4], &cur.rgba[((size_t)ya * cur.w + xb) * 4],
                                   &cur.rgba[((size_t)yb * cur.w + xa) * 4], &cur.rgba[((size_t)yb * cur.w + xb) * 4]};
            float wgt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
            float acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) { float a = p[k][3] / 255.f * wgt[k]; acc[0] += p[k][0] * a; acc[1] += p[k][1] * a; acc[2] += p[k][2] * a; acc[3] += a; }
            uint8_t* o = &out.rgba[((size_t)y * nw + x) * 4];
            if (acc[3] > 0) { o[0] = (uint8_t)(acc[0] / acc[3] + .5f); o[1] = (uint8_t)(acc[1] / acc[3] + .5f); o[2] = (uint8_t)(acc[2] / acc[3] + .5f); }
            else o[0] = o[1] = o[2] = 0;
            o[3] = (uint8_t)(std::min(1.f, acc[3]) * 255.f + .5f);
        }
    }
    return out;
}

Image fitImage(const Image& src, int w, int h, const std::string& fit) {
    if (src.w == w && src.h == h) return src;
    if (fit == "stretch") return resizeTo(src, w, h);
    double sx = (double)w / src.w, sy = (double)h / src.h;
    double sc = fit == "contain" ? std::min(sx, sy) : std::max(sx, sy);
    int iw = std::max(1, (int)std::lround(src.w * sc)), ih = std::max(1, (int)std::lround(src.h * sc));
    Image scaled = resizeTo(src, iw, ih);
    Image out; out.w = w; out.h = h; out.hadAlpha = src.hadAlpha || fit == "contain";
    out.rgba.assign((size_t)w * h * 4, 0);
    int ox = (w - iw) / 2, oy = (h - ih) / 2;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        int sxp = x - ox, syp = y - oy;
        if (sxp < 0 || syp < 0 || sxp >= iw || syp >= ih) continue;   // letterbox stays transparent
        std::copy(&scaled.rgba[((size_t)syp * iw + sxp) * 4], &scaled.rgba[((size_t)syp * iw + sxp) * 4] + 4, &out.rgba[((size_t)y * w + x) * 4]);
    }
    return out;
}

static void sink(void* ctx, void* data, int size) {
    std::vector<uint8_t>* v = (std::vector<uint8_t>*)ctx; const uint8_t* p = (const uint8_t*)data; v->insert(v->end(), p, p + size);
}
bool encodeImage(const Image& img, const std::string& ext, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    std::string e = heng::lowerCopy(ext);
    int ok = 0;
    // formats without alpha: flatten over black
    std::vector<uint8_t> flat;
    const uint8_t* data = img.rgba.data(); int comp = 4;
    if (e == "jpg" || e == "jpeg" || e == "bmp" || (e == "png" && !img.hadAlpha) || (e == "tga" && !img.hadAlpha)) {
        flat.resize((size_t)img.w * img.h * 3);
        for (size_t i = 0; i < (size_t)img.w * img.h; ++i) {
            float a = img.rgba[i * 4 + 3] / 255.f;
            for (int k = 0; k < 3; ++k) flat[i * 3 + k] = (uint8_t)(img.rgba[i * 4 + k] * a + .5f);
        }
        data = flat.data(); comp = 3;
    }
    if (e == "png") ok = stbi_write_png_to_func(sink, &out, img.w, img.h, comp, data, img.w * comp);
    else if (e == "jpg" || e == "jpeg") ok = stbi_write_jpg_to_func(sink, &out, img.w, img.h, comp, data, 92);
    else if (e == "bmp") ok = stbi_write_bmp_to_func(sink, &out, img.w, img.h, comp, data);
    else if (e == "tga") ok = stbi_write_tga_to_func(sink, &out, img.w, img.h, comp, data);
    else { err = "cannot write ." + ext + " images (supported: png, jpg, bmp, tga) - use replace_file with a pre-converted file"; return false; }
    if (!ok || out.empty()) { err = "image encoding failed"; return false; }
    return true;
}

bool ImageConverter::convert(const std::vector<uint8_t>& orig, const std::string& origName,
                             const std::vector<uint8_t>& src, const std::string& fit,
                             std::vector<uint8_t>& out, std::string& note, std::string& err) {
    Image o, s;
    std::string e1;
    if (!decodeImage(orig, o, e1)) { err = "the original " + origName + " is not an image this tool can read (" + e1 + ")"; return false; }
    if (!decodeImage(src, s, e1)) { err = "the attachment is not a readable image (" + e1 + ")"; return false; }
    Image r = fitImage(s, o.w, o.h, fit);
    r.hadAlpha = o.hadAlpha || (s.hadAlpha && heng::lowerCopy(heng::extensionOf(origName)) == "png");
    if (!encodeImage(r, heng::extensionOf(origName), out, err)) return false;
    note = std::to_string(s.w) + "x" + std::to_string(s.h) + " -> " + std::to_string(o.w) + "x" + std::to_string(o.h) + " (" + fit + ")";
    return true;
}

} // namespace he
