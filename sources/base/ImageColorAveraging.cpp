/* ImageColorAveraging.cpp
*
*  MIT License
*
*  Copyright (c) 2020-2026 awawa-dev
*
*  Project homesite: https://github.com/awawa-dev/HyperHDR
*
*  Permission is hereby granted, free of charge, to any person obtaining a copy
*  of this software and associated documentation files (the "Software"), to deal
*  in the Software without restriction, including without limitation the rights
*  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*  copies of the Software, and to permit persons to whom the Software is
*  furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in all
*  copies or substantial portions of the Software.

*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*  SOFTWARE.
*/

#include <base/ImageColorAveraging.h>
#include <base/ImageToLedManager.h>
#include <infinite-color-engine/InfiniteProcessing.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <ranges>
#include <iterator>

#define push_back_index(list, index) list.push_back((index) * 3)

using namespace hyperhdr;
using namespace linalg::aliases;

ImageColorAveraging::ImageColorAveraging(
				const LoggerName& _log,
				const int mappingType,
				const bool sparseProcessing,
				const unsigned width,
				const unsigned height,
				const unsigned horizontalBorder,
				const unsigned verticalBorder,
				const quint8 /*instanceIndex*/,
				const std::vector<LedString::Led>& leds)
	: _width(width)
	, _height(height)
	, _sparseProcessing(sparseProcessing)
	, _horizontalBorder(horizontalBorder)
	, _verticalBorder(verticalBorder)
	, _colorsMap()
	, _colorGroups()
{
	Q_ASSERT(_width > 2 * _verticalBorder);
	Q_ASSERT(_height > 2 * _horizontalBorder);
	Q_ASSERT(_width < 10000);
	Q_ASSERT(_height < 10000);

	_mappingType = mappingType;

	_colorsMap.reserve(leds.size());

	const int32_t xOffset = _verticalBorder;
	const int32_t actualWidth = _width - 2 * _verticalBorder;
	const int32_t yOffset = _horizontalBorder;
	const int32_t actualHeight = _height - 2 * _horizontalBorder;

	size_t   totalCount = 0;
	size_t   totalCapasity = 0;

	for (int ledIndex = -1; const LedString::Led& led : leds)
	{
		ledIndex++;

		if ((led.maxX_frac - led.minX_frac) < 1e-6 || (led.maxY_frac - led.minY_frac) < 1e-6)
		{
			_colorsMap.emplace_back();
			continue;
		}

		int32_t minX_idx = xOffset + int32_t(qRound(actualWidth * led.minX_frac));
		int32_t maxX_idx = xOffset + int32_t(qRound(actualWidth * led.maxX_frac));
		int32_t minY_idx = yOffset + int32_t(qRound(actualHeight * led.minY_frac));
		int32_t maxY_idx = yOffset + int32_t(qRound(actualHeight * led.maxY_frac));

		minX_idx = qMin(minX_idx, xOffset + actualWidth - 1);
		if (minX_idx == maxX_idx)
		{
			maxX_idx++;
		}
		minY_idx = qMin(minY_idx, yOffset + actualHeight - 1);
		if (minY_idx == maxY_idx)
		{
			maxY_idx++;
		}

		const int32_t maxYLedCount = qMin(maxY_idx, yOffset + actualHeight);
		const int32_t maxXLedCount = qMin(maxX_idx, xOffset + actualWidth);
		const int32_t realYLedCount = qAbs(maxYLedCount - minY_idx);
		const int32_t realXLedCount = qAbs(maxXLedCount - minX_idx);

		bool   sparseIndexes = sparseProcessing;
		size_t totalSize = static_cast<size_t>(realYLedCount) * realXLedCount;

		if (!sparseIndexes && totalSize > 1600)
		{
			Warning(_log, "This is large image area for lamp: {:d}. It contains {:d} indexes for captured video frame so reduce it by four. Enabling 'sparse processing' option for you. Consider to enable it permanently in the processing configuration to hide that warning.", ledIndex, totalSize);
			sparseIndexes = true;
		}

		const int32_t increment = (sparseIndexes) ? 2 : 1;

		totalCount += _colorsMap.emplace_back([&]{
			std::vector<uint32_t> ledColor;
			if (!led.disabled)
			{
				ledColor.reserve((sparseIndexes) ? ((static_cast<unsigned long long>(realYLedCount / 2)) + (realYLedCount % 2)) * ((realXLedCount / 2) + (realXLedCount % 2)) : totalSize);

				for (int32_t y = minY_idx; y < maxYLedCount; y += increment)
				{
					for (int32_t x = minX_idx; x < maxXLedCount; x += increment)
					{
						push_back_index(ledColor, y * width + x);
					}
				}
			}
			return ledColor;
		}()).size();
		totalCapasity += _colorsMap.back().capacity();

		if (led.group > 0)
		{
			if (_colorGroups.contains(led.group))
			{
				int master = _colorGroups[led.group].front();
				auto& dest_vec = _colorsMap[master];
				auto& source_vec = _colorsMap.back();

				dest_vec.insert(
					dest_vec.end(),
					std::make_move_iterator(source_vec.begin()),
					std::make_move_iterator(source_vec.end())
				);

				source_vec.clear();
			}
			_colorGroups[led.group].push_back(ledIndex);
		}
	}
	Info(_log, "Total index number is: {:d} (memory: {:d}). User sparse processing is: {:s}, image size: {:d} x {:d}, area number: {:d}",
		totalCount, totalCapasity, (sparseProcessing) ? "enabled" : "disabled", width, height, leds.size());
}

