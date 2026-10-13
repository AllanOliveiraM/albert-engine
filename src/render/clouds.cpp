#include "clouds.hpp"
#include "core/time.hpp"
#include "platform.hpp"
#include "wms.hpp"
#include "threepp/loaders/ImageLoader.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <unordered_map>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cache de nuvens indisponivel");
    std::error_code ec;fs::last_write_time(path,fs::file_time_type::clock::now(),ec);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string observationDay(int daysAgo = 0) {
    // Leave a processing margin. Within each UTC day the mosaic is partial
    // and may have gaps; the UI reports its date and actual valid coverage.
    const auto utc = albert::utcTime(static_cast<std::time_t>(albert::time::clock().utcSeconds()) - 3 * 3600 - daysAgo * 86400);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d");
    return out.str();
}

std::string url(const std::string& day, int reflectance = 0, albert::GeoRegion region = {}, unsigned width = 4096, unsigned height = 2048) {
    const std::string layers = reflectance ?
        (reflectance == 1 ? "MODIS_Aqua_CorrectedReflectance_TrueColor" : "MODIS_Terra_CorrectedReflectance_TrueColor") :
        "MODIS_Terra_Cloud_Fraction_Day,MODIS_Terra_Cloud_Fraction_Night,MODIS_Aqua_Cloud_Fraction_Day,MODIS_Aqua_Cloud_Fraction_Night";
    std::ostringstream bounds;
    bounds << std::fixed << std::setprecision(6) << region.west << ',' << region.south << ',' << region.east << ',' << region.north;
    return std::string("https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi?") +
        "SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&LAYERS=" + layers +
        "&SRS=EPSG:4326&BBOX=" + bounds.str() + "&WIDTH=" + std::to_string(width) + "&HEIGHT=" + std::to_string(height) +
        "&FORMAT=image/png&TRANSPARENT=TRUE&TIME=" + day;
}

// Cloud fraction is a 5 km cell statistic, not photographic opacity.
// Smooth only this visual weight; retain the observed reflectance structure
// and leave pixels without a scientific observation unavailable.
std::vector<float> smoothFraction(const std::vector<float>& input,unsigned width,unsigned height,int rx,int ry,bool wrap) {
    std::vector<float> sums(input.size()),weights(input.size()),output(input.size(),-1.f);
    for(unsigned y=0;y<height;++y) {
        float sum=0,weight=0;
        auto add=[&](int x,float sign) {
            if(wrap) x=(x%static_cast<int>(width)+width)%width;
            if(x<0 || x>=static_cast<int>(width)) return;
            const float value=input[static_cast<size_t>(y)*width+x];
            if(value>=0) {sum+=sign*value;weight+=sign;}
        };
        for(int x=-rx;x<=rx;++x) add(x,1);
        for(unsigned x=0;x<width;++x) {
            const auto at=static_cast<size_t>(y)*width+x;sums[at]=sum;weights[at]=weight;
            add(static_cast<int>(x)-rx,-1);add(static_cast<int>(x)+rx+1,1);
        }
    }
    for(unsigned x=0;x<width;++x) {
        float sum=0,weight=0;
        auto add=[&](int y,float sign) {
            if(y<0 || y>=static_cast<int>(height)) return;
            const auto at=static_cast<size_t>(y)*width+x;sum+=sign*sums[at];weight+=sign*weights[at];
        };
        for(int y=-ry;y<=ry;++y) add(y,1);
        for(unsigned y=0;y<height;++y) {
            const auto at=static_cast<size_t>(y)*width+x;
            if(input[at]>=0 && weight>0) output[at]=std::clamp(sum/weight,0.f,100.f);
            add(static_cast<int>(y)-ry,-1);add(static_cast<int>(y)+ry+1,1);
        }
    }
    return output;
}

