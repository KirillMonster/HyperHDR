#pragma once

#ifndef PCH_ENABLED
	#include <cassert>
	#include <sstream>
	#include <math.h>
	#include <algorithm>
	#include <vector>
	#include <map>
#endif

#include <array>
#include <base/DominantDarkSceneGate.h>
#include <base/DeskAtmosphereGradient.h>
#include <image/Image.h>
#include <utils/Logger.h>
#include <base/LedString.h>

#include <linalg.h>

namespace hyperhdr
{	
	class ImageColorAveraging
	{
	public:
		ImageColorAveraging(
			const LoggerName& _log,
			const int mappingType,
			const bool sparseProcessing,
			const unsigned width,
			const unsigned height,
			const unsigned horizontalBorder,
			const unsigned verticalBorder,
			const quint8 instanceIndex,
			const std::vector<LedString::Led>& leds);

		unsigned width() const;
		unsigned height() const;

		unsigned horizontalBorder() const;
		unsigned verticalBorder() const;

		void process(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image);

	private:
		void getUnicolorForLeds(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image) const;
		void getMulticolorForLeds(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image) const;
		void getUnicolorExperimental(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image, bool adaptive);
		void getDeskAtmosphereGradient(std::vector<linalg::aliases::float3>& ledColors, const Image<ColorRgb>& image);

		const unsigned _width;
		const unsigned _height;
		const bool	_sparseProcessing;
		const unsigned _horizontalBorder;
		const unsigned _verticalBorder;
		int _mappingType;
		int _lastHueBin = -1;
		int _pendingHueBin = -1;
		int _pendingHueFrames = 0;
		std::array<double, 24> _dominantScores{};
		long long _lastDominantMs = 0;
		DominantDarkSceneGate _dominantDarkGate;
		DeskAtmosphereGradient _deskGradient;
		bool _hasLastDominantOutput = false;
		linalg::aliases::float3 _lastDominantOutput{0.f, 0.f, 0.f};

		std::vector<std::vector<uint32_t>> _colorsMap;
		std::map<int, std::vector<uint32_t>> _colorGroups;

		linalg::aliases::float3 calcMulticolorForLeds(const Image<ColorRgb>& image, const std::vector<uint32_t>& colors) const;
		linalg::aliases::float3 calcUnicolorForLeds(const Image<ColorRgb>& image) const;
	};
}