unsigned ImageColorAveraging::width() const
{
	return _width;
}

unsigned ImageColorAveraging::height() const
{
	return _height;
}

unsigned ImageColorAveraging::horizontalBorder() const
{
	return _horizontalBorder;
}

unsigned ImageColorAveraging::verticalBorder() const {
	return _verticalBorder;
}

void ImageColorAveraging::process(std::vector<float3>& ledColors, const Image<ColorRgb>& image)
{
	ledColors.clear();
	ledColors.reserve(_colorsMap.size());

	switch (_mappingType)
	{
		case 1: getUnicolorForLeds(ledColors, image); break;
		case 2: getUnicolorExperimental(ledColors, image, false); break;
		case 3: getUnicolorExperimental(ledColors, image, true); break;
		default: getMulticolorForLeds(ledColors, image);
	}

	if (!_colorGroups.empty() && _mappingType != 1)
	{
		for (auto& group : _colorGroups)
		{
			const auto& combined = ledColors[group.second.front()];
			for (int g = 1; g < static_cast<int>(group.second.size()); g++)
			{
				ledColors[group.second[g]] = combined;
			}
		}
	}
}

void  ImageColorAveraging::getUnicolorForLeds(std::vector<float3>& ledColors, const Image<ColorRgb>& image) const
{
	ledColors.resize(_colorsMap.size(), calcUnicolorForLeds(image));
}


void ImageColorAveraging::getMulticolorForLeds(std::vector<float3>& ledColors, const Image<ColorRgb>& image) const
{
	for (auto colors = _colorsMap.begin(); colors != _colorsMap.end(); ++colors)
	{
		ledColors.push_back(calcMulticolorForLeds(image, *colors));
	}
}

float3 ImageColorAveraging::calcMulticolorForLeds(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors) const
{
	if (colors.empty())
	{
		return float3{ 0, 0, 0 };
	}

	linalg::vec<uint_fast64_t, 3> sumLinear(0, 0, 0);

	const uint8_t* imgData = image.rawMem();

	for (const uint32_t colorOffset : colors)
	{		
		sumLinear += InfiniteProcessing::srgbNonlinearToLinear(byte3(imgData[colorOffset], imgData[colorOffset + 1], imgData[colorOffset + 2]));
	}

	auto averageLinear = (static_cast<float3>(sumLinear) / static_cast<float>(colors.size())) / 65535.0f;

	return averageLinear;
}

