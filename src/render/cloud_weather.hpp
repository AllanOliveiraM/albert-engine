#pragma once
#include "threepp/textures/Image.hpp"

namespace albert {
enum class CloudMode {Off,Observed,Simulated};
// Historical NASA structure for animated scenario clouds, not current weather.
// Shader advection keeps its fronts and fine wisps coherent.
threepp::Image simulatedCloudImage(unsigned width=4096,unsigned height=2048);
}
