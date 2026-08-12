// The idle card: what the camera shows when no producer is feeding it.
//
// A camera that goes black - or worse, freezes on the last frame of the
// previous presentation - reads as broken to everyone in the meeting. The
// idle card replaces that dead air with a full-frame animated standby screen:
// a slow-scrolling perspective grid under a sunset gradient (the scene's own
// backdrop, in miniature), with a title, an optional big join code, and an
// optional hint line.
//
// Everything is CPU: the gradient and grid are drawn straight into a top-down
// 32bpp DIB section, and GDI draws the text on top - which is the entire
// reason a DIB is used, because GDI is the only free text rasteriser this
// process has. ~1MP at 30fps is nothing.
#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace vkr {

class IdleCard {
  public:
    bool init(unsigned w, unsigned h, const std::wstring& title, const std::wstring& code,
              const std::wstring& hint) {
        w_ = w;
        h_ = h;
        title_ = title;
        code_ = code;
        hint_ = hint;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = (LONG)w;
        bi.bmiHeader.biHeight = -(LONG)h; // top-down, so bits_ rows match frames
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        dib_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, (void**)&bits_, nullptr, 0);
        if (!dc_ || !dib_ || !bits_) return false;
        SelectObject(dc_, dib_);

        titleFont_ = CreateFontW(-(int)(h / 14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        codeFont_ = CreateFontW(-(int)(h / 4), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        hintFont_ = CreateFontW(-(int)(h / 24), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        return true;
    }

    ~IdleCard() {
        if (titleFont_) DeleteObject(titleFont_);
        if (codeFont_) DeleteObject(codeFont_);
        if (hintFont_) DeleteObject(hintFont_);
        if (dib_) DeleteObject(dib_);
        if (dc_) DeleteDC(dc_);
    }

    // Renders the card for time t (seconds). The returned buffer is BGRA,
    // tightly packed (stride == w*4), valid until the next render call.
    const uint8_t* render(double t) {
        drawBackdrop(t);
        drawText();
        return bits_;
    }

    unsigned width() const { return w_; }
    unsigned height() const { return h_; }

  private:
    // Sunset gradient over a perspective grid scrolling toward the viewer -
    // obviously alive, unmistakably the same product as the scene.
    void drawBackdrop(double t) {
        const unsigned horizon = h_ * 55 / 100;
        for (unsigned y = 0; y < h_; ++y) {
            uint8_t r, g, b;
            if (y < horizon) {
                // Sky: deep violet at the top -> hot pink -> ember at the horizon.
                const float f = (float)y / (float)horizon;
                r = (uint8_t)(24 + f * (235 - 24));
                g = (uint8_t)(8 + f * (90 - 8));
                b = (uint8_t)(60 + f * (120 - 60));
            } else {
                // Floor: near-black violet.
                r = 16;
                g = 6;
                b = 32;
            }
            uint8_t* row = bits_ + (size_t)y * w_ * 4;
            for (unsigned x = 0; x < w_; ++x) {
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = 255;
            }
        }
        // Horizontal grid lines, exponentially spaced, scrolling toward the
        // viewer (the classic infinite-floor illusion: phase wraps once per
        // spacing period).
        const double phase = fmod(t * 0.55, 1.0);
        for (int i = 0; i < 14; ++i) {
            const double d = (double)i + 1.0 - phase; // distance in "grid units"
            if (d <= 0.05) continue;
            const unsigned y = horizon + (unsigned)((double)(h_ - horizon) / d);
            if (y >= h_) continue;
            const uint8_t glow = (uint8_t)(200.0 / d + 40);
            uint8_t* row = bits_ + (size_t)y * w_ * 4;
            for (unsigned x = 0; x < w_; ++x) {
                row[x * 4 + 0] = (uint8_t)((row[x * 4 + 0] + 255) / 2);
                row[x * 4 + 1] = (uint8_t)(row[x * 4 + 1] / 2 + glow / 4);
                row[x * 4 + 2] = (uint8_t)((row[x * 4 + 2] + glow) / 2);
            }
        }
        // Vertical lines converging on the centre of the horizon.
        const int cx = (int)w_ / 2;
        for (int i = -9; i <= 9; ++i) {
            if (i == 0) continue;
            const double bottomX = cx + i * (double)w_ / 9.0;
            for (unsigned y = horizon + 2; y < h_; ++y) {
                const double f = (double)(y - horizon) / (double)(h_ - horizon);
                const int x = cx + (int)((bottomX - cx) * f);
                if (x < 0 || x >= (int)w_) continue;
                uint8_t* p = bits_ + ((size_t)y * w_ + (unsigned)x) * 4;
                p[0] = (uint8_t)((p[0] + 255) / 2);
                p[2] = (uint8_t)((p[2] + 160) / 2);
            }
        }
        // A soft sun pulse sitting on the horizon.
        const int sunR = (int)(h_ / 5) + (int)(4.0 * sin(t * 1.8));
        const int sy = (int)horizon;
        for (int dy = -sunR; dy <= 0; ++dy) {
            const int y = sy + dy;
            if (y < 0) continue;
            const int half = (int)sqrt((double)(sunR * sunR - dy * dy));
            uint8_t* row = bits_ + (size_t)y * w_ * 4;
            for (int x = cx - half; x <= cx + half; ++x) {
                if (x < 0 || x >= (int)w_) continue;
                uint8_t* p = row + (size_t)x * 4;
                p[0] = (uint8_t)((p[0] + 90) / 2);
                p[1] = (uint8_t)((p[1] + 180) / 2);
                p[2] = (uint8_t)((p[2] + 255) / 2);
            }
        }
    }

    void drawText() {
        SetBkMode(dc_, TRANSPARENT);
        RECT rc{0, (LONG)(h_ / 10), (LONG)w_, (LONG)(h_ / 10 + h_ / 8)};
        SetTextColor(dc_, RGB(255, 255, 255));
        SelectObject(dc_, titleFont_);
        DrawTextW(dc_, title_.c_str(), -1, &rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (!code_.empty()) {
            RECT rcode{0, (LONG)(h_ * 30 / 100), (LONG)w_, (LONG)(h_ * 62 / 100)};
            // Shadow first, so the digits hold up over the sun.
            SetTextColor(dc_, RGB(30, 0, 40));
            OffsetRect(&rcode, 4, 4);
            SelectObject(dc_, codeFont_);
            DrawTextW(dc_, code_.c_str(), -1, &rcode, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            OffsetRect(&rcode, -4, -4);
            SetTextColor(dc_, RGB(255, 255, 255));
            DrawTextW(dc_, code_.c_str(), -1, &rcode, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        if (!hint_.empty()) {
            RECT rh{0, (LONG)(h_ * 66 / 100), (LONG)w_, (LONG)(h_ * 74 / 100)};
            SetTextColor(dc_, RGB(230, 210, 255));
            SelectObject(dc_, hintFont_);
            DrawTextW(dc_, hint_.c_str(), -1, &rh, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        GdiFlush(); // GDI writes land in bits_ before the caller copies them
    }

    unsigned w_ = 0, h_ = 0;
    std::wstring title_, code_, hint_;
    HDC dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    uint8_t* bits_ = nullptr;
    HFONT titleFont_ = nullptr, codeFont_ = nullptr, hintFont_ = nullptr;
};

} // namespace vkr

#endif // _WIN32
