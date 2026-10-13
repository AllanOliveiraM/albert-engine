#pragma once
#include "geography.hpp"
#include "download.hpp"
#include <functional>
namespace albert {
using WmsURL=std::function<std::string(const GeoRegion&,unsigned,unsigned)>;
// WMS providers require canonical longitude bounds; reassemble a wrapped view.
std::string fetchWms(const WmsURL& url,const GeoRegion& region,unsigned width,unsigned height,DownloadProgress& progress);
}