albert::CloudUpdate decode(const std::array<std::string, 6>& data, const std::string& date, const fs::path& palettePath, unsigned width = 4096, unsigned height = 2048, albert::GeoRegion region = {}) {
    std::ifstream paletteFile(palettePath);
    const auto palette = json::parse(paletteFile);
    std::unordered_map<unsigned, int> fractions;
    for (const auto& entry : palette) {
        const auto key = (entry[1].get<unsigned>() << 16) | (entry[2].get<unsigned>() << 8) | entry[3].get<unsigned>();
        fractions.emplace(key, entry[0].get<int>());
    }
    std::array<std::optional<threepp::Image>, 6> images;
    threepp::ImageLoader loader;
    for (size_t i = 0; i < 6; ++i) {
        std::vector<unsigned char> bytes(data[i].begin(), data[i].end());
        images[i] = loader.load(bytes, 4, true);
        if (!images[i] || images[i]->width() != width || images[i]->height() != height) {
            throw std::runtime_error("NASA GIBS: imagem de nuvens invalida");
        }
    }
    std::vector<float> observedFraction(static_cast<size_t>(width)*height,-1.f);
    for(size_t pixel=0;pixel<observedFraction.size();++pixel) {
        const auto at=pixel*4;
        for(size_t observation=0;observation<2;++observation) {
            const auto& rgba=images[observation]->data();
            if(!rgba[at+3]) continue;
            const unsigned key=(static_cast<unsigned>(rgba[at])<<16)|(static_cast<unsigned>(rgba[at+1])<<8)|rgba[at+2];
            if(const auto match=fractions.find(key);match!=fractions.end()) {observedFraction[pixel]=static_cast<float>(match->second);break;}
        }
    }
    const double kilometersPerPixel=(region.north-region.south)*111.195/height;
    const int ry=std::clamp(static_cast<int>(std::ceil(5./kilometersPerPixel)),1,32);
    const double cosine=std::max(.2,std::cos((region.north+region.south)*.5*3.141592653589793/180.));
    const int rx=std::clamp(static_cast<int>(std::ceil(ry/cosine)),1,32);
    const auto visualFraction=smoothFraction(observedFraction,width,height,rx,ry,region.east-region.west>=359.99);
    std::vector<unsigned char> mask(static_cast<size_t>(width) * height * 4, 0);
    size_t valid = 0;
    double validArea = 0, detailArea = 0, totalArea = 0;
    for (size_t pixel = 0; pixel < mask.size(); pixel += 4) {
        const size_t row = pixel / (width * 4);
        const double area = std::cos((static_cast<double>(row) + .5) / height * (region.north-region.south) * 3.141592653589793/180. + region.south * 3.141592653589793/180.);
        totalArea += area;
        if(observedFraction[pixel/4]<0) continue;
        const float fraction=visualFraction[pixel/4];
        ++valid;
        validArea += area;
        // Cloud fraction gives coverage, not photographic opacity. Daytime
        // reflectance adds observed spatial structure instead of flat white cells.
        // Snow/bright ground can remain ambiguous; this is a derived visual mask.
        float detail = 0.f, remaining = 1.f;
        const auto column = (pixel / 4) % width;
        for (size_t observation = 2; observation < 6 && remaining > .001f; ++observation) {
            const auto& rgb = images[observation]->data();
            auto available = [&](size_t x, size_t y) {
                const size_t at = 4 * (y*width+x);
                // GIBS can encode missing swaths as opaque black.
                return rgb[at+3] != 0 && std::max({rgb[at], rgb[at+1], rgb[at+2]}) > 8;
            };
            if (!available(column, row)) continue;
            const float white = std::min({rgb[pixel], rgb[pixel+1], rgb[pixel+2]}) / 255.f;
            const float t = std::clamp((white - .12f) / .68f, 0.f, 1.f);
            // Blend observation boundaries into the next available pass, rather
            // than turning missing reflectance into opaque rectangular clouds.
            const float confidence = (1.f + available((column+width-12)%width,row) +
                available((column+12)%width,row) + available(column,row > 12 ? row-12 : 0) +
                available(column,std::min<size_t>(height-1,row+12))) / 5.f;
            detail += t*t*(3.f-2.f*t) * confidence * remaining;
            remaining *= 1.f-confidence;
        }
        if (remaining < 1.f) detailArea += area;
        // Reflectance already resolves the broken-cloud coverage within a
        // coarse fraction cell. Use the statistic as a presence confidence,
        // rather than attenuating that photographed coverage a second time.
        const float confidence=std::clamp(fraction/20.f,0.f,1.f);
        const auto alpha = static_cast<unsigned char>(255.f*confidence*confidence*(3.f-2.f*confidence)*detail);
        mask[pixel] = mask[pixel+1] = mask[pixel+2] = alpha;
        mask[pixel+3] = 255;
    }
    if (valid < static_cast<size_t>(width) * height / 100) throw std::runtime_error("NASA GIBS: mosaico ainda sem cobertura suficiente");
    albert::CloudUpdate result;
    result.image = threepp::Image(std::move(mask), width, height);
    result.date = date;
    result.coverage = validArea / totalArea;
    result.detailCoverage = detailArea / totalArea;
    return result;
}

