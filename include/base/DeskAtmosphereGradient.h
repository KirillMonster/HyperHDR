#pragma once

#include <base/DominantDarkSceneGate.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hyperhdr {

// One full-screen capture -> 3 independent atmospheric palettes -> 178 LEDs.
// Physical order (looking at the desk):
//  0..32   left, rear -> front
//  33..88  front, left -> right
//  89..121 right, front -> rear
//  122..177 rear, right -> left
// The 4 physical strips have gaps at the corners; no virtual pixels are added.
class DeskAtmosphereGradient {
public:
    struct Rgb { std::uint8_t r=0, g=0, b=0; };
    static constexpr std::size_t kLedCount = 178;
    using Output = std::array<Rgb, kLedCount>;

    Output render(const std::uint8_t* rgb, unsigned width, unsigned height,
                  unsigned cropX, unsigned cropY, unsigned cropWidth, unsigned cropHeight,
                  std::int64_t nowMs)
    {
        if (rgb == nullptr || width == 0 || height == 0) return {};
        cropX = std::min(cropX, width);
        cropY = std::min(cropY, height);
        cropWidth = std::min(cropWidth, width - cropX);
        cropHeight = std::min(cropHeight, height - cropY);
        if (cropWidth == 0 || cropHeight == 0) return {};

        std::array<ZoneFrame, 3> frames{};
        double framePeak = 0.0;
        double brightPixels = 0.0;
        double sampleCount = 0.0;
        // Small fixed stride: no per-LED image rescanning.
        for (unsigned y = cropY; y < cropY + cropHeight; y += 4) {
            const double ny = (static_cast<double>(y - cropY) + .5) / cropHeight;
            for (unsigned x = cropX; x < cropX + cropWidth; x += 4) {
                const double nx = (static_cast<double>(x - cropX) + .5) / cropWidth;
                const auto off = (static_cast<std::size_t>(y) * width + x) * 3;
                const Rgb c{rgb[off], rgb[off + 1], rgb[off + 2]};
                const int peak = std::max({int(c.r), int(c.g), int(c.b)});
                const int low = std::min({int(c.r), int(c.g), int(c.b)});
                framePeak += peak;
                brightPixels += peak >= 20 ? 1.0 : 0.0;
                sampleCount += 1.0;
                const bool colored = peak >= 32 && (peak - low) >= 16 &&
                    static_cast<double>(peak - low) / std::max(1, peak) >= .12;
                double hue = 0.0, weight = 0.0;
                if (colored) {
                    hue = pixelHue(c, peak, peak - low);
                    const double saturation = static_cast<double>(peak - low) / peak;
                    weight = std::sqrt(static_cast<double>(peak) / 255.0) *
                             (.55 + .45 * std::sqrt(saturation));
                }
                if (nx < .40) accumulate(frames[0], c, colored, hue, weight, ny);
                if (nx >= .25 && nx < .75)
                    accumulate(frames[1], c, colored, hue, weight,
                               std::clamp((nx - .25) / .50, 0.0, 1.0));
                if (nx >= .60) accumulate(frames[2], c, colored, hue, weight, ny);
            }
        }

        const auto decision = _darkGate.update(framePeak / std::max(1.0, sampleCount),
                                              brightPixels / std::max(1.0, sampleCount), nowMs);
        if (decision == DominantDarkSceneGate::Decision::HoldPrevious)
            return _hasPrevious ? _previous : Output{};
        if (decision == DominantDarkSceneGate::Decision::Dark) {
            clearHistory();
            _previous = {};
            _hasPrevious = true;
            return _previous;
        }
        if (decision == DominantDarkSceneGate::Decision::Resumed) clearHistory();

        std::array<Rgb, 3> base{};
        for (int z = 0; z < 3; ++z) base[z] = selectDominant(frames[z], _history[z], nowMs);

        Output out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            int zone = 0;
            double t = 0.0;
            if (i < 33) { zone = 0; t = static_cast<double>(i) / 32.0; }
            else if (i < 89) { zone = 1; t = static_cast<double>(i - 33) / 55.0; }
            else if (i < 122) { zone = 2; t = static_cast<double>(121 - i) / 32.0; }
            else { zone = 1; t = static_cast<double>(177 - i) / 55.0; }
            out[i] = makeGradient(base[zone], frames[zone], t);
        }
        _previous = out;
        _hasPrevious = true;
        return out;
    }

