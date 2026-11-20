#pragma once

#include <cstdint>

namespace hyperhdr {

// Detect near-black frames without repeatedly turning the entire room on/off
// when a tiny player overlay, cursor, or compression noise appears.
class DominantDarkSceneGate {
public:
    enum class Decision { Normal, HoldPrevious, Dark, Resumed };

    Decision update(double meanPeak, double brightAreaRatio, std::int64_t nowMs) noexcept
    {
        // meanPeak is the mean of max(R,G,B) on the sampled 8-bit frame.
        // brightAreaRatio is the fraction of samples with max(R,G,B) >= 20.
        const bool nearBlack = meanPeak < 12.0 && brightAreaRatio < 0.075;
        const bool unmistakableNewScene = meanPeak >= 48.0 || brightAreaRatio >= 0.30;

        if (!_dark) {
            _recoverSinceMs = -1;
            if (!nearBlack) {
                _nearBlackSinceMs = -1;
                return Decision::Normal;
            }
            if (_nearBlackSinceMs < 0 || nowMs < _nearBlackSinceMs)
                _nearBlackSinceMs = nowMs;
            if (nowMs - _nearBlackSinceMs < 350)
                return Decision::HoldPrevious;
            _dark = true;
            return Decision::Dark;
        }

        if (nearBlack) {
            _recoverSinceMs = -1;
            return Decision::Dark;
        }
        if (unmistakableNewScene) {
            reset();
            return Decision::Resumed;
        }
        if (_recoverSinceMs < 0 || nowMs < _recoverSinceMs)
            _recoverSinceMs = nowMs;
        if (nowMs - _recoverSinceMs < 240)
            return Decision::Dark;
        reset();
        return Decision::Resumed;
    }

    void reset() noexcept
    {
        _dark = false;
        _nearBlackSinceMs = -1;
        _recoverSinceMs = -1;
    }

private:
    bool _dark = false;
    std::int64_t _nearBlackSinceMs = -1;
    std::int64_t _recoverSinceMs = -1;
};

} // namespace hyperhdr