void save(const fs::path& directory, const std::array<std::string, 6>& data, const std::string& day) {
    fs::create_directories(directory);
    // Invalidate the commit marker before replacing observation files.
    fs::remove(directory / "date.txt");
    for (size_t i = 0; i < 6; ++i) {
        constexpr const char* names[] = {"current", "previous", "reflectance-current", "reflectance-current-terra", "reflectance-previous", "reflectance-previous-terra"};
        const auto target = directory / (std::string(names[i]) + ".png");
        const auto temporary = directory / (std::string(names[i]) + ".tmp");
        std::ofstream file(temporary, std::ios::binary);
        file.write(data[i].data(), static_cast<std::streamsize>(data[i].size()));
        file.close();
        if (!file) throw std::runtime_error("Nao foi possivel salvar o cache de nuvens");
        fs::remove(target); // Windows rename does not replace an existing file.
        fs::rename(temporary, target);
    }
    std::ofstream fetched(directory / "fetched-at.txt");
    fetched << static_cast<std::time_t>(albert::time::clock().utcSeconds());fetched.close();
    if(!fetched) throw std::runtime_error("Nao foi possivel salvar a consulta das nuvens");
    std::ofstream metadata(directory / "date.txt");
    metadata << day;
    metadata.close();
    if (!metadata) throw std::runtime_error("Nao foi possivel salvar a data das nuvens");
}
} // namespace

albert::CloudSync::CloudSync(fs::path cache, fs::path palette): cache_(std::move(cache)), palette_(std::move(palette)) {}
albert::CloudSync::~CloudSync() {
    progress.cancelled = true;
    detailProgress_.cancelled = true;
    if (pending_.valid()) pending_.wait();
    if (detailPending_.valid()) detailPending_.wait();
}

void albert::CloudSync::refresh(bool clearCache) {
    if (active) return;
    error.clear();
    detailError.clear();
    loaded_.reset();
    detailNext_ = {};
    if (clearCache) detailProgress_.cancelled = true;
    progress.cancelled = false;
    const bool tryCache = !cacheChecked_ && !clearCache;
    cacheChecked_ = true;
    pending_ = std::async(std::launch::async, [this, clearCache, tryCache] {
        CloudUpdate fallback;
        if (clearCache) {
            fs::remove_all(cache_/"detail");
            // Only fixed, known cache files. Static bundled assets are untouched.
            for (const char* name : {"date.txt", "fetched-at.txt", "current.png", "previous.png", "current.tmp", "previous.tmp", "day.png", "night.png", "day.tmp", "night.tmp", "reflectance-current.png", "reflectance-previous.png", "reflectance-current.tmp", "reflectance-previous.tmp", "reflectance-current-terra.png", "reflectance-previous-terra.png", "reflectance-current-terra.tmp", "reflectance-previous-terra.tmp"}) fs::remove(cache_ / name);
        } else if (tryCache) {
            try {
                const auto cachedDay = read(cache_ / "date.txt");
                fallback = decode({read(cache_ / "current.png"), read(cache_ / "previous.png"), read(cache_ / "reflectance-current.png"), read(cache_ / "reflectance-current-terra.png"), read(cache_ / "reflectance-previous.png"), read(cache_ / "reflectance-previous-terra.png")}, cachedDay, palette_);
                fallback.fromCache = true;
                try {
                    const auto fetched=std::stoll(read(cache_/"fetched-at.txt"));
                    const auto age=static_cast<std::time_t>(albert::time::clock().utcSeconds())-fetched;
                    if(age>=0 && age<1800) fallback.refreshAfterSeconds=static_cast<int>(1800-age);
                } catch(const std::exception&) {}
                return fallback; // Show cached observations; retain the 30-minute cadence.
            } catch (const std::exception&) { /* No usable cache: request observations. */ }
        }
        try {
            const auto day = observationDay();
            std::array<std::string, 6> data;
            const auto previousDay = observationDay(1);
            progress.stage("Nuvens: observacoes Terra e Aqua de hoje", 0);
            data[0] = httpGet(url(day), &progress);
            progress.stage("Nuvens: preenchendo lacunas com o dia anterior", 1);
            data[1] = httpGet(url(previousDay), &progress);
            // Fetch independently: the server can paint missing swaths opaque
            // black, which would hide another instrument in a merged WMS layer.
            progress.stage("Nuvens: detalhes Aqua de hoje", 2);
            data[2] = httpGet(url(day, 1), &progress);
            progress.stage("Nuvens: detalhes Terra de hoje", 3);
            data[3] = httpGet(url(day, 2), &progress);
            progress.stage("Nuvens: detalhes Aqua anteriores", 4);
            data[4] = httpGet(url(previousDay, 1), &progress);
            progress.stage("Nuvens: detalhes Terra anteriores", 5);
            data[5] = httpGet(url(previousDay, 2), &progress);
            progress.checkCancelled();
            const auto dates = previousDay + " a " + day;
            auto result = decode(data, dates, palette_);
            try { save(cache_, data, dates); }
            catch (const std::exception& exception) { result.error = exception.what(); }
            return result;
        } catch (const std::exception& exception) {
            fallback.error = exception.what();
            return fallback;
        }
    });
    active = true;
}