float3 ImageColorAveraging::calcUnicolorForLeds(const Image<ColorRgb>& image) const
{
	uint_fast64_t sum = 0;
	linalg::vec<uint_fast64_t, 3> sumLinear(0, 0, 0);

	const uint8_t* imgData = image.rawMem();
	const uint32_t rowSize = image.width() * static_cast<uint32_t>(sizeof(ColorRgb));
	const uint32_t increment = (_sparseProcessing) ? 2 : 1;

	for (uint32_t y = 0; y < image.height(); y += increment)
	{
		for (uint32_t colorOffset = y * rowSize; colorOffset < y * rowSize + rowSize; colorOffset += increment * static_cast<uint32_t>(sizeof(ColorRgb)))
		{
			sumLinear += InfiniteProcessing::srgbNonlinearToLinear(byte3(imgData[colorOffset], imgData[colorOffset + 1], imgData[colorOffset + 2]));
			sum++;
		}
	}

	auto averageLinear = (static_cast<float3>(sumLinear) / static_cast<float>(sum)) / 65535.0f;

	return averageLinear;
}


void ImageColorAveraging::getUnicolorExperimental(std::vector<float3>& ledColors, const Image<ColorRgb>& image, bool adaptive)
{
    // Custom v3: temporally stable multi-modal hue selection.
    // Dominant uses a brightness/position-weighted circular hue histogram,
    // and adds a small amount of a genuine, distinct second color family.
    // This is deliberately not object segmentation or motion tracking.
    // Adaptive Accent keeps its original scoring behavior.
    constexpr int kBins = 24;
    struct Cluster {
        double pixels = 0.0;
        double centralWeight = 0.0;
        double borderPixels = 0.0;
        double red = 0.0, green = 0.0, blue = 0.0;
        double weightedVotes = 0.0;
        double weightedRed = 0.0, weightedGreen = 0.0, weightedBlue = 0.0;
    };
    std::array<Cluster, kBins> clusters{};
    std::array<double, kBins> backgroundVotes{};
    const uint8_t* pixels = image.rawMem();
    const unsigned width = image.width();
    const unsigned height = image.height();
    const unsigned x0 = std::min(_verticalBorder, width);
    const unsigned y0 = std::min(_horizontalBorder, height);
    const unsigned x1 = (width > _verticalBorder) ? width - _verticalBorder : width;
    const unsigned y1 = (height > _horizontalBorder) ? height - _horizontalBorder : height;

    if (x0 >= x1 || y0 >= y1) {
        ledColors.assign(_colorsMap.size(), calcUnicolorForLeds(image));
        return;
    }

    const double scaleX = 1.0 / std::max(1u, x1 - x0);
    const double scaleY = 1.0 / std::max(1u, y1 - y0);
    double samples = 0.0;
    double sumPeak = 0.0;
    double brightSamples = 0.0;
    for (unsigned y = y0; y < y1; y += 4) {
        for (unsigned x = x0; x < x1; x += 4) {
            const size_t off = (static_cast<size_t>(y) * width + x) * 3;
            const int r = pixels[off], g = pixels[off + 1], b = pixels[off + 2];
            const int maximum = std::max({r, g, b});
            const int minimum = std::min({r, g, b});
            const int delta = maximum - minimum;
            samples += 1.0;
            sumPeak += maximum;
            if (maximum >= 20) brightSamples += 1.0;
            if (maximum == 0) continue;
            const double saturation = static_cast<double>(delta) / maximum;
            // Keep adaptive's old thresholds; dominant should also see bright,
            // moderately saturated beige/yellow instead of only dark vivid blues.
            if (adaptive) {
                if (maximum < 36 || delta < 28 || saturation < 0.20) continue;
            } else if (maximum < 32 || delta < 16 || saturation < 0.12) {
                continue;
            }

            double hue;
            if (maximum == r) hue = 60.0 * (g - b) / delta;
            else if (maximum == g) hue = 60.0 * ((b - r) / static_cast<double>(delta) + 2.0);
            else hue = 60.0 * ((r - g) / static_cast<double>(delta) + 4.0);
            if (hue < 0.0) hue += 360.0;
            const double nx = ((static_cast<double>(x - x0) + 0.5) * scaleX - 0.5) * 2.0;
            const double ny = ((static_cast<double>(y - y0) + 0.5) * scaleY - 0.5) * 2.0;
            const bool atBorder = std::abs(nx) > 0.70 || std::abs(ny) > 0.70;
            const double centerGaussian = std::exp(-3.5 * (nx * nx + ny * ny));
            const double centralWeight = 1.0 + 4.0 * centerGaussian;

            const auto addHueSample = [&](int bin, double fraction) {
                auto& c = clusters[bin];
                c.pixels += fraction;
                c.centralWeight += centralWeight * fraction;
                c.borderPixels += atBorder ? fraction : 0.0;
                c.red += r * fraction;
                c.green += g * fraction;
                c.blue += b * fraction;
                if (atBorder) backgroundVotes[bin] += fraction;
                if (!adaptive) {
                    const double brightness = std::sqrt(static_cast<double>(maximum) / 255.0);
                    const double colorfulness = 0.55 + 0.45 * std::sqrt(saturation);
                    const double position = 0.85 + 0.45 * centerGaussian;
                    const double weight = brightness * colorfulness * position * fraction;
                    c.weightedVotes += weight;
                    c.weightedRed += r * weight;
                    c.weightedGreen += g * weight;
                    c.weightedBlue += b * weight;
                }
            };

            if (adaptive) {
                // Preserve Adaptive Accent's original discrete histogram.
                addHueSample(std::clamp(static_cast<int>(hue * kBins / 360.0), 0, kBins - 1), 1.0);
            } else {
                // Circular bilinear histogram: avoid 15-degree hue-boundary jumps.
                // Bin centers are at 7.5, 22.5, ..., 352.5 degrees.
                const double position = hue * kBins / 360.0 - 0.5;
                const int lower = static_cast<int>(std::floor(position));
                const double upperFraction = position - lower;
                const int first = (lower + kBins) % kBins;
                addHueSample(first, 1.0 - upperFraction);
                addHueSample((first + 1) % kBins, upperFraction);
            }
        }
    }

    double chromatic = 0.0;
    for (const auto& c : clusters) chromatic += c.pixels;

    const auto resetHueHistory = [&] {
        _lastHueBin = -1;
        _pendingHueBin = -1;
        _pendingHueFrames = 0;
        _dominantScores.fill(0.0);
        _lastDominantMs = 0;
    };

    if (!adaptive) {
        // Darkness is a scene state, not a hue. Stable transitions avoid
        // repeated brightness=0 / brightness=255 calls to Home Assistant
        // when a cursor or playback overlay appears on a black frame.
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        const auto decision = _dominantDarkGate.update(
            sumPeak / std::max(1.0, samples),
            brightSamples / std::max(1.0, samples), nowMs);

        if (decision == DominantDarkSceneGate::Decision::Dark) {
            resetHueHistory();
            _lastDominantOutput = float3{0.f, 0.f, 0.f};
            _hasLastDominantOutput = true;
            ledColors.assign(_colorsMap.size(), _lastDominantOutput);
            return;
        }
        if (decision == DominantDarkSceneGate::Decision::HoldPrevious) {
            const float3 held = _hasLastDominantOutput ?
                _lastDominantOutput : float3{0.f, 0.f, 0.f};
            ledColors.assign(_colorsMap.size(), held);
            return;
        }
        if (decision == DominantDarkSceneGate::Decision::Resumed)
            resetHueHistory();
    }

    if (chromatic < 6.0 || chromatic / std::max(1.0, samples) < 0.10) {
        resetHueHistory();
        const float3 average = calcUnicolorForLeds(image);
        if (!adaptive) {
            _lastDominantOutput = average;
            _hasLastDominantOutput = true;
        }
        ledColors.assign(_colorsMap.size(), average);
        return;
    }

    int background = 0;
    for (int i = 1; i < kBins; ++i)
        if (backgroundVotes[i] > backgroundVotes[background]) background = i;

    std::array<double, kBins> scores{};
    for (int i = 0; i < kBins; ++i) {
        const auto& c = clusters[i];
        if (c.pixels == 0.0) continue;
        if (adaptive) {
            const int d = std::abs(i - background);
            const double hueContrast = std::min(d, kBins - d) / 12.0;
            const double edgeShare = c.borderPixels / c.pixels;
            scores[i] = c.centralWeight * (1.0 + 1.8 * hueContrast) * (1.0 - 0.40 * edgeShare);
            if (c.pixels < 0.055 * chromatic) scores[i] *= 0.10;
        } else {
            // Circular hue density: a family cannot split at 0/360 or bin edges.
            const auto vote = [&](int offset) {
                return clusters[(i + kBins + offset) % kBins].weightedVotes;
            };
            scores[i] = vote(0) + 0.60 * (vote(-1) + vote(1)) +
                        0.20 * (vote(-2) + vote(2));
        }
    }

    if (!adaptive) {
        // Normalize before applying temporal smoothing: the hue distribution
        // must remain comparable when the whole scene gets darker/brighter.
        double totalScore = 0.0;
        for (double score : scores) totalScore += score;
        if (totalScore > 0.0)
            for (double& score : scores) score /= totalScore;

        // Smooth candidate scores, not the output RGB (HyperHDR does that).
        // Strong distribution shifts are likely scene cuts, so react faster.
        const auto timePoint = std::chrono::steady_clock::now().time_since_epoch();
        const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint).count();
        double alpha = 1.0;
        if (_lastDominantMs > 0 && nowMs >= _lastDominantMs) {
            const double elapsed = std::clamp(static_cast<double>(nowMs - _lastDominantMs), 1.0, 2000.0);
            alpha = 1.0 - std::exp(-elapsed / 140.0);
            double distributionShift = 0.0;
            for (int i = 0; i < kBins; ++i)
                distributionShift += std::abs(scores[i] - _dominantScores[i]);
            // Total variation > 0.65: the dominant colors changed abruptly.
            if (distributionShift > 1.30) alpha = std::max(alpha, 0.85);
        }
        _lastDominantMs = nowMs;
        for (int i = 0; i < kBins; ++i) {
            _dominantScores[i] += alpha * (scores[i] - _dominantScores[i]);
            scores[i] = _dominantScores[i];
        }
    } else {
        _dominantScores.fill(0.0);
        _lastDominantMs = 0;
    }

    // A smoothed peak can outlive its actual pixels. Never select a hue
    // that is absent from the current image just because it won previously.
    int winner = -1;
    for (int i = 0; i < kBins; ++i) {
        if (clusters[i].pixels <= 1e-9) continue;
        if (winner < 0 || scores[i] > scores[winner]) winner = i;
    }
    if (winner < 0) {
        const float3 average = calcUnicolorForLeds(image);
        if (!adaptive) {
            _lastDominantOutput = average;
            _hasLastDominantOutput = true;
        }
        ledColors.assign(_colorsMap.size(), average);
        return;
    }

    const auto distanceBins = [&](int a, int b) {
        const int d = std::abs(a - b);
        return std::min(d, kBins - d);
    };

    if (_lastHueBin >= 0 && _lastHueBin != winner && clusters[_lastHueBin].pixels > 1e-9) {
        // Adjacent bin changes are the same broad color family, not a scene cut.
        if (!adaptive && distanceBins(_lastHueBin, winner) <= 1) {
            _pendingHueBin = -1;
            _pendingHueFrames = 0;
        } else {
            const double margin = adaptive ? 1.22 : 1.15;
            if (scores[winner] < scores[_lastHueBin] * margin) {
                winner = _lastHueBin;
                _pendingHueBin = -1;
                _pendingHueFrames = 0;
            } else {
                if (_pendingHueBin != winner) {
                    _pendingHueBin = winner;
                    _pendingHueFrames = 1;
                } else ++_pendingHueFrames;
                const int requiredFrames = (!adaptive && scores[winner] > scores[_lastHueBin] * 1.90) ? 1 :
                                           (adaptive ? 2 : 3);
                if (_pendingHueFrames < requiredFrames) winner = _lastHueBin;
            }
        }
    }
    if (winner != _lastHueBin) {
        _pendingHueBin = -1;
        _pendingHueFrames = 0;
    }
    _lastHueBin = winner;

    const auto& selected = clusters[winner];
    if (selected.pixels == 0.0) {
        const float3 average = calcUnicolorForLeds(image);
        if (!adaptive) {
            _lastDominantOutput = average;
            _hasLastDominantOutput = true;
        }
        ledColors.assign(_colorsMap.size(), average);
        return;
    }

    double mixCount = adaptive ? selected.pixels : selected.weightedVotes;
    double mixR = adaptive ? selected.red : selected.weightedRed;
    double mixG = adaptive ? selected.green : selected.weightedGreen;
    double mixB = adaptive ? selected.blue : selected.weightedBlue;
    if (!adaptive) {
        // Main family: blend neighboring bins with exactly the same weights as voting.
        for (int d : {-2, -1, 1, 2}) {
            const auto& neighbor = clusters[(winner + kBins + d) % kBins];
            const double k = std::abs(d) == 1 ? 0.60 : 0.20;
            mixCount += k * neighbor.weightedVotes;
            mixR += k * neighbor.weightedRed;
            mixG += k * neighbor.weightedGreen;
            mixB += k * neighbor.weightedBlue;
        }
    }
    double outR = mixR / std::max(1e-9, mixCount);
    double outG = mixG / std::max(1e-9, mixCount);
    double outB = mixB / std::max(1e-9, mixCount);

    if (!adaptive) {
        // Find a second *independent* family, not another peak of the same family.
        int second = -1;
        for (int i = 0; i < kBins; ++i) {
            if (clusters[i].pixels == 0.0 || distanceBins(winner, i) < 4) continue;
            if (second < 0 || scores[i] > scores[second]) second = i;
        }
        if (second >= 0 && scores[winner] > 0.0) {
            const double ratio = std::clamp(scores[second] / scores[winner], 0.0, 1.0);
            const double angularDistance = 15.0 * distanceBins(winner, second);
            // Close color families can contribute ~18%. Opposite hues only ~4.5%
            // so blue+orange does not turn into an unrelated grayish mixture.
            const double distanceFactor = (angularDistance <= 75.0) ? 1.0 :
                (angularDistance >= 120.0) ? 0.25 :
                1.0 - 0.75 * (angularDistance - 75.0) / 45.0;
            const double secondFraction = 0.18 *
                std::clamp((ratio - 0.40) / 0.60, 0.0, 1.0) * distanceFactor;
            if (secondFraction > 0.0) {
                double secondCount = 0.0, secondR = 0.0, secondG = 0.0, secondB = 0.0;
                for (int d = -2; d <= 2; ++d) {
                    const auto& c = clusters[(second + kBins + d) % kBins];
                    const double k = (d == 0) ? 1.0 : (std::abs(d) == 1 ? 0.60 : 0.20);
                    secondCount += k * c.weightedVotes;
                    secondR += k * c.weightedRed;
                    secondG += k * c.weightedGreen;
                    secondB += k * c.weightedBlue;
                }
                if (secondCount > 0.0) {
                    outR = (1.0 - secondFraction) * outR + secondFraction * (secondR / secondCount);
                    outG = (1.0 - secondFraction) * outG + secondFraction * (secondG / secondCount);
                    outB = (1.0 - secondFraction) * outB + secondFraction * (secondB / secondCount);
                }
            }
        }
    }

    const uint8_t r = static_cast<uint8_t>(std::clamp(outR, 0.0, 255.0));
    const uint8_t g = static_cast<uint8_t>(std::clamp(outG, 0.0, 255.0));
    const uint8_t b = static_cast<uint8_t>(std::clamp(outB, 0.0, 255.0));
    const auto linear16 = InfiniteProcessing::srgbNonlinearToLinear(byte3(r, g, b));
    const float3 result = static_cast<float3>(linear16) / 65535.0f;
    if (!adaptive) {
        _lastDominantOutput = result;
        _hasLastDominantOutput = true;
    }
    ledColors.assign(_colorsMap.size(), result);
}