private:
    static constexpr int kHueBins = 24;
    static constexpr int kLocalBins = 32;
    struct Accum {
        double weight=0.0, r=0.0, g=0.0, b=0.0;
        void add(Rgb color, double k) {
            weight += k; r += color.r * k; g += color.g * k; b += color.b * k;
        }
    };
    struct ZoneFrame {
        std::array<Accum, kHueBins> hues{};
        std::array<Accum, kLocalBins> local{};
        double pixels=0.0, chromatic=0.0;
        Accum mean{};
    };
    struct History {
        std::array<double, kHueBins> scores{};
        int selected=-1;
        std::int64_t updatedMs=-1;
    };
    struct Hsv { double h=0.0, s=0.0, v=0.0; };

    std::array<History, 3> _history{};
    DominantDarkSceneGate _darkGate{};
    Output _previous{};
    bool _hasPrevious=false;

    void clearHistory() noexcept { _history = {}; }
    static int wrapBin(int i) noexcept { return (i % kHueBins + kHueBins) % kHueBins; }
    static double pixelHue(Rgb c, int peak, int d) noexcept {
        if (d == 0) return 0.0;
        double hue = 0.0;
        if (peak == c.r) hue = 60.0 * (int(c.g) - int(c.b)) / d;
        else if (peak == c.g) hue = 60.0 * ((int(c.b) - int(c.r)) / static_cast<double>(d) + 2.0);
        else hue = 60.0 * ((int(c.r) - int(c.g)) / static_cast<double>(d) + 4.0);
        return hue < 0.0 ? hue + 360.0 : hue;
    }
    static void accumulate(ZoneFrame& zone, Rgb c, bool colored, double hue,
                           double weight, double localT)
    {
        zone.mean.add(c, 1.0);
        zone.pixels += 1.0;
        if (!colored) return;
        zone.chromatic += 1.0;
        // Place samples across circular bin centers, not sharp hue edges.
        const double p = hue / (360.0 / kHueBins) - .5;
        const int lo = static_cast<int>(std::floor(p));
        const double hiWeight = p - lo;
        zone.hues[wrapBin(lo)].add(c, weight * (1.0 - hiWeight));
        zone.hues[wrapBin(lo + 1)].add(c, weight * hiWeight);
        const int bin = std::clamp(static_cast<int>(localT * kLocalBins), 0, kLocalBins - 1);
        zone.local[bin].add(c, weight);
    }
    static Rgb asRgb(const Accum& a) noexcept {
        if (a.weight <= 1e-9) return {};
        const auto b = [](double v) {
            return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
        };
        return {b(a.r / a.weight), b(a.g / a.weight), b(a.b / a.weight)};
    }
    static Rgb selectDominant(const ZoneFrame& frame, History& history, std::int64_t nowMs)
    {
        if (frame.pixels < 1 || frame.chromatic < 6 || frame.chromatic / frame.pixels < .08) {
            history = {};
            return asRgb(frame.mean);
        }
        std::array<double, kHueBins> score{};
        double total = 0.0;
        for (int i = 0; i < kHueBins; ++i) {
            auto vote = [&](int d) { return frame.hues[wrapBin(i + d)].weight; };
            score[i] = vote(0) + .60 * (vote(-1) + vote(1)) +
                .20 * (vote(-2) + vote(2));
            total += score[i];
        }
        if (total <= 1e-9) { history = {}; return asRgb(frame.mean); }
        for (double& s : score) s /= total;

        double alpha = 1.0;
        if (history.updatedMs >= 0 && nowMs >= history.updatedMs) {
            const double elapsed = std::clamp(static_cast<double>(nowMs - history.updatedMs), 1.0, 2000.0);
            alpha = 1.0 - std::exp(-elapsed / 140.0);
            double shift = 0.0;
            for (int i = 0; i < kHueBins; ++i)
                shift += std::abs(score[i] - history.scores[i]);
            if (shift > 1.30) alpha = std::max(alpha, .85);
        }
        history.updatedMs = nowMs;
        for (int i = 0; i < kHueBins; ++i)
            history.scores[i] += alpha * (score[i] - history.scores[i]);
        int winner = -1;
        for (int i = 0; i < kHueBins; ++i) {
            if (frame.hues[i].weight <= 1e-9) continue;
            if (winner < 0 || history.scores[i] > history.scores[winner]) winner = i;
        }
        if (winner < 0) return asRgb(frame.mean);
        if (history.selected >= 0 && history.selected != winner &&
            frame.hues[history.selected].weight > 1e-9) {
            const int d = std::abs(history.selected - winner);
            if (std::min(d, kHueBins - d) > 1 &&
                history.scores[winner] < history.scores[history.selected] * 1.15)
                winner = history.selected;
        }
        history.selected = winner;
        Accum main{};
        for (int d = -2; d <= 2; ++d) {
            const Accum& a = frame.hues[wrapBin(winner + d)];
            const double k = (d == 0) ? 1.0 : (std::abs(d) == 1 ? .60 : .20);
            main.weight += k * a.weight;
            main.r += k * a.r;
            main.g += k * a.g;
            main.b += k * a.b;
        }
        return asRgb(main);
    }
    static Hsv rgbToHsv(Rgb rgb) noexcept {
        const double r = rgb.r / 255.0, g = rgb.g / 255.0, b = rgb.b / 255.0;
        const double mx = std::max({r,g,b}), mn=std::min({r,g,b});
        const double d = mx - mn;
        Hsv out{0.0, mx > 0 ? d / mx : 0.0, mx};
        if (d <= 1e-10) return out;
        if (mx == r) out.h = 60.0 * ((g-b)/d);
        else if (mx == g) out.h = 60.0 * ((b-r)/d+2);
        else out.h = 60.0 * ((r-g)/d+4);
        if (out.h < 0) out.h += 360.0;
        return out;
    }
    static Rgb hsvToRgb(Hsv hsv) noexcept {
        hsv.h = std::fmod(hsv.h + 720.0, 360.0);
        hsv.s = std::clamp(hsv.s, 0.0, 1.0);
        hsv.v = std::clamp(hsv.v, 0.0, 1.0);
        const double c = hsv.v * hsv.s;
        const double h = hsv.h / 60.0;
        const double x = c * (1 - std::abs(std::fmod(h, 2.0) - 1));
        double r=0,g=0,b=0;
        if (h < 1) {r=c;g=x;}
        else if(h < 2) {r=x;g=c;}
        else if(h < 3) {g=c;b=x;}
        else if(h < 4) {g=x;b=c;}
        else if(h < 5) {r=x;b=c;}
        else {r=c;b=x;}
        const double m = hsv.v - c;
        const auto v = [m](double z) {
            return static_cast<std::uint8_t>(std::lround(std::clamp((z+m)*255.0, 0.0, 255.0)));
        };
        return {v(r),v(g),v(b)};
    }
    static Rgb makeGradient(Rgb base, const ZoneFrame& frame, double t) noexcept {
        if (base.r == 0 && base.g == 0 && base.b == 0) return base;
        const int center = std::clamp(static_cast<int>(std::lround(t * (kLocalBins - 1))), 0, kLocalBins - 1);
        Accum local{};
        // Seven adjacent buckets yield a smooth gradient with no LED-wide jumps.
        for (int d = -3; d <= 3; ++d) {
            const int idx = center + d;
            if (idx < 0 || idx >= kLocalBins) continue;
            const auto& a = frame.local[idx];
            const double k = 4.0 - std::abs(d);
            local.weight += k * a.weight;
            local.r += k * a.r;
            local.g += k * a.g;
            local.b += k * a.b;
        }
        if (local.weight < 1e-7) return base;
        const Hsv b = rgbToHsv(base);
        const Hsv l = rgbToHsv(asRgb(local));
        if (b.s < .13 || b.v < .06 || l.s < .15) return base;
        double alpha = .24 * std::clamp((l.s - .12) / .45, 0.0, 1.0);
        const double dh = std::fmod(l.h - b.h + 540.0, 360.0) - 180.0;
        // Opposite hues can otherwise create visually unrelated muddy mixtures.
        if (std::abs(dh) > 100.0) alpha *= .65;
        const double h = b.h + std::clamp(dh, -95.0, 95.0) * alpha;
        const double s = b.s * (1.0 - .65 * alpha) + l.s * (.65 * alpha);
        const double v = b.v * (1.0 - .50 * alpha) + l.v * (.50 * alpha);
        return hsvToRgb({h, s, v});
    }
};

} // namespace hyperhdr