std::optional<albert::CloudUpdate> albert::CloudSync::poll(bool enabled) {
    const auto now = std::chrono::steady_clock::now();
    if (!enabled) {progress.cancelled=true;detailProgress_.cancelled=true;next_={};}
    if (enabled && !active && now >= next_) refresh();
    if (!active || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
    active = false;
    CloudUpdate result;
    try { result = pending_.get(); }
    catch (const std::exception& exception) { result.error = exception.what(); }
    if(progress.cancelled) {cacheChecked_=false;next_={};return std::nullopt;}
    error = result.error;
    if (!error.empty()) std::cerr << "Nuvens: " << error << '\n';
    if (result.image) { date = result.date; coverage = result.coverage; detailCoverage = result.detailCoverage; }
    if (result.fromCache) {
        next_ = now + std::chrono::seconds(result.refreshAfterSeconds);
    } else if (error.empty()) {
        failures_ = 0;
        next_ = now + std::chrono::minutes(30);
    } else {
        next_ = now + std::chrono::seconds(std::min(1800, 30 * (1 << std::min(failures_++, 6))));
    }
    return result;
}


void albert::CloudSync::requestDetail(std::optional<GeoRegion> region) {
    if (region != desired_) {
        if(detailPending_.valid() && (!region || (working_ && !regionRelevant(*working_,*region)))) detailProgress_.cancelled=true;
        desired_ = region;
        detailFailures_ = 0;
        if (!region) detailProgress_.cancelled = true;
    }
}

std::optional<albert::CloudDetail> albert::CloudSync::pollDetail() {
    const auto now = std::chrono::steady_clock::now();
    if (detailPending_.valid()) {
        if (detailPending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
        auto result = detailPending_.get();
        working_.reset();
        if (detailProgress_.cancelled || !desired_ || !regionRelevant(result.region,*desired_) || result.observation.date!=date) return std::nullopt;
        detailError = result.observation.error;
        if (result.observation.image) {
            loaded_ = result.region;
            detailFailures_ = 0;
            detailNext_ = now + std::chrono::minutes(30);
        } else {
            detailNext_ = now + std::chrono::seconds(std::min(1800, 30 * (1 << std::min(detailFailures_++, 6))));
        }
        return result;
    }
    if (!desired_ || active || date.empty() || (now < detailNext_ && loaded_ && regionSufficient(*loaded_,*desired_)) ||
        (now < detailNext_ && !detailError.empty()) || now-detailChanged_ < std::chrono::milliseconds(250)) return std::nullopt;
    const auto region = *desired_;
    const auto dates = date;
    working_=region;detailChanged_=now;
    detailProgress_.cancelled = false;
    detailPending_ = std::async(std::launch::async, [this,region,dates] {
        CloudDetail result; result.region = region;
        try {
            std::array<std::string,6> data;
            std::ostringstream cell;
            cell << std::fixed << std::setprecision(3) << region.west << '_' << region.south << '_' << region.east << '_' << region.north;
            const auto directory = cache_/"detail"/(dates.substr(dates.size()-10)+"_"+cell.str());
            try {
                const auto age=static_cast<std::time_t>(albert::time::clock().utcSeconds())-std::stoll(read(directory/"fetched-at.txt"));
                if(age>=0 && age<1800 && read(directory/"date.txt")==dates) {
                    result.observation=decode({read(directory/"current.png"),read(directory/"previous.png"),read(directory/"reflectance-current.png"),read(directory/"reflectance-current-terra.png"),read(directory/"reflectance-previous.png"),read(directory/"reflectance-previous-terra.png")},dates,palette_,2048,2048,region);
                    return result;
                }
            } catch(const std::exception&) {}
            const std::string previous = dates.substr(0,10), current = dates.substr(dates.size()-10);
            const std::array<std::string,6> days = {current,previous,current,current,previous,previous};
            constexpr int channels[] = {0,0,1,2,1,2};
            for (size_t i=0;i<6;++i) {
                detailProgress_.stage("Detalhes regionais das nuvens",static_cast<int>(i));
                data[i]=fetchWms([&](const GeoRegion& r,unsigned w,unsigned h){return url(days[i],channels[i],r,w,h);},region,2048,2048,detailProgress_);
            }
            detailProgress_.checkCancelled();
            result.observation = decode(data,dates,palette_,2048,2048,region);
            detailProgress_.checkCancelled();
            try { save(directory,data,dates); } catch(const std::exception& e) { result.observation.error=e.what(); }
        } catch (const std::exception& e) { result.observation.error=e.what(); }
        return result;
    });
    return std::nullopt;
}
