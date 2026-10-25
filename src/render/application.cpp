
#include "physics/earth.hpp"
#include "threepp/threepp.hpp"
#include "threepp/objects/LineSegments.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/math/Spherical.hpp"
#include "threepp/materials/PointsMaterial.hpp"
#include "threepp/extras/imgui/ImguiContext.hpp"

#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <erfa.h>
#include <imgui.h>

#include "application.hpp"
#include "platform.hpp"
#include "download.hpp"
#include "visuals.hpp"
#include "clouds.hpp"
#include "cache.hpp"
#include "regional.hpp"
#include "animation.hpp"
#include "memory.hpp"
#include "paths.hpp"
#include "borders.hpp"
#include "objects.hpp"
#include "rocks.hpp"
#include "eclipses.hpp"
#include "window_icon.hpp"
#include "broadcast.hpp"
#ifdef ALBERT_HAS_RPC
#include "playground.hpp"
#endif
#include "display.hpp"
#include "gpu/context.hpp"
#include "layers.hpp"
#include "asset_pack.hpp"
#include "asset_downloads.hpp"
#include "solids.hpp"
#include "comets.hpp"
#include "core/bridge.hpp"
#include "core/time.hpp"
#include "core/simulation.hpp"
#include "core/approximate_ephemeris.hpp"
#include "core/comets.hpp"
#include "core/astro_time.hpp"
#include "core/local_system.hpp"
#include "debris.hpp"
#include "providers/horizons.hpp"
#include "catalog/catalog.hpp"
#ifdef ALBERT_HAS_RPC
#include "rpc/server.hpp"
#endif
#include "sound.hpp"
#include "threepp/renderers/gl/GLShadowMap.hpp"
#include "threepp/renderers/gl/GLInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <limits>
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <utility>
#include <vector>

using namespace threepp;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr double PI = 3.14159265358979323846;

// ALBERT_STREAM=path: the frames as raw RGBA rows, at the window's first size, written into that pipe (watch-stream.py's ffmpeg
// reads it) at ALBERT_STREAM_FPS a second (20), their size first into path.size; on a thread of their own, the newest frame only:
// a slow reader drops frames, never the render.
class FrameStream {
    struct State {std::mutex lock;std::condition_variable ready;std::vector<unsigned char> frame;bool fresh=false,busy=false,stop=false;unsigned width=0,height=0;};
    std::shared_ptr<State> s_=std::make_shared<State>();std::string path_;
public:
    explicit FrameStream(std::string path):path_(std::move(path)){
#ifndef _WIN32
        std::signal(SIGPIPE,SIG_IGN); // Its reader gone: a failed write, not the end of the app.
#endif
        std::thread([s=s_,path=path_]{std::FILE* f=std::fopen(path.c_str(),"wb");if(!f){std::cerr<<"Transmissao: "<<path<<" nao abriu"<<std::endl;return;}std::vector<unsigned char> out;
            for(;;){{std::unique_lock l(s->lock);s->ready.wait(l,[&]{return s->fresh||s->stop;});if(s->stop)break;out.swap(s->frame);s->fresh=false;s->busy=true;}
                const bool ok=std::fwrite(out.data(),1,out.size(),f)==out.size()&&std::fflush(f)==0;std::lock_guard l(s->lock);s->busy=false;if(!ok)break;}
            std::fclose(f);}).detach(); // Blocked opening a pipe nobody reads yet, it must not hold the app's exit.
    }
    ~FrameStream(){{std::lock_guard l(s_->lock);s_->stop=true;}s_->ready.notify_all();}
    bool wanted()const{std::lock_guard l(s_->lock);return !s_->fresh&&!s_->busy;}
    void put(std::vector<unsigned char>&& rgba,unsigned w,unsigned h){std::lock_guard l(s_->lock);
        if(!s_->width){s_->width=w;s_->height=h;std::ofstream(path_+".size")<<w<<"x"<<h<<"\n";}if(w!=s_->width||h!=s_->height)return; // Resized: its frames left out.
        s_->frame=std::move(rgba);s_->fresh=true;s_->ready.notify_one();}
};

// Valores em km
constexpr double EARTH_RADIUS = 6378.137;
constexpr double EARTH_POLAR = 6356.752314245;
constexpr double MOON_RADIUS = 1737.4;
constexpr double SUN_RADIUS = 695700.0;

constexpr double SCALE = EARTH_RADIUS;
constexpr double F = 1.0 / 298.257223563;
constexpr double E2 = F * (2.0 - F);

// Aproximacoes IERS ainda pendentes
constexpr double DUT1 = 0;
constexpr double XP = 0;
constexpr double YP = 0;

// Parametros de visualizacao
constexpr float CAMERA_EASING = 0.12f;

constexpr int REFRESH_SECONDS = 300;

struct Vec3 {
    double x = 0, y = 0, z = 0;

    Vec3 operator+(Vec3 b) const {
        return {x + b.x, y + b.y, z + b.z};
    }

    Vec3 operator-(Vec3 b) const {
        return {x - b.x, y - b.y, z - b.z};
    }

    Vec3 operator*(double s) const {
        return {x * s, y * s, z * s};
    }
};

struct State {
    double jd = 0;
    Vec3 position;
    Vec3 velocity;
};

struct Ephemerides {
    bool cached = false;
    // Simulated interval of each table: "sun", "moon" and body ids.
    std::map<std::string,albert::providers::Window> windows;
    std::vector<State> sun;
    std::vector<State> moon;
    std::map<std::string,std::vector<State>> bodies;
    std::map<std::string,std::vector<State>> comets; // Geocentric, by scene id.
    std::vector<std::string> failures; // Comet tables that failed; the rest still apply.
};

struct Orbits {
    bool cached = false;
    double centerUtc = 0; // Simulated instant the paths are centred on.
    std::vector<State> moon;
    std::vector<State> earth;
    std::map<std::string,std::vector<State>> bodies;
};

std::vector<const albert::core::NaturalBody*> availableBodies(){
    std::vector<const albert::core::NaturalBody*> result;std::shared_ptr<const albert::AssetPack> pack;
    // While the essential container downloads there is no library yet; every
    // bundled planet arrives with it, so count them all.
    try{pack=albert::visualAssetPack();}catch(const std::exception&){}
    for(const auto& body:albert::core::naturalBodies()){const std::string id(body.id);if(!pack||pack->contains(id+"-day"))result.push_back(&body);}
    return result;
}

// The loading card shows the essential container like any other stage.
void waitForEssential(albert::AssetDownloads& assets,albert::DownloadProgress& progress){
    std::string shown;
    while(true){
        const auto status=assets.status();if(status.essentialReady)return;
        const auto& item=status.items.front();
        const auto stage=status.error.empty()?std::string("Baixando dados visuais essenciais"):"Dados essenciais: nova tentativa em "+std::to_string(status.retrySeconds)+" s ("+status.error.substr(0,60)+")";
        if(stage!=shown){progress.stage(stage,0);shown=stage;}
        progress.transfer(static_cast<std::int64_t>(item.received),static_cast<std::int64_t>(item.bytes));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));progress.checkCancelled();
    }
}

int loadSteps(){static const int steps=11+2*static_cast<int>(availableBodies().size())+static_cast<int>(albert::core::cometActivities().size());return steps;}

using Focus=albert::core::ObjectId;
namespace targets {
const Focus Earth="earth",Moon="moon",Sun="sun",EarthMoon="@earth-moon",Overview="@system",Inner="@inner-system";
}



Vector3 graphical(Vec3 p) {
    return {
        static_cast<float>(p.x / SCALE),
        static_cast<float>(p.z / SCALE),
        static_cast<float>(-p.y / SCALE)
    };
}

void append(std::vector<float>& out, Vec3 p) {
    auto v = graphical(p);
    out.insert(out.end(), {v.x, v.y, v.z});
}

double radians(double degrees){return degrees*PI/180.;}

Vec3 geodetic(double latDeg, double lonDeg, double h = 0) {
    double lat = radians(latDeg);
    double lon = radians(lonDeg);

    double s = std::sin(lat);
    double c = std::cos(lat);

    double n = EARTH_RADIUS /
        std::sqrt(1.0 - E2 * s * s);

    return {
        (n + h) * c * std::cos(lon),
        (n + h) * c * std::sin(lon),
        (n * (1.0 - E2) + h) * s
    };
}

// ----------------------------------------
// Tempo astronomico
// ----------------------------------------

double jdTT(double utc){return albert::astro::julianTT(utc);}
double jdTDB(double utc){return albert::astro::julianTDB(utc);}

// Celestial (GCRS) to terrestrial (ITRS) at a Julian UTC date.
void terrestrial(double utc, double m[3][3]) {
    double tt = jdTT(utc);
    double ut1 = utc + DUT1 / 86400.0;
    eraC2t06a(
        2400000.5, tt - 2400000.5,
        2400000.5, ut1 - 2400000.5,
        XP, YP, m
    );
}

Matrix4 earthOrientation(const double m[3][3]) {

    auto transform = [&](Vec3 p) -> Vec3 {
        return {
            m[0][0]*p.x + m[1][0]*p.y + m[2][0]*p.z,
            m[0][1]*p.x + m[1][1]*p.y + m[2][1]*p.z,
            m[0][2]*p.x + m[1][2]*p.y + m[2][2]*p.z
        };
    };

    Vector3 x = graphical(transform({SCALE, 0, 0}));
    Vector3 y = graphical(transform({0, 0, SCALE}));
    Vector3 z = graphical(transform({0, -SCALE, 0}));

    Matrix4 result;
    result.makeBasis(x, y, z);
    return result;
}

// ----------------------------------------
// HTTP
// ----------------------------------------

using albert::httpGet;

// Interval in Unix UTC seconds of simulated time. Cached tables count: revisiting a
// date reuses them, also offline.
std::vector<State> states(const albert::providers::Table& table,bool* cached){
    if(cached)*cached|=table.fromCache;
    std::vector<State> result;result.reserve(table.points.size());
    for(const auto& point:table.points)result.push_back({point.jdTDB,{point.positionMeters.x/1000.,point.positionMeters.y/1000.,point.positionMeters.z/1000.},{point.velocityMetersPerSecond.x/1000.,point.velocityMetersPerSecond.y/1000.,point.velocityMetersPerSecond.z/1000.}});
    return result;
}

std::vector<State> queryHorizons(int body,const std::string& center,double start,double end,const std::string& step,albert::DownloadProgress* progress=nullptr,bool bootstrap=false,bool* cached=nullptr){
    return states(albert::providers::horizons().query({body,center,start,end,step,bootstrap},progress),cached);
}

// Periodic comet by designation, from the solution of its apparition for the dates.
std::vector<State> queryComet(const std::string& designation,const albert::providers::Window& window,albert::DownloadProgress* progress,bool bootstrap,bool* cached){
    albert::providers::Query query;query.center="500@399";query.startUtc=window.startUtc;query.endUtc=window.endUtc;query.step=window.step;
    query.bootstrapCache=bootstrap;query.comet=designation;
    return states(albert::providers::horizons().query(query,progress),cached);
}

// Positions of the Sun, the Moon and the planets: at least one simulated day, and
// `playbackSeconds` of playback at the current rate. Cubic Hermite error grows as
// a*(2*pi*step/period)^4/384, so steps stay below ~1/45 of the fastest period
// involved (the body's own, or Earth's year for geocentric positions): at the
// coarsest rows the Moon is within ~3 km, Venus ~100 km, the Sun and others ~20 km.
// Horizons answers each table in one to several seconds: tables stay short, and
// above TABLE_RATE_LIMIT (simulated seconds per second) they cannot keep up with
// playback, so positions there come from the analytic models anchored to the
// last tables, which are fetched again once playback slows down or pauses.
constexpr double TABLE_RATE_LIMIT = 6 * 3600.;
struct PlaybackPlan {double seconds;unsigned rows;};
constexpr PlaybackPlan FULL_PLAYBACK{600., 200};
constexpr PlaybackPlan QUICK_PLAYBACK{60., 60}; // First table for a date without data.
albert::providers::Playback ephemerisPlayback(std::string_view id, PlaybackPlan plan) {
    constexpr double DAY = 86400.;
    if (id == "moon") return {DAY, plan.seconds, plan.rows, 3600., DAY};
    // Comets near perihelion move fastest: daily rows keep Halley within ~2 km.
    if (albert::core::cometActivity(id)) return {DAY, plan.seconds, plan.rows, DAY, DAY};
    double coarsest = 5 * DAY;
    if (const auto* body = albert::core::naturalBody(id)) coarsest = std::min(coarsest, 2 * body->orbitHalfDays * DAY / 45);
    coarsest = std::max(DAY, coarsest);
    return {DAY, plan.seconds, plan.rows, DAY, coarsest};
}
// A newer request replaces a running one between bodies.
struct Superseded : std::runtime_error {Superseded() : std::runtime_error("Efemerides substituidas por outra data") {}};

Ephemerides loadEphemerides(double utc, double rate, PlaybackPlan plan, albert::DownloadProgress* progress = nullptr, int offset = 0, bool bootstrap = false, const std::function<bool()>& superseded = {}) {
    Ephemerides e;
    auto query=[&](const std::string& id, int body){
        if (superseded && superseded()) throw Superseded();
        const auto window = albert::providers::playbackWindow(utc, rate, ephemerisPlayback(id, plan));
        e.windows[id] = window;
        return queryHorizons(body, "500@399", window.startUtc, window.endUtc, window.step, progress, bootstrap, &e.cached);
    };

    if (progress) progress->stage("Efemerides do Sol", 2 + offset);
    e.sun = query("sun", 10);

    if (progress) progress->stage("Efemerides da Lua", 3 + offset);
    e.moon = query("moon", 301);

    int bodyIndex=0;
    for(const auto* body:availableBodies()){
        if(progress)progress->stage("Efemerides: "+std::string(body->name),4+offset+bodyIndex++);
        e.bodies[std::string(body->id)]=query(std::string(body->id), body->horizonsId);
    }
    for(const auto& comet:albert::core::cometActivities()){
        if(progress)progress->stage("Efemerides: cometa "+std::string(comet.designation),4+offset+bodyIndex++);
        if (superseded && superseded()) throw Superseded();
        const std::string id(comet.id);
        const auto window = albert::providers::playbackWindow(utc, rate, ephemerisPlayback(id, plan));
        e.windows[id] = window;
        // One comet's table must not discard the others: it keeps its elements meanwhile.
        try { e.comets[id] = queryComet(std::string(comet.designation), window, progress, bootstrap, &e.cached); }
        catch (const Superseded&) { throw; }
        catch (const std::exception& error) {
            if (progress && progress->cancelled) throw;
            e.windows.erase(id);
            e.failures.push_back("Cometa " + std::string(comet.designation) + ": " + error.what());
        }
    }
    return e;
}

// Some table needs replacing for the full playback ahead of the simulated instant.
bool expiring(const Ephemerides& e, double utc, double rate) {
    for (const auto& [id, window] : e.windows)
        if (albert::providers::expiring(window, utc, rate, ephemerisPlayback(id, FULL_PLAYBACK))) return true;
    return e.windows.empty();
}

// Real seconds of playback the tables still cover ahead of the instant.
double secondsCovered(const Ephemerides& e, double utc, double rate) {
    if (rate == 0.) return std::numeric_limits<double>::infinity();
    double ahead = std::numeric_limits<double>::infinity();
    for (const auto& [id, window] : e.windows) {(void)id;ahead = std::min(ahead, rate > 0 ? window.endUtc - utc : utc - window.startUtc);}
    return std::max(0., ahead) / std::abs(rate);
}

bool covers(const Ephemerides& e, double tdb) {
    auto inside=[&](const std::vector<State>& v){return v.size()>=2&&tdb>=v.front().jd&&tdb<=v.back().jd;};
    if(!inside(e.sun)||!inside(e.moon))return false;
    for(const auto& [id,samples]:e.bodies){(void)id;if(!inside(samples))return false;}
    for(const auto& [id,samples]:e.comets){(void)id;if(!inside(samples))return false;}
    return true;
}

// Drawn paths only; positions never depend on them. Centred on the simulated
// instant, one revolution or more, so the highlight can fold by whole turns.
// Paths yield the shared JPL queue to position tables between requests.
Orbits loadOrbits(double centerUtc, albert::DownloadProgress* progress = nullptr, int offset = 0, bool bootstrap = false, const std::function<bool()>& superseded = {}, const std::function<bool()>& yield = {}) {
    constexpr double DAY = 86400.;
    Orbits orbits;
    orbits.centerUtc = centerUtc;
    auto query=[&](int body,const char* center,double halfDays,const std::string& step){
        while (yield && yield()) {
            if (progress) progress->checkCancelled();
            if (superseded && superseded()) throw Superseded();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (superseded && superseded()) throw Superseded();
        return queryHorizons(body, center, centerUtc - halfDays*DAY, centerUtc + halfDays*DAY, step, progress, bootstrap, &orbits.cached);
    };

    // Trajetoria geocentrica da Lua (~35 dias).
    if (progress) progress->stage("Orbita da Lua", 4 + offset);
    orbits.moon = query(301, "500@399", 14., "3 h");

    // Trajetoria heliocentrica da Terra (~370 dias).
    if (progress) progress->stage("Orbita da Terra", 5 + offset);
    orbits.earth = query(399, "500@10", 185., "1 d");

    int bodyIndex=0;
    for(const auto* body:availableBodies()){
        if(progress)progress->stage("Orbita: "+std::string(body->name),6+offset+bodyIndex++);
        orbits.bodies[std::string(body->id)]=query(body->horizonsId,"500@10",body->orbitHalfDays,std::to_string(static_cast<int>(body->orbitStepHours))+" h");
    }
    return orbits;
}

Vec3 interpolate(const std::vector<State>& states, double jd) {
    if (states.size() < 2 ||
        jd < states.front().jd ||
        jd > states.back().jd) {
        throw std::runtime_error("Efemerides expiradas");
    }

    auto upper = std::upper_bound(
        states.begin(), states.end(), jd,
        [](double t, const State& s) { return t < s.jd; }
    );

    size_t index = std::clamp<size_t>(
        std::distance(states.begin(), upper),
        1, states.size() - 1
    );

    const auto& a = states[index - 1];
    const auto& b = states[index];

    double duration = (b.jd - a.jd) * 86400.0;
    double t = (jd - a.jd) * 86400.0 / duration;
    double t2 = t*t;
    double t3 = t2*t;

    return a.position * (2*t3 - 3*t2 + 1)
         + a.velocity * (duration * (t3 - 2*t2 + t))
         + b.position * (-2*t3 + 3*t2)
         + b.velocity * (duration * (t3 - t2));
}

// Derivative of the same Hermite cubic (km/s), for tables covering jd.
Vec3 interpolateVelocity(const std::vector<State>& states, double jd) {
    const auto upper = std::upper_bound(states.begin(), states.end(), jd, [](double t, const State& s) { return t < s.jd; });
    const size_t index = std::clamp<size_t>(std::distance(states.begin(), upper), 1, states.size() - 1);
    const auto& a = states[index - 1];const auto& b = states[index];
    const double duration = (b.jd - a.jd) * 86400.0, t = (jd - a.jd) * 86400.0 / duration, t2 = t*t;
    return (a.position * (6*t2 - 6*t) + b.position * (6*t - 6*t2)) * (1 / duration)
         + a.velocity * (3*t2 - 4*t + 1) + b.velocity * (3*t2 - 2*t);
}

// ----------------------------------------
// Cartografia
// ----------------------------------------

void addBorder(
    std::vector<float>& vertices,
    const json& a,
    const json& b
) {
    double lon1 = a[0].get<double>();
    double lat1 = a[1].get<double>();
    double lon2 = b[0].get<double>();
    double lat2 = b[1].get<double>();

    if (lon2 - lon1 > 180) lon2 -= 360;
    if (lon2 - lon1 < -180) lon2 += 360;

    int steps = std::max(1, static_cast<int>(
        std::ceil(std::max(
            std::abs(lat2 - lat1),
            std::abs(lon2 - lon1)
        ) / 0.05)
    ));

    for (int i = 0; i < steps; ++i) {
        double t0 = double(i) / steps;
        double t1 = double(i + 1) / steps;

        append(vertices, geodetic(
            lat1 + (lat2-lat1)*t0,
            lon1 + (lon2-lon1)*t0, 0
        ));

        append(vertices, geodetic(
            lat1 + (lat2-lat1)*t1,
            lon1 + (lon2-lon1)*t1, 0
        ));
    }
}

void addRing(const json& ring, std::vector<float>& vertices) {
    if (ring.size() < 2) return;

    for (size_t i = 1; i < ring.size(); ++i) {
        addBorder(vertices, ring[i-1], ring[i]);
    }

    if (ring.front() != ring.back()) {
        addBorder(vertices, ring.back(), ring.front());
    }
}

std::vector<float> loadBorders(const fs::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("GeoJSON indisponivel");

    json data;
    file >> data;

    std::vector<float> vertices;

    for (const auto& feature : data.at("features")) {
        const auto& g = feature.at("geometry");
        if (g.is_null()) continue;

        auto type = g.at("type").get<std::string>();
        const auto& coords = g.at("coordinates");

        if (type == "Polygon") {
            for (const auto& ring : coords) {
                addRing(ring, vertices);
            }
        } else if (type == "MultiPolygon") {
            for (const auto& polygon : coords) {
                for (const auto& ring : polygon) {
                    addRing(ring, vertices);
                }
            }
        }
    }

    return vertices;
}

struct LoadedData {
    std::optional<albert::DecodedAssets> assets;
    std::vector<float> borders;
    Ephemerides ephemerides;
    Orbits orbits;
    std::uint64_t timeGeneration=0,simulationGeneration=0;
};

// Network work owns its progress state and never accesses scene/UI objects.
// Scene replacements are applied on the render thread only after full success.
class DataLoader {
public:
    explicit DataLoader(fs::path countries,albert::AssetDownloads* assets=nullptr): countries_(std::move(countries)),assets_(assets) {}

    ~DataLoader() {
        progress.cancelled = true;
        if (pending_.valid()) pending_.wait();
    }

    void start(bool reloadAssets = true, bool bootstrap = false) {
        if (active) return;
        reloadAssets_ = reloadAssets;
        bootstrap_ = bootstrap;
        error.clear();
        animation = {};
        progress.cancelled = false;
        progress.stage("Preparando simulacao", 0);
        // Planned on the render thread: the simulated instant when the load starts.
        const auto moment = albert::time::simulation().read();
        pending_ = std::async(std::launch::async, [this, reloadAssets, bootstrap, moment] {
            LoadedData result;
            if (reloadAssets) {
                if (assets_) waitForEssential(*assets_, progress);
                result.assets = albert::loadVisualAssets(countries_.parent_path(), progress);
                progress.stage("Preparando cartografia local", 6);
                result.borders = loadBorders(countries_);
            }
            result.timeGeneration=albert::time::clock().status().generation;
            result.simulationGeneration=moment.generation;
            result.ephemerides = loadEphemerides(moment.utcSeconds, moment.rate, FULL_PLAYBACK, &progress, 5, bootstrap);
            result.orbits = loadOrbits(moment.utcSeconds, &progress, 5+static_cast<int>(availableBodies().size()+albert::core::cometActivities().size()), bootstrap);
            progress.stage("Pronto", loadSteps());
            return result;
        });
        active = true;
    }

    void retry() { start(reloadAssets_, bootstrap_); }

    double retryIn() const {
        return std::max(0., std::chrono::duration<double>(nextAttempt_ - std::chrono::steady_clock::now()).count());
    }

    std::optional<LoadedData> poll() {
        if (!active && !error.empty() && progress.snapshot().completed >= 7 && retryIn() == 0) retry();
        if (!active || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return std::nullopt;
        }
        active = false;
        try {
            auto result = pending_.get();
            failures_ = 0;
            std::cout << "Dados carregados: " << countries_ << std::endl;
            return result;
        } catch (const std::exception& exception) {
            error = exception.what();
            const auto delay = std::min(300, 15 * (1 << std::min(failures_++, 5)));
            nextAttempt_ = std::chrono::steady_clock::now() + std::chrono::seconds(delay);
            std::cerr << "Download: " << error << '\n';
            return std::nullopt;
        }
    }

    bool active = false;
    std::string error;
    albert::DownloadProgress progress;
    // Only the render thread accesses presentation state.
    struct Animation {
        std::string stage;
        int completed = -1;
        float overall = 0, individual = 0;
    } animation;

private:
    fs::path countries_;
    albert::AssetDownloads* assets_ = nullptr;
    bool reloadAssets_ = true;
    bool bootstrap_ = false;
    int failures_ = 0;
    std::chrono::steady_clock::time_point nextAttempt_{};
    std::future<LoadedData> pending_;
};

void drawLoadingBar(float fraction, bool indeterminate, bool animated, const ImVec4& tint) {
    const auto origin = ImGui::GetCursorScreenPos();
    const float width = std::max(1.f, ImGui::GetContentRegionAvail().x), height = 6.f;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                        ImGui::GetColorU32(ImVec4(.10f, .16f, .21f, 1.f)), height / 2);
    float left = 0, right = width * std::clamp(fraction, 0.f, 1.f);
    if (indeterminate) {
        const float phase = static_cast<float>(ImGui::GetTime() * .55);
        const float travel = (phase - std::floor(phase)) * 1.35f - .35f;
        left = std::max(0.f, travel * width);
        right = std::min(width, (travel + .35f) * width);
    }
    if (right > left) {
        draw->AddRectFilled(ImVec2(origin.x + left, origin.y),
                            ImVec2(origin.x + right, origin.y + height), ImGui::GetColorU32(tint), height / 2);
        if (animated && !indeterminate && right > 1.f) {
            const float pulse = .08f + .07f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.f);
            draw->AddRectFilled(origin, ImVec2(origin.x + right, origin.y + height),
                                ImGui::GetColorU32(ImVec4(1, 1, 1, pulse)), height / 2);
        }
    }
    ImGui::Dummy(ImVec2(width, height));
}

// ----------------------------------------
// Tempo simulado
// ----------------------------------------

std::string civilDate(double utc) {
    static constexpr const char* months[]{"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"};
    const auto c = albert::time::civil(utc);
    char text[32];std::snprintf(text, sizeof text, "%d %s %04d", c.day, months[c.month-1], c.year);
    return text;
}

std::string civilClock(double utc) {
    const auto c = albert::time::civil(utc);
    char text[16];std::snprintf(text, sizeof text, "%02d:%02d:%02d", c.hour, c.minute, c.second);
    return text;
}

struct SpeedPreset {double rate;const char* label;};
constexpr SpeedPreset SPEEDS[]{{1.,"1x"},{60.,"1 min/s"},{3600.,"1 h/s"},{21600.,"6 h/s"},{86400.,"1 dia/s"},{604800.,"1 sem/s"}};

// Moments to visit: paused there, with the camera arriving from the side that shows it.
// Eclipse instants are greatest eclipse in TD from F. Espenak (NASA GSFC) minus the
// observed Delta T (~69 s); the others are mission records (NASA, ESA).
enum class EventView {Default, FromSun, FromEarth};
struct SimulationEvent {const char* label;const char* utc;const char* focus;EventView view;bool enhance;};
constexpr SimulationEvent EVENTS[]{
    {"Eclipse solar total, 8 abr 2024","2024-04-08 18:17:25","earth",EventView::FromSun,false},   // 18:18:34 TD
    {"Eclipse lunar total, 3 mar 2026","2026-03-03 11:33:43","moon",EventView::FromEarth,false},  // 11:34:52 TD
    {"Eclipse solar total, 2 ago 2027","2027-08-02 10:06:44","earth",EventView::FromSun,false},   // 10:07:53 TD
    {"Apollo 11 pousa na Lua, 20 jul 1969","1969-07-20 20:17:40","moon",EventView::FromEarth,false},
    {"Giotto passa pelo Halley, 14 mar 1986","1986-03-14 00:03:02","halley",EventView::Default,true},
    {"67P no perielio (Rosetta), 13 ago 2015","2015-08-13 02:03:00","67p",EventView::Default,true}};

std::string speedLabel(double rate) {
    for (const auto& speed : SPEEDS) if (std::abs(rate) == speed.rate) return speed.label;
    char text[32];std::snprintf(text, sizeof text, "%.0fx", std::abs(rate));
    return text;
}

// Distance of the simulated date from the present, in the largest whole unit.
std::string offsetLabel(double seconds) {
    const double span = std::abs(seconds);
    if (span < 60.) return "No presente";
    struct Unit {double seconds;const char *one,*many;};
    constexpr Unit units[]{{365.25*86400.,"ano","anos"},{30.4375*86400.,"mes","meses"},{86400.,"dia","dias"},{3600.,"hora","horas"},{60.,"minuto","minutos"}};
    for (const auto& unit : units) if (span >= unit.seconds) {
        const auto count = static_cast<long long>(span / unit.seconds);
        char text[64];std::snprintf(text, sizeof text, "%lld %s %s do presente", count, count == 1 ? unit.one : unit.many, seconds > 0 ? "a frente" : "antes");
        return text;
    }
    return {};
}

void drawLoadingLabel(const char* label, const char* value) {
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), right - ImGui::CalcTextSize(value).x));
    ImGui::TextDisabled("%s", value);
}

void drawDownloadStatus(DataLoader& loader) {
    const auto status = loader.progress.snapshot();
    const bool failed = !loader.error.empty(), ready = status.completed >= loadSteps();
    const bool measurable = status.total > 0;
    const float individual = ready ? 1.f : measurable ?
        std::clamp(static_cast<float>(status.received) / static_cast<float>(status.total), 0.f, 1.f) : 0.f;
    auto& animation = loader.animation;
    if (animation.stage != status.stage || animation.completed != status.completed) {
        animation.stage = status.stage;
        animation.completed = status.completed;
        animation.individual = 0;
    }
    const float blend = 1.f - std::exp(-12.f * std::min(ImGui::GetIO().DeltaTime, .1f));
    const float overall = std::clamp((status.completed + individual) / static_cast<float>(loadSteps()), 0.f, 1.f);
    animation.overall += (std::max(animation.overall, overall) - animation.overall) * blend;
    animation.individual += (individual - animation.individual) * blend;
    if (ready) animation.overall = animation.individual = 1.f;

    const auto indicator = ImGui::GetCursorScreenPos();
    const float radius = ImGui::GetTextLineHeight() * .34f;
    const ImVec2 center(indicator.x + radius + 1, indicator.y + ImGui::GetTextLineHeight() * .5f);
    const auto accent = failed ? ImVec4(1.f, .55f, .45f, 1.f) : ImVec4(.35f, .82f, .91f, 1.f);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddCircle(center, radius, ImGui::GetColorU32(ImVec4(.16f, .26f, .32f, 1.f)), 24, 1.5f);
    const float angle = static_cast<float>(ImGui::GetTime()) * 2.8f;
    draw->PathArcTo(center, radius, ready || failed ? 0.f : angle,
                    ready || failed ? 6.283185f : angle + 4.3f, 24);
    draw->PathStroke(ImGui::GetColorU32(accent), 0, 1.5f);
    ImGui::Dummy(ImVec2(radius * 2 + 2, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", failed ? "CARREGAMENTO INTERROMPIDO" : ready ? "CENA PRONTA" : "CARREGANDO A CENA");

    char percentage[32];
    std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(animation.overall * 100.f));
    drawLoadingLabel("Progresso geral", percentage);
    drawLoadingBar(animation.overall, false, !failed && !ready, accent);
    ImGui::Spacing();
    const float stageY = ImGui::GetCursorPosY();
    ImGui::TextWrapped("%s", status.stage.c_str());
    // Reserve two lines so long stage names and transfer updates do not move the card.
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), stageY + ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().ItemSpacing.y));
    if (measurable || ready)
        std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(animation.individual * 100.f));
    else std::snprintf(percentage, sizeof(percentage), "%s", failed ? "Interrompido" : "Em andamento");
    drawLoadingLabel("Etapa atual", percentage);
    drawLoadingBar(animation.individual, !measurable && !ready && !failed, !failed && !ready, accent);
    ImGui::TextDisabled("Etapa %d de %d", std::min(status.completed + 1, loadSteps()), loadSteps());
    if (!loader.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.55f, 0.45f, 1.f));
        ImGui::TextWrapped("Nao foi possivel concluir o carregamento.");
        ImGui::PopStyleColor();
        ImGui::TextWrapped(status.completed >= 7 ?
            "Verifique sua conexao com a internet e tente novamente." :
            "Verifique se a pasta assets acompanha o programa.");
        if (status.completed >= 7) ImGui::TextDisabled("Nova tentativa automatica em %.0f s", loader.retryIn());
        ImGui::Spacing();
        if (ImGui::Button("Tentar novamente", ImVec2(-1, 36))) loader.retry();
        if (ImGui::CollapsingHeader("Detalhes da falha")) {
            ImGui::TextWrapped("Etapa: %s", status.stage.c_str());
            ImGui::TextWrapped("%s", loader.error.c_str());
        }
        return;
    }
}

void applyInterfaceTheme() {
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 14.f;
    style.FrameRounding = 10.f;
    style.ChildRounding = 10.f;
    style.PopupRounding = 12.f;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(.035f,.055f,.09f,.98f);
    style.ScrollbarRounding = 8.f;
    style.WindowBorderSize = 1.f;
    style.FramePadding = ImVec2(10, 8);
    style.Colors[ImGuiCol_Border] = ImVec4(.25f, .55f, .65f, .22f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.45f, .88f, .94f, 1.f);
    style.Colors[ImGuiCol_Header] = ImVec4(.08f, .15f, .21f, 1.f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.12f, .24f, .30f, 1.f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(.16f, .32f, .40f, 1.f);
    style.WindowPadding = ImVec2(20, 20);
    style.ItemSpacing = ImVec2(8, 10);
    style.FontScaleMain = 1.f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.055f, 0.09f, 0.97f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.15f, 0.21f, 1.f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.16f, 0.27f, 0.37f, 1.f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.37f, 0.50f, 1.f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.28f, 0.75f, 0.85f, 1.f);
}

// ----------------------------------------
// Objetos visuais
// ----------------------------------------

// Retain physical coordinates as doubles. Rebase to the camera before the GPU
// conversion, so near views never subtract two astronomical float positions.
class OrbitPath : public Mesh {
public:
    using Mesh::Mesh;
    std::vector<Vec3> endpoints;
    Vec3 origin;
    double epoch = 0, halfWidth = 0, period = 0; // TDB days; period 0 when unknown.
    void rebase(const Vector3& camera,Vec3 renderOrigin) {
        auto* position=geometry()->getAttribute<float>("position");
        auto* next=geometry()->getAttribute<float>("nextPosition");
        constexpr int ends[]={0,1,0,0,1,1};
        for(size_t segment=0;segment<endpoints.size()/2;++segment) {
            const auto a=endpoints[2*segment]-origin-renderOrigin,b=endpoints[2*segment+1]-origin-renderOrigin;
            const Vector3 local[]={
                {static_cast<float>(a.x/SCALE-camera.x),static_cast<float>(a.z/SCALE-camera.y),static_cast<float>(-a.y/SCALE-camera.z)},
                {static_cast<float>(b.x/SCALE-camera.x),static_cast<float>(b.z/SCALE-camera.y),static_cast<float>(-b.y/SCALE-camera.z)}};
            for(size_t v=0;v<6;++v) {
                const auto& p=local[ends[v]];const auto& q=local[1-ends[v]];
                position->setXYZ(segment*6+v,p.x,p.y,p.z);
                next->setXYZ(segment*6+v,q.x,q.y,q.z);
            }
        }
        position->needsUpdate();next->needsUpdate();
    }
};

// Dates are relative to a nearby epoch: absolute Julian dates lose minutes
// when converted to the GPU's floats. Hermite subdivision keeps curves smooth.
std::shared_ptr<OrbitPath> makeOrbit(const std::vector<State>& samples) {
    std::vector<float> positions, times, nextPositions, nextTimes, sides;
    std::vector<Vec3> endpointsKm;
    const double epoch = (samples.front().jd + samples.back().jd) * .5;
    constexpr int endpoints[]={0,1,0,0,1,1};
    constexpr float signs[]={-1,-1,1,1,-1,1};
    for (size_t i = 1; i < samples.size(); ++i) {
        for (int part = 0; part < 8; ++part) {
            const double dates[]={std::lerp(samples[i-1].jd,samples[i].jd,part/8.),
                std::lerp(samples[i-1].jd,samples[i].jd,(part+1)/8.)};
            const Vec3 points[]={interpolate(samples,dates[0]),interpolate(samples,dates[1])};
            endpointsKm.insert(endpointsKm.end(),{points[0],points[1]});
            for(int vertex=0;vertex<6;++vertex) {
                const int end=endpoints[vertex];
                append(positions,points[end]); append(nextPositions,points[1-end]);
                times.push_back(static_cast<float>(dates[end]-epoch));
                nextTimes.push_back(static_cast<float>(dates[1-end]-epoch));
                sides.push_back(signs[vertex]*(end ? -1.f : 1.f));
            }
        }
    }
    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(std::move(positions), 3));
    geometry->setAttribute("orbitTime", FloatBufferAttribute::create(std::move(times), 1));
    geometry->setAttribute("nextPosition", FloatBufferAttribute::create(std::move(nextPositions), 3));
    geometry->setAttribute("nextTime", FloatBufferAttribute::create(std::move(nextTimes), 1));
    geometry->setAttribute("lineSide", FloatBufferAttribute::create(std::move(sides), 1));
    geometry->getAttribute<float>("position")->setUsage(DrawUsage::Dynamic);
    geometry->getAttribute<float>("nextPosition")->setUsage(DrawUsage::Dynamic);
    auto material = albert::orbitMaterial(static_cast<float>((samples.back().jd-samples.front().jd)*.14));
    auto path=std::make_shared<OrbitPath>(geometry,material);
    path->endpoints=std::move(endpointsKm);
    path->epoch=epoch;
    path->halfWidth=(samples.back().jd-samples.front().jd)*.5;
    // Turn about the orbit normal at the path centre (prograde by construction).
    const auto& middle=samples[samples.size()/2];
    const std::array<double,3> normal{middle.position.y*middle.velocity.z-middle.position.z*middle.velocity.y,
        middle.position.z*middle.velocity.x-middle.position.x*middle.velocity.z,middle.position.x*middle.velocity.y-middle.position.y*middle.velocity.x};
    std::vector<albert::PathPoint> points;points.reserve(samples.size());
    for(const auto& sample:samples)points.push_back({sample.jd,sample.position.x,sample.position.y,sample.position.z});
    path->period=albert::revolutionDays(points,normal);
    path->frustumCulled=false;
    path->renderOrder=1;
    return path;
}

void updateOrbitTime(const std::shared_ptr<OrbitPath>& path, double now) {
    auto* material = path->material()->as<ShaderMaterial>();
    material->uniforms.at("present").setValue(static_cast<float>(albert::pathPresent(now - path->epoch, path->halfWidth, path->period)));
}

// ----------------------------------------
// Animacao da camera
// ----------------------------------------

float smoothstep(float t) {
    t = std::clamp(t, 0.f, 1.f);
    // Quintic easing has zero velocity and acceleration at both ends.
    return t*t*t*(t*(t*6.f - 15.f) + 10.f);
}

Vector3 lerpVector(Vector3 a, Vector3 b, float t) {
    return {
        std::lerp(a.x, b.x, t),
        std::lerp(a.y, b.y, t),
        std::lerp(a.z, b.z, t)
    };
}

// Spherical interpolation avoids speed changes and zero-length offsets when
// switching between opposite viewing directions.
Vector3 arcDirection(Vector3 a, Vector3 b, float t) {
    a.normalize(); b.normalize();
    const float dot = std::clamp(a.dot(b), -1.f, 1.f);
    if (dot > .999f) return lerpVector(a, b, t).normalize();
    if (dot < -.999f) {
        Vector3 axis = std::abs(a.y) < .9f ? Vector3(0,1,0) : Vector3(1,0,0);
        axis.cross(a).normalize();
        return a.multiplyScalar(std::cos(static_cast<float>(PI)*t))
            .add(axis.multiplyScalar(std::sin(static_cast<float>(PI)*t))).normalize();
    }
    const float angle = std::acos(dot), divisor = std::sin(angle);
    return a.multiplyScalar(std::sin((1-t)*angle)/divisor)
        .add(b.multiplyScalar(std::sin(t*angle)/divisor)).normalize();
}

struct CameraTransition {
    bool active = false;
    bool preserveDirection = false;
    std::optional<float> arrivalDistance; // Overrides the target's default framing.
    float elapsed = 0;
    float duration = 1.f;

    Vector3 startTarget;
    Vector3 startOffset;
    Vector3 arrivalDirection;

    Focus destination = targets::Earth;
};

// ----------------------------------------
// Aplicacao
// ----------------------------------------

} // namespace

int albert::runRenderer(int argc, char** argv) {
    bool smokeTest = false,demo=false;
    std::optional<unsigned> rpcPort;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--smoke-test") {
                smokeTest = true;
            } else if (argument == "--renderer=webgpu") { // The only renderer (OpenGL was removed); kept for scripts.
            } else if (argument == "--rpc" || argument.starts_with("--rpc-port=")) {
                size_t consumed=0;const auto text=argument=="--rpc" ? std::string("50051") : argument.substr(11);
                const auto port=std::stoul(text,&consumed);
                if(consumed!=text.size() || port==0 || port>65535) throw std::invalid_argument("Porta gRPC invalida");
                rpcPort=static_cast<unsigned>(port);
#ifndef ALBERT_HAS_RPC
                throw std::runtime_error("Este build foi compilado sem gRPC (ALBERT_RPC=OFF)");
#endif
            } else if (argument == "--demo") { demo=true;
            } else if (argument == "--help") {
                std::cout << "Albert Engine\n"
                          << "  --smoke-test  Carrega dados reais, renderiza 5 quadros e encerra\n"
                          << "  --rpc         Ativa gRPC local na porta 50051 (ou --rpc-port=N)\n"
                          << "  --demo        Adiciona um objeto de exemplo simulado\n"
                          << "  --help        Mostra esta ajuda\n";
                return 0;
            } else {
                throw std::runtime_error("Argumento desconhecido: " + argument);
            }
        }

        // Declared before the futures: cleanup runs after all HTTP workers,
        // including when initialization or rendering throws an exception.
        struct CurlLifetime {
            CurlLifetime() {
                if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
                    throw std::runtime_error("CURL indisponivel");
                }
            }
            ~CurlLifetime() { curl_global_cleanup(); }
        } curlLifetime;
        auto& timeService=albert::time::clock();
        // Astronomy, objects and the API follow the simulated instant, read once per frame.
        auto& simulation=albert::time::simulation();
        double simulatedUtc=simulation.read().utcSeconds;
        auto simulatedTDB=[&]{return jdTDB(2440587.5+simulatedUtc/86400.);};

        fs::path countries =
            albert::assetDirectory() / "countries.geojson";

        std::function<void()> drawInterface;
        WindowSize initialSize(1280,800); // ALBERT_WINDOW=1920x1080: another first size (frame measurements at a set resolution).
        if(const char* size=std::getenv("ALBERT_WINDOW")){int w=0,h=0;if(std::sscanf(size,"%dx%d",&w,&h)==2&&w>=320&&h>=240&&w<=7680&&h<=4320)initialSize=WindowSize(w,h);}
        std::unique_ptr<albert::Display> display;
        try{display=std::make_unique<albert::Display>("Albert Engine",initialSize,albert::assetDirectory()/"branding/albert.png",[&]{
            applyInterfaceTheme();
            if (drawInterface) drawInterface();
        });}
        catch(const std::exception& e){ // No WebGPU adapter at all, not even a software one (WARP, lavapipe).
            throw std::runtime_error(std::string("WebGPU indisponivel: ")+e.what()+". O Albert precisa de Vulkan, Direct3D 12 ou Metal (ou de um adaptador por software).");
        }
        auto& graphics=*display;
        // Light distributions carry only the catalogue: download what is missing,
        // essential first. Complete libraries never start this service.
        std::unique_ptr<albert::AssetDownloads> assetDownloads;
        if(const auto catalog=albert::assetDirectory()/"catalog.json";fs::is_regular_file(catalog)){
            bool complete=false;try{complete=albert::visualAssetsComplete();}catch(const std::exception&){}
            if(!complete){assetDownloads=std::make_unique<albert::AssetDownloads>(albert::readAssetCatalog(catalog),albert::AssetDownloads::Options{});assetDownloads->start();}
        }
        DataLoader downloads(countries,assetDownloads.get());
        std::ifstream fontFile(albert::assetDirectory() / "fonts" / "Inter.ttf", std::ios::binary);
        std::vector<char> fontBytes(std::istreambuf_iterator<char>(fontFile), {});
        if (!fontBytes.empty()) {
            ImFontConfig fontConfig;
            fontConfig.FontDataOwnedByAtlas = false;
            ImGui::GetIO().Fonts->AddFontFromMemoryTTF(fontBytes.data(), static_cast<int>(fontBytes.size()), 16.f, &fontConfig);
        }
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        auto drawLoading = [&](float opacity, float lift = 0.f) {
            const auto display = ImGui::GetIO().DisplaySize;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
            ImGui::SetNextWindowPos(ImVec2(display.x / 2, display.y / 2 - lift),
                                   ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(480.f, display.x - 24.f), 0));
            ImGui::Begin("Carregamento", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("ALBERT ENGINE");
            ImGui::TextDisabled("PREPARANDO A SIMULACAO");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const auto timing=timeService.status();
            if(timing.synchronizing) ImGui::TextDisabled("Conferindo horario na internet...");
            else if(timing.quality==albert::time::Quality::Local) ImGui::TextColored(ImVec4(1,.7f,.4f,1),"Horario local: sem confirmacao externa");
            else ImGui::TextDisabled("Horario conferido na internet");
            drawDownloadStatus(downloads);
            ImGui::Spacing();
            ImGui::TextWrapped("%s",assetDownloads?"Dados visuais essenciais baixados ao abrir; as demais resolucoes continuam em segundo plano. Buscando as posicoes reais dos corpos.":"Imagens incluidas no programa. Buscando as posicoes reais dos corpos disponiveis.");
            if (ImGui::Button("Sair", ImVec2(-1, 32))) graphics.close();
            ImGui::End();
            ImGui::PopStyleVar();
        };
        drawInterface = [&] { drawLoading(1.f); };
        bool initialDownloadsStarted=false;
        const double initialClockWait=albert::time::monotonicSeconds();
        std::optional<LoadedData> loaded;
        while (graphics.isOpen() && !loaded) {
            if(!initialDownloadsStarted && (!timeService.status().synchronizing || albert::time::monotonicSeconds()-initialClockWait>12.)) {
                downloads.start(true,true);initialDownloadsStarted=true;
                const auto timing=timeService.status();
                std::cout<<"Tempo: "<<albert::time::qualityName(timing.quality)<<" ("<<timing.source<<"), incerteza "<<timing.uncertaintySeconds<<" s"<<std::endl;
            }
            loaded = downloads.poll();
            if (smokeTest && !downloads.error.empty()) throw std::runtime_error(downloads.error);
            if (loaded) break;
            if (!graphics.frame([&] {
                graphics.clear();
                graphics.ui();
            })) break;
        }
        if (!loaded || !graphics.isOpen()) return smokeTest ? 1 : 0;
        auto loadedAssets = std::move(loaded->assets);
        auto borders = std::move(loaded->borders);
        auto ephemerides = std::move(loaded->ephemerides);
        auto orbits = std::move(loaded->orbits);
        const auto initialTimeGeneration=loaded->timeGeneration;
        const auto initialSimulationGeneration=loaded->simulationGeneration;
        loaded.reset();

        auto scene = Scene::create();
        scene->background = Color(0x010208);

        auto camera = PerspectiveCamera::create(
            60, graphics.aspect(), 0.01f, 100000.f
        );

        camera->position.set(0, 0, 3.5f);

        OrbitControls controls{*camera, graphics.events()};
        // The second view (picture-in-picture): the other flight beside the one followed, from the same side, at its framing.
        auto insetCamera=PerspectiveCamera::create(60,16.f/9.f,.01f,100000.f);std::string insetId;unsigned long long insetTexture=0;unsigned insetFrames=0;
        bool insetEngines=false;std::string insetTestId;std::chrono::steady_clock::time_point insetTestUntil{};

        controls.enableDamping = true;
        controls.dampingFactor = CAMERA_EASING;
        controls.rotateSpeed = 0.6f;
        controls.zoomSpeed = 0.8f;
        controls.enablePan = false;
        controls.minDistance = 1.01f;
        controls.maxDistance = 65000.f;
        controls.minPolarAngle = .035f; // 2° off the orbit's axis: straight above or below, the view's roll is undefined and it shook.
        controls.maxPolarAngle = static_cast<float>(PI) - .035f;

        auto stars = albert::makeStarField(albert::assetDirectory() / "stars.csv", simulatedTDB());
        scene->add(stars);
        auto solidAssets=std::move(loadedAssets->surfaces);
        albert::PlanetVisuals planets(*scene, std::move(*loadedAssets));
        albert::SolidBodies solids(*scene,std::move(solidAssets));
        albert::CometVisuals comets(*scene);
        auto earth = planets.earth;
        auto moon = planets.moon;
        albert::EclipseVisuals eclipses(earth,moon,planets.sun);
        auto sun = planets.sun;
        albert::VisualSettings visualSettings;
        albert::core::Scene registry;
        albert::core::Bridge commands;
        albert::catalog::Manager catalog(commands,albert::providers::horizons(),{},{},[&simulation]{return simulation.read();});
#ifdef ALBERT_HAS_RPC
        std::unique_ptr<albert::rpc::Server> rpc;
        bool broadcasting=false; // The broadcast's panels were shown last frame: the corner views make room for them.
        // The flight's sound at the camera: muted until "Som do voo" is ticked (the device opens then); its emissions over
        // the last ten minutes and the listener's last place.
        bool soundOn=true;std::unique_ptr<albert::Sound> sound; // On unless unticked (ALBERT_AUDIO=0 keeps a run silent).
        struct Heard {std::vector<albert::physics::acoustics::Emission> emissions;std::size_t booms=0;bool blasted=false;
            struct Tail {albert::physics::acoustics::Emission emission;bool ignition=false,active=false;};std::array<Tail,256> tails{};
            std::vector<albert::physics::acoustics::Emission> pulse=std::vector<albert::physics::acoustics::Emission>(2); // Reused propagation scratch, even after the steady roar has stopped.
            albert::physics::acoustics::RocketNoise reference{};std::uint64_t serial=0,instance=0;double playedUtc=-1e30;
        };std::map<std::string,Heard> heardFlights; // By scene id.
        std::optional<std::pair<double,albert::physics::V3>> lastListener;albert::physics::V3 listenerVelocity;
        const bool soundStats=std::getenv("ALBERT_SOUND_STATS")!=nullptr;auto soundReport=std::chrono::steady_clock::now();
#endif
        albert::SceneObjects objects(*scene);
        albert::VectorLayers vectorLayers(*scene);
        objects.bind("earth",earth);objects.bind("moon",moon);objects.bind("sun",sun);
        for(const auto& [id,node]:solids.nodes()){if(const auto* body=albert::core::naturalBody(id))registry.include(*body);objects.bind(id,node,solids.picker(id));}
        albert::core::includeLocalSystem(registry);
        albert::DebrisFields debris(*scene);
        albert::core::Vector sceneOrigin;
        Focus originId="earth";
        if(demo){albert::core::Object object;object.id="demo";object.name="Exemplo (simulado)";object.source="Synthetic example";
            object.appearance.sizeMeters={200000,200000,200000};object.samples.push_back({simulatedUtc,{7500000,0,0}});registry.create(std::move(object));}

        albert::CloudSync cloudSync(albert::dataDirectory() / "clouds", albert::assetDirectory() / "cloud-colormap.json");
        auto bordersObject = std::make_shared<albert::BorderLines>(borders);
        scene->add(bordersObject);

        // Iluminacao
        auto sunlight = DirectionalLight::create();
        sunlight->color = Color(0xFFF6E9);
        sunlight->intensity = 2.4f;
        sunlight->castShadow = true;
        sunlight->shadow->bias = -0.00005f;
        sunlight->shadow->normalBias = 0.001f;
        auto lightTarget = Object3D::create();
        sunlight->setTarget(*lightTarget);
        scene->add(lightTarget);
        scene->add(sunlight);

        auto ambient = AmbientLight::create();
        ambient->intensity = 0.003f;
        scene->add(ambient);

        // Orbitas baseadas em efemerides
        auto moonOrbit = makeOrbit(orbits.moon);

        auto earthOrbit = makeOrbit(orbits.earth);

        scene->add(moonOrbit);
        scene->add(earthOrbit);
        std::map<std::string,std::shared_ptr<OrbitPath>> bodyOrbits;
        auto replaceBodyOrbits=[&](const Orbits& values){
            std::map<std::string,std::shared_ptr<OrbitPath>> next;
            for(const auto& [id,samples]:values.bodies)next[id]=makeOrbit(samples);
            for(const auto& [id,path]:bodyOrbits){(void)id;scene->remove(*path);}
            bodyOrbits=std::move(next);for(const auto& [id,path]:bodyOrbits){(void)id;scene->add(path);}
        };
        replaceBodyOrbits(orbits);

        albert::Cache cache(albert::dataDirectory());
        albert::RegionalImagery regional;
        albert::MemoryReclaimer memory;
        auto nextCacheCheck = std::chrono::steady_clock::now();
        bool showOrbits = true,showDebris=true;
        struct LocalPath {std::shared_ptr<OrbitPath> path;double epoch,period;};
        std::map<std::string,LocalPath> localPaths;
        for(const auto& body:albert::core::localOrbits()){
            const double now=simulatedTDB(),period=body.period/86400.;
            double anomaly=std::fmod(body.meanAnomaly+(now-body.epoch)*86400.*body.meanMotion,360.);if(anomaly<0)anomaly+=360.;
            const double beginning=now-anomaly/360.*period,center=beginning+period*.5;std::vector<State> samples;
            // Sample eccentric anomaly: highly eccentric comet paths need many
            // more time samples near perihelion than near their distant aphelion.
            for(int i=0;i<=128;++i){const double eccentric=2.*PI*i/128.;
                const double date=beginning+(eccentric-body.e*std::sin(eccentric))/(2.*PI)*period;const auto state=albert::core::localState(body,date);
                samples.push_back({date,{state.positionMeters.x/1000.,state.positionMeters.y/1000.,state.positionMeters.z/1000.},{state.velocityMetersPerSecond.x/1000.,state.velocityMetersPerSecond.y/1000.,state.velocityMetersPerSecond.z/1000.}});}
            auto path=makeOrbit(samples);scene->add(path);localPaths.emplace(std::string(body.id),LocalPath{path,center,period});
        }
        float orbitVisibility=1.f,borderVisibility=0.f;
        std::map<std::string,float> secondaryVisibility;
        std::string refreshError;

        Focus focus = targets::Earth;
        CameraTransition transition;
        bool automaticOrbit=false;
        float automaticOrbitAge=0.f;

        Vector3 sunPosition{0, 0, 0};
        Vector3 moonPosition{0, 0, 0};
        Vector3 earthPosition{0, 0, 0};

        auto systemView = [&](const Focus& f){return f==targets::Overview||f==targets::Inner;};
        auto targetId = [&](const Focus& f) -> Focus {return systemView(f) ? targets::Sun : f==targets::EarthMoon ? targets::Earth : f;};
        auto focusName = [&](const Focus& f) -> const char* {return f==targets::Inner ? "Planetas internos" : f==targets::Overview ? "Sistema solar" : f==targets::EarthMoon ? "Terra e Lua" : registry.get(f).name.c_str();};
        auto targetFor = [&](const Focus& f) {return albert::renderPosition(registry.evaluate(targetId(f),simulatedUtc).state.positionMeters,sceneOrigin);};
        auto distanceFor = [&](const Focus& f) -> float {
            if(f==targets::EarthMoon)return 180.f;
            if(f==targets::Inner)return static_cast<float>(4.2*149597870700./albert::core::earthRadiusMeters/std::sin(std::atan(std::tan(camera->fov*PI/360.)*std::min(1.f,camera->aspect))));
            if(f==targets::Overview){
                double extent=0;const double now=simulatedUtc;
                for(const auto& [id,object]:registry.objects())if(object.visible&&!albert::core::localOrbit(id)){const auto p=registry.evaluate(id,now).state.positionMeters;extent=std::max(extent,std::hypot(p.x,p.y,p.z)+object.radiusMeters);}
                const double angle=std::atan(std::tan(camera->fov*PI/360.)*std::min(1.f,camera->aspect));
                return static_cast<float>(std::max(65000.,extent/albert::core::earthRadiusMeters/std::sin(angle)*1.15));
            }
            return static_cast<float>(registry.get(f).defaultDistanceMeters/albert::core::earthRadiusMeters);
        };
        auto radiusFor = [&](const Focus& f) -> float {return f.front()=='@' ? 0.f : static_cast<float>(registry.get(f).radiusMeters/albert::core::earthRadiusMeters);};
        auto minimumFor = [&](const Focus& f) -> float {
            if(f.front()=='@')return .05f;
            float minimum=static_cast<float>(registry.get(f).minDistanceMeters/albert::core::earthRadiusMeters);
            if(const auto* surface=albert::core::surfaceBody(f);surface&&surface->shapeRadiusMeters>0)minimum=std::max(minimum,static_cast<float>(surface->shapeRadiusMeters/albert::core::earthRadiusMeters)*1.05f);
            if(const auto* surface=albert::core::surfaceBody(f);surface&&surface->axesMeters[0]>0){const auto& axes=surface->axesMeters;minimum=std::max(minimum,static_cast<float>(*std::max_element(axes.begin(),axes.end())/albert::core::earthRadiusMeters)*1.04f);}
            if(albert::artisticRock(f)&&objects.nodes().contains(f)){
                const auto& node=objects.nodes().at(f);
                if(node->geometry()->boundingSphere){const auto& bounds=*node->geometry()->boundingSphere;minimum=std::max(minimum,(bounds.radius+bounds.center.length())*node->scale.x*1.04f);}
            }
            return minimum;
        };

        // Ajustes, kept in settings.json across runs (the sound always starts muted). The volumetric clouds and the ocean's
        // waves are the costly effects: on by default, a weaker machine turns them off.
        struct Preferences {bool volumetricClouds=true,oceanWaves=true,antialias=true,vsync=true,autoRotate=true,invertY=false,inset=true,broadcast=true,playground=true,deluge=true;int frameCap=60;float fov=60,uiScale=1,sensitivity=1,volume=.8f;
            bool thirdBodies=true,relativity=true,flightAir=true,wind=true,solarPressure=true;float turbulence=0;} preferences; // The last six: the Fisica page's defaults for new flights.
        const auto preferencesFile=albert::dataDirectory()/"settings.json";
        try{std::ifstream in(preferencesFile);if(in){const auto j=json::parse(in);auto& p=preferences;auto& v=visualSettings;
            p.volumetricClouds=j.value("volumetric_clouds",p.volumetricClouds);p.oceanWaves=j.value("ocean_waves",p.oceanWaves);p.antialias=j.value("antialias",p.antialias);
            p.vsync=j.value("vsync",p.vsync);p.autoRotate=j.value("auto_rotate",p.autoRotate);p.invertY=j.value("invert_y",p.invertY);p.inset=j.value("flight_inset",p.inset);p.deluge=j.value("deluge_effects",p.deluge);p.broadcast=j.value("broadcast_panels",p.broadcast);p.playground=j.value("interference_panel",p.playground);p.frameCap=std::clamp(j.value("frame_cap",p.frameCap),0,240);
            p.fov=std::clamp(j.value("field_of_view",p.fov),35.f,90.f);p.uiScale=std::clamp(j.value("interface_scale",p.uiScale),.8f,1.6f);
            p.sensitivity=std::clamp(j.value("mouse_sensitivity",p.sensitivity),.25f,3.f);p.volume=std::clamp(j.value("volume",p.volume),0.f,1.f);
            p.thirdBodies=j.value("physics_third_bodies",p.thirdBodies);p.relativity=j.value("physics_relativity",p.relativity);p.flightAir=j.value("physics_atmosphere",p.flightAir);
            p.wind=j.value("physics_wind",p.wind);p.solarPressure=j.value("physics_solar_pressure",p.solarPressure);p.turbulence=std::clamp(j.value("physics_turbulence",p.turbulence),0.f,10.f);
            v.quality=std::clamp(j.value("quality",v.quality),0,1);v.shadows=j.value("shadows",v.shadows);v.atmosphere=j.value("atmosphere",v.atmosphere);
            v.terrain=j.value("terrain",v.terrain);v.clouds=j.value("live_clouds",v.clouds);v.nightLights=j.value("city_lights",v.nightLights);v.borders=j.value("borders",v.borders);
            showOrbits=j.value("orbits",showOrbits);showDebris=j.value("debris",showDebris);}}
        catch(const std::exception&){} // A damaged preference never prevents startup.
        auto savePreferences=[&]{try{const auto& p=preferences;const auto& v=visualSettings;std::filesystem::create_directories(preferencesFile.parent_path());std::ofstream out(preferencesFile);
            out<<json{{"volumetric_clouds",p.volumetricClouds},{"ocean_waves",p.oceanWaves},{"antialias",p.antialias},{"vsync",p.vsync},{"auto_rotate",p.autoRotate},{"invert_y",p.invertY},{"flight_inset",p.inset},{"deluge_effects",p.deluge},{"broadcast_panels",p.broadcast},{"interference_panel",p.playground},{"frame_cap",p.frameCap},
                {"field_of_view",p.fov},{"interface_scale",p.uiScale},{"mouse_sensitivity",p.sensitivity},{"volume",p.volume},{"quality",v.quality},{"shadows",v.shadows},
                {"atmosphere",v.atmosphere},{"terrain",v.terrain},{"live_clouds",v.clouds},{"city_lights",v.nightLights},{"borders",v.borders},{"orbits",showOrbits},{"debris",showDebris},
                {"physics_third_bodies",p.thirdBodies},{"physics_relativity",p.relativity},{"physics_atmosphere",p.flightAir},{"physics_wind",p.wind},{"physics_solar_pressure",p.solarPressure},{"physics_turbulence",p.turbulence}}.dump(2)<<'\n';}
            catch(const std::exception&){}};
        // The Fisica page's switches as the flight API's defaults (flights created without them).
        auto applyPhysics=[&]{
#ifdef ALBERT_HAS_RPC
            if(!rpc)return;
            albert::v1::PhysicsSettings m;const auto& p=preferences;
            m.set_third_bodies(p.thirdBodies);m.set_relativity(p.relativity);m.set_atmosphere(p.flightAir);m.set_wind(p.wind);m.set_solar_radiation_pressure(p.solarPressure);
            m.set_turbulence_meters_per_second(p.turbulence);rpc->physicsDefaults(m);
#endif
        };
        // Free camera (Ajustes, or F): flies from the current view; dragging looks, W/A/S/D, Space/E and Ctrl/Q move, Shift is
        // five times faster, the wheel scales the speed, which follows the height over the nearest surface.
        bool freeCamera=false;float freeSpeed=1.f;
        // Wind waves: the wind they follow (m/s toward east and north), their clock, and the tangent frame they are drawn in.
        // cloudShare: the volumetric layer's share of the clouds, animated; cloudVolume: as drawn.
        float seaWindEast=-7.f,seaWindNorth=0,cloudShare=0,cloudVolume=0;double seaClock=0;struct SeaFrame {albert::physics::V3 position,east,north;};std::optional<SeaFrame> seaOrigin;
        // arrival: direction from the target to the arriving camera, overriding the default.
        auto setFocus = [&](Focus next, bool resetZoom = false, std::optional<Vector3> arrival = {}, std::optional<float> distance = {}) {
            registry.get(targetId(next));
            if (focus == next && !transition.active && !resetZoom && !arrival && !distance) return;
            // Local surfaces (moons and small bodies) share the "moons" download containers.
            if(assetDownloads){const auto id=targetId(next);assetDownloads->prioritize(id!=targets::Moon&&albert::core::surfaceBody(id)&&!albert::core::naturalBody(id)?std::string("moons"):id);}

            // Clear residual control inertia without moving the displayed camera.
            const Vector3 displayedPosition=camera->position,displayedTarget=controls.target;
            automaticOrbit=false;automaticOrbitAge=0.f;controls.autoRotate=false;
            controls.dampingFactor = 1.f;controls.update();
            camera->position.copy(displayedPosition);controls.target.copy(displayedTarget);
            transition.active = true;
            transition.preserveDirection = resetZoom;
            transition.elapsed = 0.f;
            transition.startTarget.copy(controls.target);
            transition.startOffset.copy(camera->position);
            transition.startOffset.sub(controls.target);
            transition.destination = next;
            transition.arrivalDistance = distance;
            transition.arrivalDirection.copy(camera->position).sub(targetFor(next));
            if(transition.arrivalDirection.lengthSq()<1e-12f)transition.arrivalDirection.copy(transition.startOffset);
            if(next==targets::Overview||next==targets::Inner||next==targets::EarthMoon)transition.arrivalDirection.set(.2f,.9f,.7f);
            if(arrival&&arrival->lengthSq()>1e-12f){transition.arrivalDirection.copy(*arrival);transition.preserveDirection=false;}
            transition.arrivalDirection.normalize();
            const float scaleChange = std::abs(std::log(distance.value_or(distanceFor(next)) / std::max(1e-9f, transition.startOffset.length())));
            const float travel=transition.startTarget.distanceTo(targetFor(next));
            transition.duration = albert::flightDuration(scaleChange,
                travel/std::max(.01f,radiusFor(next)),resetZoom);
            if(!resetZoom)transition.duration*=1.15f;
            freeCamera=false;camera->up.set(0,1,0); // A programmed flight ends the free camera.

            focus = next;registry.cameraChanged();
        };
        // Into the free camera from the current view; out of it, a flight back to the focus from where the eye is.
        auto toggleFreeCamera=[&](bool on){
            if(on==freeCamera)return;
            if(on){transition.active=false;automaticOrbit=false;automaticOrbitAge=0.f;controls.autoRotate=false;freeCamera=true;return;}
            freeCamera=false;camera->up.set(0,1,0);Vector3 back=camera->position;back.sub(targetFor(focus));setFocus(focus,false,back);
        };

        bool refreshing = false;
        bool refreshingOrbits = false;
        bool approximatePositions = false; // Simulated instant outside the JPL tables.
        // Epoch of the offline two-body elements (one epoch for the whole catalogue).
        const double localElementsUtc = (albert::core::localOrbits().front().epoch - 2440587.5) * 86400.;
        bool cloudsCurrent = true; // Simulated date within a day of the observations.
        // Time panel: last chosen speed, kept while paused or reversed.
        double chosenSpeed = 1.;
        bool backwards = false;
        std::array<char,32> dateText{};
        std::string dateError;
        // A chosen event waits for positions at its date before flying there.
        struct PendingEvent {const SimulationEvent* event;std::uint64_t generation;};
        std::optional<PendingEvent> pendingEvent;
        std::vector<fs::path> pendingCaptures;
        std::unique_ptr<FrameStream> frameStream;if(const char* p=std::getenv("ALBERT_STREAM"))frameStream=std::make_unique<FrameStream>(p);
        const double streamFps=std::getenv("ALBERT_STREAM_FPS")?std::clamp(std::atof(std::getenv("ALBERT_STREAM_FPS")),1.,60.):20.;std::chrono::steady_clock::time_point streamAt{};
        std::optional<albert::core::FlightTelemetry> flightTelemetry; // From the gRPC flight API.
        auto flightTelemetryAt=std::chrono::steady_clock::now(); // Its latest sample (the sea's wind ignores a stale one).
        bool menuOpen = false;
        float menuProgress = 0.f;
        float resetZoomProgress = 0.f;
        float detailLoaderAge=0.f,detailLoaderAlpha=0.f;
        int menuPage = 0;

        std::array<char,96> focusSearch{};bool uiPopupWasOpen=false;
        float zoomTarget = 3.5f, zoomDisplayed = 3.5f;
        // Start with the day/terminator visible, independent of UTC season.
        Vector3 initialSun = graphical(interpolate(ephemerides.sun, simulatedTDB()));
        initialSun.normalize();
        Vector3 tangent(-initialSun.z, 0, initialSun.x);
        tangent.normalize();
        camera->position.copy(initialSun).multiplyScalar(1.8f).add(tangent.multiplyScalar(3.f));
        camera->lookAt(controls.target);
        int appliedQuality = -1;
        Focus lastRegionalFocus=focus;bool lastDetailQuality=visualSettings.quality;std::optional<albert::GeoRegion> siteRegion; // The launch site's imagery box, kept while its vehicle stays in it.
        std::uint64_t assetGeneration=albert::visualAssetGeneration();albert::DetailRefresh detailRefresh;
        float assetIndicatorAlpha=0.f,assetCompleteAge=0.f;

        drawInterface = [&] {
            const auto display = ImGui::GetIO().DisplaySize;
            const bool earthControls=focus==targets::Earth || focus==targets::EarthMoon;
            std::array<std::pair<ImVec2,ImVec2>,5> protectedPanels;
            size_t panelCount=0;
            auto protectPanel=[&]{const auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();if(panelCount<protectedPanels.size())protectedPanels[panelCount++]={p,ImVec2(p.x+s.x,p.y+s.y)};};
            auto covered=[&](ImVec2 lo,ImVec2 hi){for(size_t i=0;i<panelCount;++i){const auto& [a,b]=protectedPanels[i];if(lo.x<b.x&&hi.x>a.x&&lo.y<b.y&&hi.y>a.y)return true;}return false;};
            // Consume wheel events inside ImGui's frame, before EndFrame clears them.
            const auto& input=ImGui::GetIO();
            if(!input.WantCaptureMouse&&(ImGui::IsMouseDragging(ImGuiMouseButton_Left)||ImGui::IsMouseDragging(ImGuiMouseButton_Right)||ImGui::IsMouseDragging(ImGuiMouseButton_Middle)||input.MouseWheel!=0.f||input.MouseWheelH!=0.f)){
                automaticOrbit=false;controls.autoRotate=false;
            }
            constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
            if(freeCamera&&!menuOpen&&!uiPopupWasOpen&&ImGui::IsKeyPressed(ImGuiKey_Escape,false))toggleFreeCamera(false);
            else if(!ImGui::GetIO().WantCaptureKeyboard&&ImGui::IsKeyPressed(ImGuiKey_F,false))toggleFreeCamera(!freeCamera);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !uiPopupWasOpen) {
                if (menuOpen) menuOpen = false;
            }
            const float margin = 16.f;
            const float barWidth = std::min(420.f, display.x - margin * 2);
            ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(barWidth, 58));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            ImGui::Begin("Navegacao", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            protectPanel();
            const auto menuPosition = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##Menu", ImVec2(38, 38))) menuOpen = !menuOpen;
            auto* draw = ImGui::GetWindowDrawList();
            const ImU32 icon = ImGui::GetColorU32(ImVec4(180/255.f, 230/255.f, 240/255.f, 1.f));
            for (int line = 0; line < 3; ++line) {
                const float y = menuPosition.y + 12 + line * 7;
                draw->AddLine(ImVec2(menuPosition.x + 10, y), ImVec2(menuPosition.x + 28, y), icon, 1.8f);
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextColored(ImVec4(.6f,.88f,.95f,1), "A L B E R T");

            ImGui::EndGroup();
            ImGui::SameLine();
            const ImVec2 pickerPosition=ImGui::GetCursorScreenPos();const float pickerButtonWidth=ImGui::GetContentRegionAvail().x;
            const bool openPicker=ImGui::Button("##Objeto atual",ImVec2(-1,38));
            auto* pickerDraw=ImGui::GetWindowDrawList();const ImU32 pickerColor=ImGui::GetColorU32(ImGuiCol_Text);
            pickerDraw->PushClipRect(ImVec2(pickerPosition.x+12,pickerPosition.y),ImVec2(pickerPosition.x+pickerButtonWidth-30,pickerPosition.y+38),true);
            pickerDraw->AddText(ImVec2(pickerPosition.x+12,pickerPosition.y+(38-ImGui::GetFontSize())*.5f),pickerColor,focusName(focus));pickerDraw->PopClipRect();
            const float arrowX=pickerPosition.x+pickerButtonWidth-18,arrowY=pickerPosition.y+19;
            pickerDraw->AddLine(ImVec2(arrowX-4,arrowY-2),ImVec2(arrowX,arrowY+2),pickerColor,1.5f);pickerDraw->AddLine(ImVec2(arrowX,arrowY+2),ImVec2(arrowX+4,arrowY-2),pickerColor,1.5f);
            if(openPicker){focusSearch.fill(0);ImGui::OpenPopup("Selecionar objeto");}
            const float pickerWidth=std::min(390.f,display.x-32.f);
            ImGui::SetNextWindowPos(ImVec2(std::clamp(pickerPosition.x,16.f,std::max(16.f,display.x-pickerWidth-16.f)),pickerPosition.y+46),ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(pickerWidth,0),ImVec2(pickerWidth,std::max(120.f,display.y-90.f)));
            if(ImGui::BeginPopup("Selecionar objeto")){
                protectPanel();
                ImGui::TextDisabled("EXPLORAR O SISTEMA SOLAR");
                if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##Buscar objeto","Planeta, lua, asteroide ou cometa",focusSearch.data(),focusSearch.size());
                struct Match {const albert::core::Object* object;int score;double distance;};
                std::array<std::vector<Match>,7> groups;
                const double searchTime=simulatedUtc;
                for(const auto& [id,object]:registry.objects()){
                    const int score=albert::core::matchScore(focusSearch.data(),object.name,id);
                    if(score>=0)groups[static_cast<unsigned>(albert::core::category(id))].push_back({&object,score,albert::core::solarOrder(registry,id,searchTime)});
                }
                std::size_t count=0,sections=0;
                for(auto& group:groups){std::sort(group.begin(),group.end(),[](const Match& a,const Match& b){if(a.score!=b.score)return a.score>b.score;if(a.distance!=b.distance)return a.distance<b.distance;return a.object->id<b.object->id;});count+=group.size();sections+=!group.empty();}
                std::array<unsigned,7> order{0,1,2,3,4,5,6};
                const float listHeight=std::max(64.f,std::min({380.f,std::max(64.f,display.y-260.f),float(count)*38.f+float(sections)*30.f}));
                ImGui::BeginChild("Lista de objetos",ImVec2(0,listHeight));
                if(!count)ImGui::TextDisabled("Nenhum objeto encontrado. Tente outro nome.");
                if(focusSearch[0]){
                    std::vector<Match> ranked;for(const auto& group:groups)ranked.insert(ranked.end(),group.begin(),group.end());
                    std::sort(ranked.begin(),ranked.end(),[](const Match& a,const Match& b){if(a.score!=b.score)return a.score>b.score;if(a.distance!=b.distance)return a.distance<b.distance;return a.object->id<b.object->id;});
                    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(ranked.size()),38.f);
                    while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){const auto& object=*ranked[i].object;
                        ImGui::PushID(object.id.c_str());const ImVec2 row=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
                        if(ImGui::Selectable("##Resultado",focus==object.id,ImGuiSelectableFlags_None,ImVec2(0,28))){setFocus(object.id);ImGui::CloseCurrentPopup();}
                        const char* kind=albert::core::categoryName(albert::core::category(object.id));const float badge=ImGui::CalcTextSize(kind).x;
                        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(row,ImVec2(row.x+width-badge-20,row.y+28),true);
                        draw->AddText(ImVec2(row.x,row.y+6),ImGui::GetColorU32(ImGuiCol_Text),object.name.c_str());draw->PopClipRect();
                        draw->AddText(ImVec2(row.x+width-badge-4,row.y+6),ImGui::GetColorU32(ImGuiCol_TextDisabled),kind);ImGui::PopID();
                    }
                }else{
                for(const auto group:order)if(!groups[group].empty()){
                    ImGui::TextDisabled("%s",albert::core::categoryName(static_cast<albert::core::Category>(group)));
                    ImGuiListClipper clipper;clipper.Begin(static_cast<int>(groups[group].size()),38.f);
                    while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){const auto& object=*groups[group][i].object;
                        ImGui::PushID(object.id.c_str());if(ImGui::Selectable(object.name.c_str(),focus==object.id,ImGuiSelectableFlags_None,ImVec2(0,28))){setFocus(object.id);ImGui::CloseCurrentPopup();}ImGui::PopID();}
                }
                }
                ImGui::EndChild();
                ImGui::TextDisabled(focusSearch[0]?"Melhores correspondencias primeiro.":"Catalogo local. Ordenado por proximidade do Sol.");
                ImGui::Separator();ImGui::TextDisabled("VISTAS DE CONJUNTO");
                const float width=(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x*2)*.333333f;
                if(ImGui::Button("Terra e Lua",ImVec2(width,34))){setFocus(targets::EarthMoon);ImGui::CloseCurrentPopup();}ImGui::SameLine();
                if(ImGui::Button("Internos",ImVec2(width,34))){setFocus(targets::Inner);ImGui::CloseCurrentPopup();}ImGui::SameLine();
                if(ImGui::Button("Tudo",ImVec2(width,34))){setFocus(targets::Overview);ImGui::CloseCurrentPopup();}
                ImGui::EndPopup();
            }
            ImGui::End();
            ImGui::PopStyleVar();

            // Symmetric timing in both directions, including a mid-flight reversal.
            menuProgress = std::clamp(menuProgress + (menuOpen ? 1.f : -1.f) * ImGui::GetIO().DeltaTime / .24f, 0.f, 1.f);
            const float menuReveal = smoothstep(menuProgress);
            if (menuReveal > 0.f) {
                const float panelWidth = std::min(330.f, display.x - 32.f);
                ImGui::SetNextWindowPos(ImVec2(margin - (1.f - menuReveal) * (panelWidth + margin), 94), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(panelWidth, std::max(80.f, display.y - 110.f)));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * menuReveal);
                ImGui::Begin("Menu lateral", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                protectPanel();
                ImGui::TextColored(ImVec4(.5f,.86f,.93f,1), "%s", focusName(focus));
                if(focus.front()!='@' && albert::core::localOrbit(focus)) {
                    ImGui::TextDisabled("Orbita aproximada offline");
                    const auto* surface=albert::core::surfaceBody(focus);
                    ImGui::TextWrapped("%s",surface&&!surface->notice.empty()&&solids.contains(focus)?std::string(surface->notice).c_str():solids.measuredShape(focus)?"Forma observada DSK; mosaico Viking. Registro da imagem sobre o modelo ainda nao validado.":solids.contains(focus)?(surface&&surface->axesMeters[0]>0?"Mosaico Viking NASA/JPL/USGS; forma elipsoidal de referencia IAU. Sem relevo medido.":surface&&surface->partialMap?"Mapa parcial NASA. Areas uniformes ou borradas podem nao ter observacoes. Sem relevo medido.":"Mapa NASA do objeto; resolucao e cobertura variam. Sem relevo medido."):albert::artisticRock(focus)?"Representacao artistica: forma e superficie nao medidas.":"Superficie ilustrativa; imagens reais ainda nao incluidas.");
                }
                if(focus==targets::Moon&&eclipses.lunarActive())ImGui::TextWrapped("Eclipse lunar: luz atmosferica avermelhada aproximada.");
                if(solids.hazy(focus)){bool surface=solids.surfaceView(focus);if(ImGui::Checkbox("Superficie em infravermelho (938 nm)",&surface))solids.showSurface(focus,surface);}
                if(solids.faintRings(focus)){
                    bool enhanced=solids.ringsEnhanced(focus);if(ImGui::Checkbox("Realcar aneis tenues",&enhanced))solids.enhanceRings(focus,enhanced);
                    ImGui::PushTextWrapPos(0);ImGui::TextDisabled(enhanced?"Brilho esticado, como em longa exposicao; proporcoes mantidas.":"Aneis escuros ou de poeira: quase invisiveis em luz visivel.");ImGui::PopTextWrapPos();
                }
                if(comets.known(focus)){
                    bool enhanced=comets.enhanced(focus);if(ImGui::Checkbox("Realcar coma e caudas",&enhanced))comets.enhance(focus,enhanced);
                    ImGui::PushTextWrapPos(0);
                    if(comets.afrho(focus)>0)ImGui::TextDisabled("Atividade medida (Afrho): %.0f cm a %.2f UA do Sol. %s",comets.afrho(focus)*100,comets.distanceAu(focus),
                        enhanced?"Brilho em escala asinh, como em astrofotografia: coma e cauda juntas.":"Em brilho fisico, quase invisivel como numa exposicao curta.");
                    else ImGui::TextDisabled("Sem coma: a %.1f UA do Sol, alem do inicio da atividade. Escolha uma data perto do perielio.",comets.distanceAu(focus));
                    ImGui::TextDisabled("Poeira em cinza; ions de CO+ em azul, na direcao do vento solar.");
                    ImGui::PopTextWrapPos();
                }
                if(focus.front()!='@' && !registry.get(focus).builtin) {
                    const auto state=registry.evaluate(focus,simulatedUtc);
                    ImGui::TextDisabled(state.stale ? "Dados interrompidos: ultima posicao mantida" : registry.get(focus).providerManaged ? "Posicoes reais; visual simplificado" : "Dados fornecidos pelo integrador");
                }
                ImGui::TextDisabled(freeCamera?"Camera livre: arraste para olhar, W A S D movem. F ou Esc volta.":"Arraste para girar. Role para aproximar.");
                ImGui::Spacing();
                const char* pages[] = {"Cena", "Dados", "Fisica", "Ajustes", "Sobre"};
                float labels = 0; // Each tab its label's width plus an even share of the rest.
                for (const char* page : pages) labels += ImGui::CalcTextSize(page).x;
                const float share = (ImGui::GetContentRegionAvail().x - 4 * ImGui::GetStyle().ItemSpacing.x - labels) / 5;
                for (int i = 0; i < 5; ++i) {
                    if (i) ImGui::SameLine();
                    const bool selectedPage = menuPage == i;
                    if (selectedPage) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.09f,.29f,.36f,1));
                    if (ImGui::Button(pages[i], ImVec2(ImGui::CalcTextSize(pages[i]).x + share, 34))) menuPage = i;
                    if (selectedPage) ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (menuPage == 0) {
                    ImGui::TextWrapped("Personalize sua vista");
                    ImGui::TextDisabled("Graficos, Terra, camera e som ficam em Ajustes.");
                    ImGui::Spacing();
                    if(showDebris&&systemView(focus))ImGui::TextDisabled("Populacoes ilustrativas, visiveis ao afastar.");
                    if (showOrbits) {
                        ImGui::TextDisabled("Distancias e tamanhos em escala real.");
                        ImGui::TextColored(ImVec4(.29f,.67f,.79f,1), "Passado: linha continua com fade");
                        ImGui::TextColored(ImVec4(.94f,.70f,.40f,1), "Futuro: linha tracejada com fade");
                    }
                    if(!registry.layers().empty()&&ImGui::CollapsingHeader("Camadas")){
                        std::optional<std::string> remove;
                        for(const auto& [id,layer]:registry.layers()){ImGui::PushID(id.c_str());bool visible=layer.visible;if(ImGui::Checkbox("##Visibilidade",&visible))registry.layerVisible(id,visible);ImGui::SameLine();ImGui::TextWrapped("%s",layer.name.c_str());if(!layer.source.empty())ImGui::TextWrapped("Fonte: %s",layer.source.c_str());if(ImGui::SmallButton("Remover"))remove=id;ImGui::PopID();}
                        if(remove)registry.removeLayer(*remove);
                    }
                    if(focus.front()!='@'&&focus!=targets::Sun){
                        const bool lunar=focus==targets::Moon,terrestrial=focus==targets::Earth;
                        auto& extra=visualSettings.auxiliary[focus];bool& assist=lunar?visualSettings.moonAssist:terrestrial?visualSettings.earthAssist:extra.enabled;
                        float& intensity=lunar?visualSettings.moonAssistIntensity:terrestrial?visualSettings.earthAssistIntensity:extra.intensity;
                        ImGui::Checkbox("Clarear o lado escuro",&assist);
                        if(assist){ImGui::SetNextItemWidth(-1);int brightness=static_cast<int>(std::round(intensity*100.f));if(ImGui::SliderInt("##Brilho auxiliar",&brightness,10,150,"Brilho auxiliar: %d%%"))intensity=brightness/100.f;ImGui::TextDisabled("Ajuda visual para a regiao sem Sol.");}
                    }
                } else if (menuPage == 1) {
                    ImGui::TextWrapped("Atualizacao automatica ativada");
                    ImGui::TextDisabled("Voce pode continuar explorando.");
                    if(ephemerides.cached)ImGui::TextWrapped("Posicoes reutilizadas do cache; conferindo em segundo plano.");
                    if (!refreshError.empty()) ImGui::TextColored(ImVec4(1,.7f,.4f,1), "Posicoes aguardando atualizacao");
                    ImGui::Spacing();
                    ImGui::BeginDisabled(downloads.active || refreshing || refreshingOrbits);
                    if (ImGui::Button("Atualizar posicoes agora", ImVec2(-1, 36))) downloads.start(false);
                    ImGui::EndDisabled();
                    ImGui::Spacing();
                    const auto timing=timeService.status();
                    ImGui::TextDisabled("HORARIO DA SIMULACAO");
                    if(timing.quality==albert::time::Quality::Synchronized) ImGui::TextWrapped("Conferido na internet");
                    else if(timing.quality==albert::time::Quality::Holdover) ImGui::TextWrapped("Ultimo horario conferido; aguardando nova consulta");
                    else if(timing.quality==albert::time::Quality::Expired) ImGui::TextWrapped("Horario estimado; confirmacao externa expirada");
                    else ImGui::TextWrapped("Horario local, sem confirmacao externa");
                    ImGui::BeginDisabled(timing.synchronizing);
                    if(ImGui::Button("Conferir horario",ImVec2(-1,32)))timeService.resynchronize();
                    ImGui::EndDisabled();
                    if(earthControls){
                    ImGui::Spacing();
                    ImGui::TextDisabled("NUVENS EM TEMPO REAL");
                    if (!visualSettings.clouds) ImGui::TextWrapped("Desativadas. Ative em Cena para buscar observacoes.");
                    else if (cloudSync.date.empty()) ImGui::TextWrapped("Aguardando observacoes de satelite.");
                    else ImGui::TextWrapped("Observacoes: %s", cloudSync.date.c_str());
                    if (cloudSync.active) {
                        const auto status = cloudSync.progress.snapshot();
                        ImGui::TextWrapped("Atualizando nuvens...");
                        if (status.total > 0) ImGui::ProgressBar(static_cast<float>(status.received) / status.total, ImVec2(-1, 5), "");
                    }
                    if (!cloudSync.error.empty()) {
                        ImGui::TextColored(ImVec4(1,.7f,.4f,1), "Atualizacao adiada");
                        ImGui::TextWrapped(cloudSync.date.empty() ?
                            "A imagem de referencia continua disponivel. Tentaremos novamente automaticamente." :
                            "A ultima imagem valida foi mantida. Tentaremos novamente automaticamente.");
                        ImGui::BeginDisabled(cloudSync.active || cloudSync.detailActive());
                        if (ImGui::Button("Tentar novamente", ImVec2(-1, 36))) { visualSettings.clouds=true; cloudSync.refresh(); }
                        ImGui::EndDisabled();
                    }
                    }
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Cache e detalhes visuais")) {
                        ImGui::Text("Espaco usado: %.1f MB",cache.bytes/(1024.*1024.));
                        constexpr int sizes[]={128,512,1024,2048,4096,8192};
                        int selected=0;
                        for(int i=0;i<6;++i) if(cache.limitMB==sizes[i]) selected=i;
                        ImGui::BeginDisabled(cloudSync.active || cloudSync.detailActive() || planets.detailActive() || regional.active());
                        if(ImGui::Combo("Limite",&selected,"128 MB\0" "512 MB\0" "1 GB\0" "2 GB\0" "4 GB\0" "8 GB\0")) cache.setLimit(sizes[selected]);
                        if(ImGui::Button("Limpar cache",ImVec2(-1,34))) { cache.clear(); planets.retryDetails(); regional.retry(); }
                        ImGui::EndDisabled();
                        ImGui::TextWrapped("Posicoes, orbitas e nuvens baixadas ficam no cache. A biblioteca incluida permanece intacta.");
                        if(planets.detailActive() || regional.active()) {
                            const auto transfer=regional.active() ? regional.progress.snapshot() : planets.detailActive() ? planets.detailProgress.snapshot() : cloudSync.detailSnapshot();
                            ImGui::TextWrapped("Carregando detalhes...");
                            if(transfer.total>0) ImGui::ProgressBar(static_cast<float>(transfer.received)/transfer.total,ImVec2(-1,5),"");
                        }
                        if(!planets.detailError.empty() || !regional.error.empty()) {
                            ImGui::TextWrapped("Detalhes adicionais indisponiveis. A imagem base foi mantida.");
                            if(ImGui::Button("Retentar detalhes visuais")) { planets.retryDetails(); regional.retry(); }
                        }
                        if(!cache.error.empty()) ImGui::TextWrapped("%s",cache.error.c_str());
                    }
                    ImGui::Spacing();
                    if (earthControls && ImGui::CollapsingHeader("Manutencao das nuvens")) {
                        ImGui::TextWrapped("Atualiza os downloads de nuvens sem apagar as texturas incluidas.");
                        ImGui::BeginDisabled(cloudSync.active || cloudSync.detailActive());
                        if (ImGui::Button("Limpar e baixar nuvens", ImVec2(-1, 36))) { visualSettings.clouds=true; cloudSync.refresh(true); }
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(downloads.active || refreshing || refreshingOrbits);
                        if (ImGui::Button("Recarregar texturas incluidas", ImVec2(-1, 36))) { regional.retry(); planets.retryDetails(); downloads.start(true); }
                        ImGui::EndDisabled();
                    }
                    if (ImGui::CollapsingHeader("Detalhes tecnicos")) {
                        const auto timing=timeService.status();
                        ImGui::TextWrapped("Tempo: %s. Fontes: %s",albert::time::qualityName(timing.quality),timing.source.c_str());
                        if(timing.uncertaintySeconds>=0)ImGui::TextWrapped("Incerteza estimada: %.1f ms. Atraso: %.1f ms. Ultima consulta valida: %.0f s atras.",timing.uncertaintySeconds*1000.,timing.delaySeconds*1000.,timing.ageSeconds);
                        ImGui::TextWrapped("NTP com concordancia entre fontes; sem autenticacao NTS.");
                        if(!timing.error.empty())ImGui::TextWrapped("%s",timing.error.c_str());
                        if(!regional.error.empty()) ImGui::TextWrapped("%s",regional.error.c_str());
                        ImGui::TextWrapped("Posicoes: NASA/JPL Horizons, atualizadas a cada 5 minutos. Orbitas: a cada 24 horas.");
                        ImGui::TextWrapped("Nuvens: Terra + Aqua / MODIS, NASA GIBS. Consulta a cada 30 minutos.");
                        ImGui::TextWrapped("Mosaico de passagens, sem simultaneidade global. Fracao valida: %.0f%%. Detalhes fotograficos: %.0f%%.", cloudSync.coverage * 100, cloudSync.detailCoverage * 100);
                        if (!cloudSync.error.empty()) ImGui::TextWrapped("%s", cloudSync.error.c_str());
                        ImGui::TextWrapped("Texturas e relevo: biblioteca local visuals.pak. Nenhum download de superficie.");
                        if(cloudSync.detailActive()) ImGui::TextWrapped("Carregando detalhes regionais das nuvens...");
                        if(!cloudSync.detailError.empty()) {
                            ImGui::TextWrapped("Detalhes das nuvens adiados: %s",cloudSync.detailError.c_str());
                            if(ImGui::Button("Retentar detalhes")) { visualSettings.clouds=true; cloudSync.refresh(); }
                        }
                        if (!refreshError.empty()) ImGui::TextWrapped("%s", refreshError.c_str());
                        ImGui::TextDisabled("%.0f quadros por segundo", ImGui::GetIO().Framerate);
                    }
                } else if (menuPage == 2) {
                    auto hint=[](const char* text){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();};
                    auto check=[&](const char* label,bool& value,const char* note){if(ImGui::Checkbox(label,&value)){savePreferences();applyPhysics();}hint(note);};
                    ImGui::TextDisabled("MODELOS DOS NOVOS VOOS");
                    hint("Valem para os voos criados pela API sem escolher (PhysicsSettings); cada voo grava os que usou.");
                    check("Mares do Sol e da Lua",preferences.thirdBodies,"A atracao de mare dos terceiros corpos (na Lua, a Terra e o Sol; em Marte, o Sol).");
                    check("Relatividade geral",preferences.relativity,"O termo de Schwarzschild do corpo central: centimetros por orbita baixa.");
                    check("Atmosfera do voo",preferences.flightAir,"Arrasto, sustentacao, aquecimento e a contrapressao nos bocais; desligada, vacuo.");
                    check("Vento",preferences.wind,"O vento do tempo real (GFS) e do HWM14; desligado, o ar parado em relacao ao solo.");
                    check("Pressao de radiacao solar",preferences.solarPressure,"Sobre a area solar do veiculo (definicao), com a sombra do corpo central.");
                    ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##Turbulencia",&preferences.turbulence,0,10,preferences.turbulence>0?"Turbulencia: %.1f m/s":"Turbulencia: nenhuma")){savePreferences();applyPhysics();}
                    hint("Rajadas de Dryden (MIL-F-8785C) com a intensidade vertical escolhida: ~1 leve, 2-3 moderada, 5 ou mais severa.");
                    ImGui::Spacing();ImGui::TextDisabled("VOO ATUAL");
                    if(flightTelemetry){ImGui::TextWrapped("%s",flightTelemetry->vehicle.c_str());hint(flightTelemetry->physics.c_str());hint(("Ar: "+flightTelemetry->atmosphere).c_str());}
                    else hint("Nenhum voo. Crie um pela API gRPC (docs/RPC.md): veiculos prontos ou definidos por voce.");
                    ImGui::Spacing();ImGui::TextDisabled("SEMPRE NO MODELO");
                    hint("Gravidade em harmonicos esfericos (grau do voo; 0: massa pontual), rotacao do corpo, empuxo com contrapressao e cardas, massa e inercia variaveis com amortecimento do jato, pernas com atrito, aquecimento de estagnacao. Motores, RCS e superficies de controle vem da definicao do veiculo.");
                } else if (menuPage == 3) {
                    auto hint=[](const char* text){ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));ImGui::TextWrapped("%s",text);ImGui::PopStyleColor();};
                    auto check=[&](const char* label,bool& value,const char* note=nullptr){if(ImGui::Checkbox(label,&value))savePreferences();if(note)hint(note);};
                    auto slider=[&](const char* id,float& value,float lo,float hi,const char* format){ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat(id,&value,lo,hi,format))savePreferences();};
                    ImGui::TextDisabled("GRAFICOS");
                    ImGui::SetNextItemWidth(-1);if(ImGui::Combo("##Qualidade",&visualSettings.quality,"Qualidade: mais leve\0Qualidade: mais detalhes\0"))savePreferences();
                    hint("Mais detalhes: ate 2x a resolucao da tela e sombras de 2048 px; carregados ao aproximar.");
                    check("Antisserrilhado (4x)",preferences.antialias);
                    check("Telemetria como na transmissão",preferences.broadcast,"Os motores de cada veículo, velocidade, altitude, propelente, o relógio T+ e os eventos da missão.");
                    check("Painel de interferência",preferences.playground,"Durante um voo: rajadas e empurrões, falhas de motores e de sensores (ou que mentem), aborto e terminação.");
                    check("Sincronia vertical",preferences.vsync,"Desligada, o quadro sai assim que fica pronto (pode rasgar a imagem).");
                    {int cap=preferences.frameCap==30?0:preferences.frameCap==60?1:2;ImGui::SetNextItemWidth(-1);
                        if(ImGui::Combo("##Limite de quadros",&cap,"Limite: 30 quadros/s\0Limite: 60 quadros/s\0Sem limite\0")){preferences.frameCap=cap==0?30:cap==1?60:0;savePreferences();}
                        hint("30 por segundo aquece e gasta bem menos a placa e o processador.");}
                    check("Sombras",visualSettings.shadows);
                    check("Atmosfera",visualSettings.atmosphere);
                    check("Relevo detalhado",visualSettings.terrain,"Copernicus 30 m na Terra, LOLA na Lua, MOLA em Marte.");
                    check("Nuvens volumetricas",preferences.volumetricClouds,"Nuvens em 3D perto da Terra, das nuvens em tempo real; desligadas, fica a camada plana.");
                    check("Ondas no mar",preferences.oceanWaves,"Ondas pelo vento perto da camera; desligadas, o mar so tem a rugosidade media.");
                    slider("##Campo de visao",preferences.fov,35,90,"Campo de visao: %.0f graus");
                    ImGui::Spacing();ImGui::TextDisabled("TERRA");
                    check("Nuvens em tempo real",visualSettings.clouds,"Observacoes de satelite, com atraso.");
                    check("Luzes das cidades",visualSettings.nightLights);
                    check("Fronteiras dos paises",visualSettings.borders);
                    ImGui::Spacing();ImGui::TextDisabled("VISTA");
                    check("Caminhos das orbitas",showOrbits);
                    check("Cinturoes e nuvem de Oort",showDebris);
                    check("Girar devagar ao chegar",preferences.autoRotate);
                    check("Segunda vista dos voos",preferences.inset,"Com dois voos ou mais, um quadro no canto mostra o outro; clique nele para trocar.");
                    check("Efeitos do dilúvio",preferences.deluge,"O spray de água do pad na contagem e as nuvens de vapor da decolagem (pesam na placa de vídeo).");
                    slider("##Escala da interface",preferences.uiScale,.8f,1.6f,"Interface: %.1fx");
                    ImGui::Spacing();ImGui::TextDisabled("CAMERA");
                    bool free=freeCamera;if(ImGui::Checkbox("Camera livre (F)",&free))toggleFreeCamera(free);
                    hint("Arraste para olhar; W A S D, Espaco e Ctrl movem; Shift acelera; a roda ajusta a velocidade.");
                    slider("##Sensibilidade",preferences.sensitivity,.25f,3,"Sensibilidade do mouse: %.2fx");
                    check("Inverter o olhar vertical",preferences.invertY);
#ifdef ALBERT_HAS_RPC
                    ImGui::Spacing();ImGui::TextDisabled("SOM");
                    ImGui::Checkbox("Som do voo",&soundOn);
                    hint("O ruido do foguete na camera, com o atraso e o ar do caminho. Comeca mudo.");
                    int volume=static_cast<int>(std::lround(preferences.volume*100));ImGui::SetNextItemWidth(-1);
                    if(ImGui::SliderInt("##Volume",&volume,0,100,"Volume: %d%%")){preferences.volume=volume/100.f;savePreferences();}
#endif
                } else {
                    ImGui::TextWrapped("O universo, na sua tela.");
                    ImGui::TextWrapped("Explore corpos celestes em escala real, com posicoes astronomicas, imagens de observacao e estrelas catalogadas.");
                    std::string included;for(const auto& [id,object]:registry.objects())if(object.builtin){(void)id;if(!included.empty())included+=", ";included+=object.name;}
                    ImGui::TextWrapped("Conteudo incluido: %s.",included.c_str());
                    ImGui::TextWrapped("Conecte um integrador pela API gRPC para adicionar outros objetos. Objetos sem imagens incluidas usam visuais simplificados.");
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Fontes e creditos")) {
                        ImGui::TextWrapped("Imagens: NASA Earth Observatory, SDO/HMI, LROC/ASU, LOLA, GEBCO e MESSENGER / JHUAPL / CIW / USGS. Fronteiras: Natural Earth. Estrelas: HYG / David Nash (CC BY-SA 4.0). Fonte: Inter (OFL).");
                        ImGui::TextWrapped("Atmosfera e brilho sao modelos visuais. Nuvens observadas possuem atraso. Imagens da superficie e do Sol sao referencias estaticas. Precisao e limites: documentacao do projeto.");
                    }
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            const float zoomRatio = camera->position.distanceTo(controls.target) / distanceFor(focus);
            const bool resetZoomVisible = !transition.active && std::abs(std::log(std::max(1e-9f, zoomRatio))) > .03f;
            resetZoomProgress = std::clamp(resetZoomProgress + (resetZoomVisible ? 1.f : -1.f) * ImGui::GetIO().DeltaTime / .22f, 0.f, 1.f);
            if (resetZoomProgress > 0.f) {
                const float reveal = smoothstep(resetZoomProgress);
                ImGui::SetNextWindowPos(ImVec2(display.x - margin,
                    display.y - margin + 18.f * (1.f - reveal) - (broadcasting ? 210.f*std::clamp(display.x/1600.f,.7f,1.4f) : 0.f)),
                    ImGuiCond_Always, ImVec2(1,1));
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
                ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
                ImGui::SetNextWindowBgAlpha(0.f);
                ImGui::Begin("Restaurar enquadramento", nullptr, flags | ImGuiWindowFlags_NoScrollbar);
                protectPanel();
                ImGui::BeginDisabled(!resetZoomVisible);
                if (ImGui::Button("Restaurar zoom", ImVec2(160,42))) setFocus(focus, true);
                ImGui::EndDisabled();
                ImGui::End();
                ImGui::PopStyleVar(4);
            }

            if(insetTexture&&!insetId.empty()){ // The second view, bottom left; a click follows that flight instead.
                const float width=display.x*.28f;
                if(broadcasting)ImGui::SetNextWindowPos(ImVec2(margin,margin+74),ImGuiCond_Always); // Above, under the menu: the panels own the bottom.
                else ImGui::SetNextWindowPos(ImVec2(margin,display.y-margin),ImGuiCond_Always,ImVec2(0,1));ImGui::SetNextWindowBgAlpha(.55f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(6,6));
                ImGui::Begin("Segunda vista",nullptr,flags|ImGuiWindowFlags_NoScrollbar);protectPanel();
                if(insetEngines){const float size=11*std::clamp(display.x/1600.f,.7f,1.4f);const auto at=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
                    draw->AddText(ImGui::GetFont(),size,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,140),"TESTE DE VETORAÇÃO DOS MOTORES");
                    draw->AddText(ImGui::GetFont(),size,at,IM_COL32(170,178,188,220),"TESTE DE VETORAÇÃO DOS MOTORES");ImGui::Dummy(ImVec2(width,size));}
                else ImGui::TextUnformatted(registry.get(insetId).name.c_str());
                if(ImGui::ImageButton("##segunda",ImTextureRef(static_cast<ImTextureID>(insetTexture)),ImVec2(width,width*9/16)))setFocus(insetId);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Seguir este voo");
                ImGui::End();ImGui::PopStyleVar();
            }
            const bool loadingDetails=planets.detailActive() || solids.uploading() || cloudSync.detailActive() || regional.active();
            detailLoaderAge=loadingDetails ? detailLoaderAge+ImGui::GetIO().DeltaTime : 0.f;
            detailLoaderAlpha=albert::animateValue(detailLoaderAlpha,detailLoaderAge>.15f ? .65f : 0.f,ImGui::GetIO().DeltaTime);
            if(detailLoaderAlpha>0.f) {
                auto* draw=ImGui::GetForegroundDrawList();
                const ImVec2 spot(display.x-margin-88,display.y-margin-60);
                for(int i=0;i<8;++i) {
                    const float angle=static_cast<float>(ImGui::GetTime()*5.)+i*static_cast<float>(PI)*.25f;
                    draw->AddLine(ImVec2(spot.x+std::cos(angle)*3,spot.y+std::sin(angle)*3),
                        ImVec2(spot.x+std::cos(angle)*5,spot.y+std::sin(angle)*5),
                        ImGui::GetColorU32(ImVec4(.55f,.78f,.84f,detailLoaderAlpha*(i+1)/8.f)),1.2f);
                }
                draw->AddText(ImVec2(spot.x+12,spot.y-8),ImGui::GetColorU32(ImVec4(.65f,.76f,.8f,detailLoaderAlpha)),"Detalhes");
            }
            if(assetDownloads) {
                // Background detail download: a quiet ring and two lines in the corner,
                // a retry button only after a failure, fading out once complete.
                const auto status=assetDownloads->status();const float dt=ImGui::GetIO().DeltaTime;
                assetCompleteAge=status.complete?assetCompleteAge+dt:0.f;
                assetIndicatorAlpha=albert::animateValue(assetIndicatorAlpha,assetCompleteAge<2.5f?1.f:0.f,dt);
                if(assetIndicatorAlpha>.01f) {
                    const double fraction=status.totalBytes?static_cast<double>(status.doneBytes)/static_cast<double>(status.totalBytes):1.;
                    auto bytes=[](std::uint64_t b){char text[32];std::snprintf(text,sizeof text,b>=1000000000ull?"%.1f GB":"%.0f MB",b>=1000000000ull?b/1e9:b/1e6);return std::string(text);};
                    auto groupName=[&](const std::string& group)->std::string{if(group=="moons")return "Luas";try{return registry.get(group).name;}catch(const std::exception&){return group;}};
                    std::string title=status.complete?"Detalhes completos":!status.error.empty()?"Download pausado":"Baixando detalhes",detail;
                    if(status.complete)detail="Todas as resolucoes disponiveis";
                    else if(!status.error.empty())detail="Nova tentativa em "+std::to_string(status.retrySeconds)+" s";
                    else for(const auto& item:status.items)if(item.active){detail=groupName(item.group)+" nivel "+std::to_string(item.level)+" - "+bytes(status.doneBytes)+" de "+bytes(status.totalBytes);break;}
                    ImGui::SetNextWindowPos(ImVec2(margin,display.y-margin),ImGuiCond_Always,ImVec2(0,1));
                    ImGui::SetNextWindowBgAlpha(.55f);
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,ImGui::GetStyle().Alpha*assetIndicatorAlpha);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12,9));
                    ImGui::Begin("Dados visuais",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove);
                    protectPanel();
                    auto* draw=ImGui::GetWindowDrawList();const auto origin=ImGui::GetCursorScreenPos();const ImVec2 centre(origin.x+11,origin.y+15);
                    const ImVec4 accent=status.error.empty()?ImVec4(.55f,.86f,.93f,assetIndicatorAlpha):ImVec4(.95f,.72f,.42f,assetIndicatorAlpha);
                    draw->AddCircle(centre,8.f,ImGui::GetColorU32(ImVec4(.6f,.7f,.75f,.25f*assetIndicatorAlpha)),32,2.f);
                    draw->PathArcTo(centre,8.f,-static_cast<float>(PI)*.5f,-static_cast<float>(PI)*.5f+static_cast<float>(2*PI*fraction),32);
                    draw->PathStroke(ImGui::GetColorU32(accent),ImDrawFlags_None,2.f);
                    ImGui::Dummy(ImVec2(22,30));ImGui::SameLine(0,8);ImGui::BeginGroup();
                    ImGui::Text("%s  %d%%",title.c_str(),static_cast<int>(fraction*100));
                    ImGui::TextDisabled("%s",detail.c_str());
                    if(!status.error.empty()&&ImGui::SmallButton("Tentar agora"))assetDownloads->retry();
                    ImGui::EndGroup();
                    ImGui::End();ImGui::PopStyleVar(2);
                }
            }
            // External flight: vehicle state and the air it flies through (gRPC FlightService).
#ifdef ALBERT_HAS_RPC
            // The broadcast's panels (Ajustes): each flight's engines, speed, height and propellant (the one with more engines
            // on the left), the mission's clock from the main flight's T-0 (its start unless its software counts one) and every
            // flight's events.
            const auto gauges=preferences.broadcast&&rpc?rpc->flightGauges():std::vector<albert::rpc::FlightServices::Gauge>{};
            broadcasting=!gauges.empty();
            if(!gauges.empty()){
                std::vector<albert::BroadcastVehicle> vehicles;std::vector<albert::BroadcastEvent> events;albert::BroadcastCount count;double start=gauges.front().zero,shown=simulatedUtc;
                for(const auto& g:gauges){if(g.id=="flight"){start=g.zero;if(g.stopped)shown=std::min(shown,g.stop);} // Its clock stopped (a scrub, an abort, its end): there.
                    if(g.id==focus)count={g.countState,g.countTime};
                    albert::BroadcastVehicle v;v.label=g.label;v.speed=g.speed;v.height=g.height;v.propellant=g.propellant;
                    for(const auto& e:g.engines)v.engines.push_back({e.y,e.z,e.radius,e.throttle,e.on,e.failed});vehicles.push_back(std::move(v));}
                for(const auto& g:gauges)for(const auto& m:g.milestones)if(std::none_of(events.begin(),events.end(),[&](const albert::BroadcastEvent& e){return e.name==m.name;}))
                    events.push_back({m.name,m.utc-start,m.planned});
                std::sort(vehicles.begin(),vehicles.end(),[](const auto& a,const auto& b){return a.engines.size()>b.engines.size();});
                if(vehicles.size()>2)vehicles.resize(2);if(vehicles.size()==2)vehicles[1].right=true;else vehicles[0].right=vehicles[0].engines.size()<=6;
                albert::BroadcastDecision decision; // The newest of every flight's latest decision.
                for(const auto& g:gauges)if(g.decided>=0&&!g.decision.empty()&&(decision.what.empty()||g.decided-start>decision.time))decision={g.label,g.decision,g.why,g.decided-start};
                albert::drawBroadcast(vehicles,shown-start,events,decision,simulation.read().pace,[&]() -> std::optional<double>{for(const auto& g:gauges)if(g.id==focus)return g.height;return std::nullopt;}(),count);
            }
            if(rpc&&preferences.playground&&menuProgress<=0.f)drawPlayground(*rpc,display,margin); // Live interference on the flights.
#else
            const std::vector<int> gauges;
#endif
            if(flightTelemetry&&gauges.empty()&&(display.x>700||menuProgress<=0.f)){
                const auto& f=*flightTelemetry;
                ImGui::SetNextWindowPos(ImVec2(display.x-margin,display.y-margin),ImGuiCond_Always,ImVec2(1,1));
                ImGui::SetNextWindowSize(ImVec2(std::min(330.f,display.x-margin*2),0),ImGuiCond_Always);ImGui::SetNextWindowBgAlpha(.65f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16,12));
                ImGui::Begin("Voo",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove);
                protectPanel();
                ImGui::TextUnformatted(f.vehicle.c_str());
                ImGui::TextDisabled("T+ %.1f s   estagio %d/%d   %s",f.elapsed,f.stage+1,f.stages,f.landed?"no solo":f.clamped?"preso no pad":f.burning?"motor ligado":"motor desligado");
                ImGui::Text("Altitude %.2f km   Velocidade %.0f m/s",f.height/1e3,f.speed);
                ImGui::Text("Mach %.2f   q %.1f kPa   Massa %.1f t",f.mach,f.dynamicPressure/1e3,f.mass/1e3);
                const double wind=std::hypot(f.windNorth,f.windEast);
                if(wind>.05)ImGui::Text("Vento %.1f m/s de %.0f graus",wind,std::fmod(std::atan2(-f.windEast,-f.windNorth)*180/PI+360,360.));else ImGui::Text("Vento calmo");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(300.f,display.x-margin*2-32));ImGui::TextDisabled("Ar: %s",f.atmosphere.c_str());ImGui::PopTextWrapPos();
                ImGui::End();ImGui::PopStyleVar();
            }
            // Time panel: the simulated date and the state of its data; opens the time controls.
            if (display.x > 900 || menuProgress <= 0.f) {
                const bool wide = display.x > 900;
                const float panelWidth = std::min(250.f, display.x - margin * 2);
                ImGui::SetNextWindowPos(wide ? ImVec2(display.x-margin,margin) : ImVec2(margin,margin+66),ImGuiCond_Always,wide ? ImVec2(1,0) : ImVec2(0,0));
                ImGui::SetNextWindowSize(ImVec2(panelWidth,80),ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(.65f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,14));
                ImGui::Begin("Estado", nullptr, (flags & ~ImGuiWindowFlags_AlwaysAutoResize) | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                protectPanel();
                const auto panelPosition = ImGui::GetWindowPos(), panelSize = ImGui::GetWindowSize();
                ImGui::SetCursorScreenPos(panelPosition);
                const bool openTime = ImGui::InvisibleButton("##Tempo", panelSize);
                if (ImGui::IsItemHovered()) ImGui::GetWindowDrawList()->AddRect(ImVec2(panelPosition.x+1,panelPosition.y+1),
                    ImVec2(panelPosition.x+panelSize.x-1,panelPosition.y+panelSize.y-1),ImGui::GetColorU32(ImVec4(.45f,.88f,.94f,.45f)),14.f);
                ImGui::SetCursorPos(ImVec2(18,14));
                const auto timing=timeService.status();
                const auto shown=simulation.read();
                ImGui::Text("%s", civilDate(simulatedUtc).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s UTC", civilClock(simulatedUtc).c_str());
                const ImVec4 attention(1,.7f,.4f,1), busy(.4f,.85f,.9f,1), calm(.4f,.85f,.7f,1);
                if(approximatePositions) ImGui::TextColored(refreshError.empty() ? busy : attention, "Posicoes aproximadas");
                else if(shown.limited) ImGui::TextColored(attention, "Limite: 1900 a 2100");
                else if(!shown.live && shown.rate == 0.) ImGui::TextDisabled("Pausado");
                else if(!shown.live) ImGui::TextColored(busy, "Simulado: %s%s", shown.pace < 0 ? "voltando " : "", speedLabel(shown.pace).c_str());
                else if(timing.quality==albert::time::Quality::Local)ImGui::TextColored(attention,"Horario local");
                else if(timing.quality!=albert::time::Quality::Synchronized)ImGui::TextColored(attention,"Horario estimado");
                else if (refreshing || refreshingOrbits || cloudSync.active || catalog.busy()) ImGui::TextColored(busy, "Atualizando dados");
                else if (!cloudSync.error.empty() || !refreshError.empty()) ImGui::TextColored(attention, "Atualizacao pendente");
                else ImGui::TextColored(calm, "Dados sincronizados");

                if (openTime) {
                    const auto text = albert::time::formatUtc(simulatedUtc).substr(0, 16);
                    std::snprintf(dateText.data(), dateText.size(), "%s", text.c_str());
                    dateError.clear();
                    ImGui::OpenPopup("Tempo da simulacao");
                }
                const float popupWidth = std::min(360.f, display.x - margin * 2), popupTop = wide ? margin+88 : margin+154;
                ImGui::SetNextWindowPos(wide ? ImVec2(display.x-margin,popupTop) : ImVec2(margin,popupTop),ImGuiCond_Appearing,wide ? ImVec2(1,0) : ImVec2(0,0));
                // Below the panel; scrolls instead of covering it on short windows.
                ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth,0),ImVec2(popupWidth,std::max(120.f,display.y-popupTop-margin)));
                if (ImGui::BeginPopup("Tempo da simulacao")) {
                    protectPanel();
                    // Follow changes made elsewhere (API) while the panel is open.
                    if (shown.rate != 0.) {chosenSpeed = std::abs(shown.pace);backwards = shown.pace < 0;}
                    const float spacing = ImGui::GetStyle().ItemSpacing.x, full = ImGui::GetContentRegionAvail().x;
                    const float half = (full - spacing) / 2, third = (full - 2 * spacing) / 3, quarter = (full - 3 * spacing) / 4;
                    auto selectable = [&](const char* label, bool selected, ImVec2 size) {
                        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.09f,.29f,.36f,1));
                        const bool pressed = ImGui::Button(label, size);
                        if (selected) ImGui::PopStyleColor();
                        return pressed;
                    };
                    ImGui::TextDisabled("TEMPO DA SIMULACAO");
                    ImGui::Text("%s   %s UTC", civilDate(simulatedUtc).c_str(), civilClock(simulatedUtc).c_str());
                    if (shown.live) ImGui::TextColored(calm, "Ao vivo");
                    else ImGui::TextDisabled("%s", offsetLabel(simulatedUtc - timing.utcSeconds).c_str());
                    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

                    ImGui::TextDisabled("VELOCIDADE");
                    const bool paused = shown.rate == 0.;
                    if (ImGui::Button(paused ? "Continuar" : "Pausar", ImVec2(half, 34)))
                        simulation.setRate(paused ? (backwards ? -chosenSpeed : chosenSpeed) : 0.);
                    ImGui::SameLine();
                    if (selectable("Voltar no tempo", backwards, ImVec2(half, 34))) {
                        backwards = !backwards;
                        if (!paused) simulation.setRate(backwards ? -chosenSpeed : chosenSpeed);
                    }
                    for (size_t i = 0; i < std::size(SPEEDS); ++i) {
                        if (i % 3) ImGui::SameLine();
                        if (selectable(SPEEDS[i].label, chosenSpeed == SPEEDS[i].rate, ImVec2(third, 32))) {
                            chosenSpeed = SPEEDS[i].rate;
                            simulation.setRate(backwards ? -chosenSpeed : chosenSpeed);
                        }
                    }
                    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

                    ImGui::TextDisabled("IR PARA (UTC)");
                    auto choose = [&](double utc) {
                        try {
                            simulation.jump(utc);dateError.clear();
                            const auto text = albert::time::formatUtc(utc).substr(0, 16);
                            std::snprintf(dateText.data(), dateText.size(), "%s", text.c_str());
                        } catch (const std::invalid_argument&) {dateError = "Escolha uma data de 1900 a 2100.";}
                    };
                    ImGui::SetNextItemWidth(full - 80 - spacing);
                    const bool entered = ImGui::InputTextWithHint("##Data", "AAAA-MM-DD HH:MM", dateText.data(), dateText.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    if (ImGui::Button("Ir", ImVec2(80, 0)) || entered) {
                        if (const auto utc = albert::time::parseUtc(dateText.data())) choose(*utc);
                        else dateError = "Use o formato AAAA-MM-DD HH:MM.";
                    }
                    constexpr std::pair<const char*, double> steps[]{{"-1 dia",-86400.},{"-1 h",-3600.},{"+1 h",3600.},{"+1 dia",86400.}};
                    for (size_t i = 0; i < std::size(steps); ++i) {
                        if (i) ImGui::SameLine();
                        if (ImGui::Button(steps[i].first, ImVec2(quarter, 32))) choose(simulatedUtc + steps[i].second);
                    }
                    if (!dateError.empty()) ImGui::TextColored(attention, "%s", dateError.c_str());
                    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
                    ImGui::TextDisabled("EVENTOS");
                    for (const auto& event : EVENTS) {
                        if (!ImGui::Button(event.label, ImVec2(full, 32))) continue;
                        simulation.choose(albert::time::parseUtc(event.utc).value(), 0.);
                        pendingEvent = PendingEvent{&event, simulation.read().generation};dateError.clear();
                        const auto text = albert::time::formatUtc(*albert::time::parseUtc(event.utc)).substr(0, 16);
                        std::snprintf(dateText.data(), dateText.size(), "%s", text.c_str());
                    }
                    ImGui::Spacing();
                    ImGui::BeginDisabled(shown.live);
                    if (ImGui::Button("Agora", ImVec2(full, 36))) {simulation.live();chosenSpeed = 1.;backwards = false;dateError.clear();}
                    ImGui::EndDisabled();
                    ImGui::Spacing();
                    ImGui::PushTextWrapPos(0);
                    ImGui::TextDisabled("Sol, Lua e planetas: efemerides do JPL Horizons para a data.");
                    if (approximatePositions) ImGui::TextColored(refreshError.empty() ? busy : attention, "%s",
                        std::abs(shown.rate) > TABLE_RATE_LIMIT ? "Acima de 6 h/s: posicoes dos modelos analiticos do ERFA, ajustadas as ultimas tabelas do JPL. Pause ou desacelere para buscar as tabelas." :
                        refreshError.empty() ? "Posicoes aproximadas (modelos analiticos do ERFA) ate as tabelas chegarem." : "Sem efemerides do JPL: posicoes aproximadas (modelos analiticos do ERFA).");
                    if (std::abs(simulatedUtc - localElementsUtc) > 365.25 * 86400.)
                        ImGui::TextDisabled("Outras luas e pequenos corpos: orbitas de dois corpos com elementos de %s, menos precisas longe dessa data.", civilDate(localElementsUtc).c_str());
                    if (visualSettings.clouds && !cloudsCurrent) ImGui::TextDisabled("Nuvens observadas so aparecem perto do presente.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndPopup();
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // Hover and click use the same nearest visible object.
            if(!ImGui::GetIO().WantCaptureMouse && !transition.active&&!covered(ImGui::GetMousePos(),ImGui::GetMousePos())){
                const auto mouse=ImGui::GetMousePos();Raycaster ray;ray.setFromCamera(Vector2(mouse.x/display.x*2.f-1.f,1.f-mouse.y/display.y*2.f),*camera);
                ray.nearPlane=camera->nearPlane;ray.farPlane=camera->farPlane;
                // During a mission (its flights on the broadcast) only its vehicles answer a click: a stray one on the Moon or a planet
                // took the camera off the flight.
                if(const auto picked=objects.pick(registry,ray);picked&&(!broadcasting||*picked=="flight"||picked->rfind("flight.",0)==0)){
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&ImGui::GetIO().MouseDragMaxDistanceSqr[0]<25.f)setFocus(*picked,*picked==focus);
                }
            }
            if (systemView(focus) || focus == targets::EarthMoon) {
                if(showOrbits&&!covered(ImVec2(display.x*.5f-130.f,display.y-36.f),ImVec2(display.x*.5f+130.f,display.y-16.f))) {
                    auto* legend=ImGui::GetBackgroundDrawList();
                    const float x=display.x*.5f-130.f, y=display.y-26.f;
                    legend->AddLine(ImVec2(x,y),ImVec2(x+24,y),IM_COL32(74,171,201,220),1.5f);
                    legend->AddText(ImVec2(x+32,y-8),IM_COL32(169,190,204,230),"Passado");
                    for(int i=0;i<3;++i) legend->AddLine(ImVec2(x+136+i*8,y),ImVec2(x+140+i*8,y),IM_COL32(240,179,102,230),1.5f);
                    legend->AddText(ImVec2(x+168,y-8),IM_COL32(210,196,174,230),"Futuro");
                }
                // Screen annotations identify sub-pixel bodies without inflating
                // their physical geometry or changing astronomical distances.
                auto mark = [&](const Focus& id,Vector3 position, const char* label, ImU32 color) {
                    position.project(*camera);
                    if (position.z < -1.f || position.z > 1.f) return;
                    const ImVec2 screen((position.x*.5f+.5f)*display.x, (.5f-position.y*.5f)*display.y);
                    if (screen.x < 8 || screen.x > display.x-8 || screen.y < 100 || screen.y > display.y-8) return;
                    const auto text=ImGui::CalcTextSize(label);
                    if(covered(ImVec2(screen.x-6,screen.y-10),ImVec2(screen.x+12+text.x,screen.y+10)))return;
                    auto* draw = ImGui::GetBackgroundDrawList();
                    draw->AddCircle(screen, 5.f, color, 16, 1.2f);
                    draw->AddText(ImVec2(screen.x+10,screen.y-8), color, label);
                    const auto mouse=ImGui::GetMousePos();
                    const bool hover=mouse.x>=screen.x-12&&mouse.x<=screen.x+12+text.x&&mouse.y>=screen.y-14&&mouse.y<=screen.y+14;
                    if(hover&&!ImGui::GetIO().WantCaptureMouse&&!transition.active){ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&ImGui::GetIO().MouseDragMaxDistanceSqr[0]<25.f)setFocus(id);}
                };
                mark(targets::Earth,earthPosition, systemView(focus) ? "Terra / Lua" : "Terra", IM_COL32(145,208,229,230));
                if (focus == targets::EarthMoon) mark(targets::Moon,moonPosition, "Lua", IM_COL32(215,220,230,230));
                else {mark(targets::Sun,sunPosition, "Sol", IM_COL32(255,239,204,230));for(const auto& [id,node]:solids.nodes())if(albert::core::naturalBody(id))mark(id,node->position,registry.get(id).name.c_str(),IM_COL32(211,201,184,230));}
            }

            {
                const auto* selected=albert::core::localOrbit(focus);
                const std::string parent=selected&&selected->category==albert::core::Category::Moon?std::string(selected->parent):focus;
                struct Label {ImVec2 point;};std::vector<Label> placed;
                for(const auto& body:albert::core::localOrbits()){
                    const bool overview=systemView(focus);
                    if(body.id==focus||(!overview&&body.parent!=parent)||(overview&&body.category==albert::core::Category::Moon))continue;
                    const float labelVisibility=overview ? secondaryVisibility[std::string(body.id)] : 1.f;
                    if(labelVisibility<.02f)continue;
                    const auto& node=objects.nodes().at(std::string(body.id));if(!node->visible)continue;
                    Vector3 delta=node->position;delta.sub(camera->position);const float actualDistance=delta.length();
                    if(actualDistance<camera->nearPlane)continue;
                    bool hidden=false;
                    for(const auto& [id,other]:objects.nodes())if(id!=body.id&&other->visible){
                        // Bounding spheres suffice for label occlusion; physical clicks still raycast geometry.
                        const float radius=static_cast<float>(registry.get(id).radiusMeters/albert::core::earthRadiusMeters);
                        Vector3 relative=other->position;relative.sub(camera->position);const auto direction=delta.clone().normalize();const float along=relative.dot(direction);
                        if(along>0&&along<actualDistance-radius&&relative.clone().cross(direction).lengthSq()<radius*radius){hidden=true;break;}
                    }
                    if(hidden)continue;
                    Vector3 projected=node->position;projected.project(*camera);if(projected.z < -1||projected.z>1)continue;
                    const ImVec2 point((projected.x*.5f+.5f)*display.x,(.5f-projected.y*.5f)*display.y);
                    if(point.x<16||point.x>display.x-16||point.y<90||point.y>display.y-40)continue;
                    // Large nearby spheres need no label unless hovered.
                    const float apparent=static_cast<float>(body.radius/albert::core::earthRadiusMeters)/actualDistance*display.y;
                    const auto mouse=ImGui::GetMousePos();const auto text=ImGui::CalcTextSize(body.name.data());
                    if(covered(ImVec2(point.x-6,point.y-10),ImVec2(point.x+12+text.x,point.y+10)))continue;
                    const bool hover=mouse.x>point.x-12&&mouse.x<point.x+text.x+14&&std::abs(mouse.y-point.y)<14;
                    if(apparent>25&&!hover)continue;
                    auto* draw=ImGui::GetBackgroundDrawList();draw->AddCircle(point,4,IM_COL32(185,203,211,int(190*labelVisibility)),12,1.f);
                    const bool overlap=std::any_of(placed.begin(),placed.end(),[&](const auto& p){return std::abs(p.point.y-point.y)<18&&std::abs(p.point.x-point.x)<100;});
                    if(!overlap||hover){draw->AddText(ImVec2(point.x+9,point.y-8),IM_COL32(185,203,211,int(220*labelVisibility)),body.name.data());placed.push_back({point});}
                    if(hover&&labelVisibility>.5f&&!ImGui::GetIO().WantCaptureMouse&&!transition.active){ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)&&ImGui::GetIO().MouseDragMaxDistanceSqr[0]<25.f)setFocus(std::string(body.id));}
                }
            }

            if (downloads.active || !downloads.error.empty()) {
                ImGui::SetNextWindowPos(ImVec2(display.x * .5f, display.y * .5f), ImGuiCond_Always, ImVec2(.5f,.5f));
                ImGui::SetNextWindowSize(ImVec2(std::min(460.f, display.x - 32), 0));
                ImGui::Begin("Sincronizacao", nullptr, flags);
                const auto timing=timeService.status();
                if(timing.synchronizing) ImGui::TextDisabled("Conferindo horario na internet...");
                else if(timing.quality==albert::time::Quality::Local) ImGui::TextColored(ImVec4(1,.7f,.4f,1),"Horario local: sem confirmacao externa");
                else ImGui::TextDisabled("Horario conferido na internet");
                drawDownloadStatus(downloads);
                ImGui::End();
            }
            uiPopupWasOpen=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
            if (ImGui::IsAnyItemHovered() && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        };

        // Reveal only after a complete scene frame has been drawn: initial
        // texture uploads/shader compilation must not consume the animation.
        constexpr float introductionDuration = 1.f;
        float introductionElapsed = 0.f;
        std::optional<std::chrono::steady_clock::time_point> introductionStart;
        auto drawSimulationInterface = std::move(drawInterface);
        drawInterface = [&] {
            if (introductionElapsed >= introductionDuration) {
                drawSimulationInterface();
                return;
            }
            const auto display = ImGui::GetIO().DisplaySize;
            const float reveal = smoothstep((introductionElapsed - .1f) / .9f);
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), display,
                IM_COL32(0, 0, 0, static_cast<int>(255.f * (1.f - reveal))));
            const float interfaceReveal = smoothstep((introductionElapsed - .4f) / .6f);
            if (interfaceReveal > 0.f) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, interfaceReveal);
                ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
                ImGui::BeginDisabled();
                drawSimulationInterface();
                ImGui::EndDisabled();
                ImGui::PopStyleVar(2);
            }
            const float cardReveal = smoothstep(introductionElapsed / .5f);
            if (cardReveal < 1.f) drawLoading(1.f - cardReveal, cardReveal * 12.f);
        };

        Clock clock;

        graphics.onResize([&](WindowSize size) {
            camera->aspect = size.aspect();
            camera->updateProjectionMatrix();
        });

        using SteadyClock = std::chrono::steady_clock;
        std::optional<std::pair<double,SteadyClock::time_point>> followed; // A running flight's last instant and when it drove the clock.
        SteadyClock::time_point nextFrame=SteadyClock::now(); // The frame limit's next slot.

        auto nextRefresh = SteadyClock::now() + std::chrono::seconds(ephemerides.cached ? 0 : REFRESH_SECONDS);
        auto nextOrbitRefresh = SteadyClock::now() + std::chrono::hours(orbits.cached ? 0 : 24);
        // Coverage-driven replacements wait for these: failures back off, paths recentre at most every 30 s.
        auto ephemerisRetry = SteadyClock::now(), orbitRetry = SteadyClock::now();
        // Duration of the last ephemeris refresh (seconds to a minute): tables also
        // cover the playback until they arrive, and when less than that remains
        // covered a short table is fetched first.
        double refreshSeconds = 20.;
        bool quickRefresh = false;
        double quickRate = std::numeric_limits<double>::quiet_NaN(); // Rate the last short table was planned for.
        auto refreshStarted = SteadyClock::now();
        int ephemerisFailures = 0, orbitFailures = 0;

        albert::DownloadProgress refreshProgress;
        albert::DownloadProgress orbitProgress;
        // Requests compare their epoch with this one between bodies, so a new date
        // abandons them. Declared before the futures, which wait for the requests.
        std::atomic<std::uint64_t> refreshEpoch{0};
        std::atomic_bool positionsLoading{false}; // Paths wait for position tables at the JPL queue.
        std::future<Ephemerides> pending;
        std::future<Orbits> pendingOrbits;
        struct CancelRefreshOnExit {
            albert::DownloadProgress& ephemerides;
            albert::DownloadProgress& orbits;
            ~CancelRefreshOnExit() {
                ephemerides.cancelled = true;
                orbits.cancelled = true;
            }
        } cancelRefresh{refreshProgress, orbitProgress};

        bool dataValid = true;
        int renderedFrames = 0;

        auto timeGeneration=initialTimeGeneration;
        auto simulationGeneration=initialSimulationGeneration;
        std::uint64_t ephemerisRequestEpoch=0,orbitRequestEpoch=0;
        double lastFrameMonotonic=albert::time::monotonicSeconds();
        double heldUtc=simulatedUtc; // Previous frame's simulated instant.
        // Tabulated minus analytic position per body (km), eased when a table arrives.
        struct Correction {Vec3 target, shown, from;float age=1e9f;bool tabulated=false;};
        std::map<std::string,Correction> corrections;
        std::map<std::string,albert::core::Sample> tabulatedComets; // Geocentric, for the local system.
        std::map<std::string,albert::core::StateVector> cometStates; // Heliocentric, for coma, tail and path.
        struct CometPath {double jd=std::numeric_limits<double>::quiet_NaN();std::uint64_t generation=0;std::chrono::steady_clock::time_point time{};bool tabulated=false;};
        std::map<std::string,CometPath> cometPaths;
        auto covered=[](const std::vector<State>& table,double tdb){return table.size()>=2&&tdb>=table.front().jd&&tdb<=table.back().jd;};
        while (graphics.frame([&] {
            const auto frameTime=timeService.status();
            // A flight that drove the clock and stopped: held at its last instant (the clock would run on past it).
            if(followed&&SteadyClock::now()-followed->second>std::chrono::milliseconds(500)){simulation.stall(followed->first);simulation.setRate(0);followed.reset();}
            auto moment=simulation.read();
            const float elapsed = clock.getDelta();
            const float dt = std::min(elapsed, 0.1f);
            auto steadyNow = SteadyClock::now();
            const double frameMonotonic=albert::time::monotonicSeconds();
            const bool suspended=frameMonotonic-lastFrameMonotonic>30.;
            const bool newDate=moment.generation!=simulationGeneration;
            // A chosen date stays where it was across a suspension.
            if(!newDate&&suspended&&!moment.live){simulation.stall(heldUtc);moment=simulation.read();}
            if(newDate)corrections.clear(); // No anchor at a new date: positions jump with it.
            if(frameTime.generation!=timeGeneration||newDate||suspended){
                timeGeneration=frameTime.generation;simulationGeneration=moment.generation;++refreshEpoch;nextRefresh=steadyNow;nextOrbitRefresh=steadyNow;
            }
            lastFrameMonotonic=frameMonotonic;
            heldUtc=moment.utcSeconds;
            simulatedUtc=moment.utcSeconds;
            approximatePositions=!covers(ephemerides,jdTDB(2440587.5+simulatedUtc/86400.));
            // Observed clouds belong to the last day; other dates show none.
            cloudsCurrent=std::abs(simulatedUtc-frameTime.utcSeconds)<86400.;

            planets.updateUploads();
            if (introductionStart) introductionElapsed = std::min(introductionDuration,
                std::chrono::duration<float>(steadyNow - *introductionStart).count());

            if (auto updated = downloads.poll()) {
                // Construct replacements before removing the working scene.
                if (updated->assets) {
                    planets.replaceAssets(std::move(*updated->assets));
                    auto nextBorders = std::make_shared<albert::BorderLines>(updated->borders);
                    scene->remove(*bordersObject);
                    bordersObject = std::move(nextBorders);
                    scene->add(bordersObject);
                }
                auto nextMoonOrbit = makeOrbit(updated->orbits.moon);
                auto nextEarthOrbit = makeOrbit(updated->orbits.earth);
                scene->remove(*moonOrbit);
                scene->remove(*earthOrbit);
                moonOrbit = std::move(nextMoonOrbit);
                earthOrbit = std::move(nextEarthOrbit);
                scene->add(moonOrbit);
                scene->add(earthOrbit);
                ephemerides = std::move(updated->ephemerides);
                replaceBodyOrbits(updated->orbits);
                orbits = std::move(updated->orbits);
                refreshError.clear();
                const bool current=updated->timeGeneration==frameTime.generation&&updated->simulationGeneration==moment.generation;
                nextRefresh = current ? steadyNow+std::chrono::seconds(REFRESH_SECONDS):steadyNow;
                if(!ephemerides.failures.empty()){refreshError=ephemerides.failures.front();std::cerr<<refreshError<<'\n';nextRefresh=std::min(nextRefresh,steadyNow+std::chrono::seconds(120));}
                nextOrbitRefresh = current ? steadyNow+std::chrono::hours(24):steadyNow;
                ephemerisFailures = orbitFailures = 0;
            }

            if (auto observed = cloudSync.poll(visualSettings.clouds&&cloudsCurrent); observed && observed->image) {
                planets.setCloudImage(std::move(*observed->image));
                std::cout << "Nuvens observadas: " << observed->date << " cobertura " << observed->coverage << std::endl;
            }

            // Sincronizacao das efemerides
            if (refreshing &&
                pending.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready) {
                try {
                    ephemerides = pending.get();
                    refreshSeconds = std::chrono::duration<double>(steadyNow - refreshStarted).count();
                    ephemerisFailures = 0;
                    // Live data refreshes periodically; other dates only for coverage.
                    nextRefresh = ephemerisRequestEpoch!=refreshEpoch ? steadyNow : moment.live ? steadyNow+std::chrono::seconds(REFRESH_SECONDS) : steadyNow+std::chrono::hours(24);
                    ephemerisRetry = steadyNow;
                    dataValid = true;
                    refreshError.clear();
                    // A missing comet table is retried in two minutes.
                    if (!ephemerides.failures.empty()) {refreshError = ephemerides.failures.front();std::cerr << refreshError << '\n';nextRefresh = std::min(nextRefresh, steadyNow + std::chrono::seconds(120));}
                } catch (const Superseded&) {
                    nextRefresh = steadyNow;
                } catch (const std::exception& e) {
                    nextRefresh = ephemerisRetry = steadyNow + std::chrono::seconds(std::min(REFRESH_SECONDS, 15 * (1 << std::min(ephemerisFailures++, 5))));
                    refreshError = e.what();
                    std::cerr << e.what() << '\n';
                }
                refreshing = false;
            }

            const bool tablesFollow=std::abs(moment.rate)<=TABLE_RATE_LIMIT;
            const bool ephemeridesExpiring=expiring(ephemerides,simulatedUtc,moment.rate);
            if (!refreshing && !downloads.active && tablesFollow && (steadyNow >= nextRefresh || (ephemeridesExpiring && steadyNow >= ephemerisRetry))) {
                refreshing = true;ephemerisRequestEpoch=refreshEpoch;positionsLoading=true;
                // An instant without tables, or about to leave them, starts from a short
                // table; the full one always follows.
                quickRefresh=!(quickRefresh&&quickRate==moment.rate)&&(approximatePositions||secondsCovered(ephemerides,simulatedUtc,moment.rate)<1.5*refreshSeconds);
                if(quickRefresh)quickRate=moment.rate;
                refreshStarted=steadyNow;
                // Tables arrive after the measured refresh time: cover that playback too.
                auto playback=quickRefresh?QUICK_PLAYBACK:FULL_PLAYBACK;playback.seconds+=refreshSeconds;
                pending = std::async(
                    std::launch::async,
                    [&, utc=simulatedUtc, rate=moment.rate, playback, epoch=ephemerisRequestEpoch] {
                        struct Loading {std::atomic_bool& flag;~Loading(){flag=false;}} loading{positionsLoading};
                        return loadEphemerides(utc, rate, playback, &refreshProgress, 0, false, [&refreshEpoch, epoch]{return refreshEpoch.load()!=epoch;});
                    }
                );
            }

            // Renovacao dos trajetos orbitais
            if (refreshingOrbits &&
                pendingOrbits.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready) {
                try {
                    Orbits updated = pendingOrbits.get();

                    scene->remove(*moonOrbit);
                    scene->remove(*earthOrbit);

                    moonOrbit = makeOrbit(updated.moon);

                    earthOrbit = makeOrbit(updated.earth);

                    replaceBodyOrbits(updated);
                    orbits = std::move(updated);
                    orbitFailures = 0;
                    nextOrbitRefresh = orbitRequestEpoch==refreshEpoch ? steadyNow+std::chrono::hours(24):steadyNow;
                    orbitRetry = steadyNow + std::chrono::seconds(30);

                    scene->add(moonOrbit);
                    scene->add(earthOrbit);
                } catch (const Superseded&) {
                    nextOrbitRefresh = steadyNow;
                } catch (const std::exception& e) {
                    nextOrbitRefresh = orbitRetry = steadyNow + std::chrono::seconds(std::min(300, 30 * (1 << std::min(orbitFailures++, 4))));
                    refreshError = e.what();
                    std::cerr << e.what() << '\n';
                }
                refreshingOrbits = false;
            }

            // Paths drift slowly (the Moon's nodes and apsides): recentre them after
            // 60 simulated days. Positions come first: no path request while they load.
            const bool orbitsDrifted=std::abs(simulatedUtc-orbits.centerUtc)>60.*86400.;
            if (!refreshingOrbits && !refreshing && !downloads.active && tablesFollow && (steadyNow >= nextOrbitRefresh || (orbitsDrifted && steadyNow >= orbitRetry))) {
                refreshingOrbits = true;orbitRequestEpoch=refreshEpoch;

                pendingOrbits = std::async(
                    std::launch::async,
                    [&, center=simulatedUtc, epoch=orbitRequestEpoch] { return loadOrbits(center, &orbitProgress, 0, false, [&refreshEpoch, epoch]{return refreshEpoch.load()!=epoch;}, [&positionsLoading]{return positionsLoading.load();}); }
                );
            }

            commands.drain([&](albert::core::Command& command) {
                std::visit([&](auto& value) {
                    using T=std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<T,albert::core::Create>) registry.create(std::move(value.object),value.replace);
                    else if constexpr(std::is_same_v<T,albert::core::Remove>) {if(registry.get(value.id).builtin)throw std::invalid_argument("Built-in objects cannot be removed");if(focus==value.id){Focus next=targets::Earth; // A flight followed ended: on to one still flying (the one it left from first), else the Earth.
                        for(const auto& [id,object]:registry.objects())if(id!=value.id&&(id=="flight"||id.rfind("flight.",0)==0)&&next!="flight")next=id;
                        setFocus(next);}
                        registry.remove(value.id);visualSettings.auxiliary.erase(value.id);}
                    else if constexpr(std::is_same_v<T,albert::core::Visibility>) registry.visible(value.id,value.enabled);
                    else if constexpr(std::is_same_v<T,albert::core::Append>) registry.samples(value.id,std::move(value.values));
                    else if constexpr(std::is_same_v<T,albert::core::Replace>)registry.replace(value.id,std::move(value.values));
                    else if constexpr(std::is_same_v<T,albert::core::InstallModel>)registry.installModel(std::move(value.model));
                    else if constexpr(std::is_same_v<T,albert::core::RemoveModel>)registry.removeModel(value.id);
                    else if constexpr(std::is_same_v<T,albert::core::CreateLayer>)registry.createLayer(std::move(value.layer));
                    else if constexpr(std::is_same_v<T,albert::core::RemoveLayer>)registry.removeLayer(value.id);
                    else if constexpr(std::is_same_v<T,albert::core::LayerVisibility>)registry.layerVisible(value.id,value.enabled);
                    else if constexpr(std::is_same_v<T,albert::core::Focus>){
                        // From one vehicle to another beside it (a stage just separated): the same view, eased onto it (no swing to
                        // its default distance); else as asked.
                        const bool nearby=!value.reset&&focus.front()!='@'&&!registry.get(focus).builtin&&!registry.get(value.id).builtin&&targetFor(focus).distanceTo(targetFor(value.id))*albert::core::earthRadiusMeters<1e3;
                        if(nearby){Vector3 offset=camera->position;offset.sub(controls.target);setFocus(value.id,false,offset,offset.length());}else setFocus(value.id,value.reset);}
                    else if constexpr(std::is_same_v<T,albert::core::Capture>){
                        const char* directory=std::getenv("ALBERT_CAPTURE_DIR");const fs::path path=directory?fs::path(directory):fs::current_path()/"captures";
                        fs::create_directories(path);pendingCaptures.push_back(path/value.fileName);}
                    else if constexpr(std::is_same_v<T,albert::core::SetTime>) {if(value.live)simulation.live();
                        else if(value.hold&&value.utcSeconds&&value.rate){simulation.follow(*value.utcSeconds,*value.rate);followed={*value.utcSeconds,SteadyClock::now()};} // A running flight.
                        else if(value.hold&&value.utcSeconds)simulation.stall(*value.utcSeconds);else simulation.choose(value.utcSeconds,value.rate);}
                    else if constexpr(std::is_same_v<T,albert::core::FlightTelemetry>) {if(value.active){flightTelemetry=std::move(value);flightTelemetryAt=steadyNow;}else flightTelemetry.reset();}
                    else if constexpr(std::is_same_v<T,albert::core::Orbit>) {
                        if(!std::isfinite(value.azimuthRadians)||!std::isfinite(value.elevationRadians)||!std::isfinite(value.distanceScale)||value.distanceScale<=0||value.distanceScale>1000||std::abs(value.azimuthRadians)>2*PI||std::abs(value.elevationRadians)>PI)
                            throw std::invalid_argument("Invalid orbit parameters");
                        automaticOrbit=false;automaticOrbitAge=0.f;controls.autoRotate=false;
                        controls.dampingFactor=1;controls.update();transition.active=false;
                        Vector3 offset=camera->position;offset.sub(controls.target);Spherical orbit;orbit.setFromVector3(offset);
                        orbit.theta+=static_cast<float>(value.azimuthRadians);orbit.phi=std::clamp(orbit.phi+static_cast<float>(value.elevationRadians),.001f,static_cast<float>(PI)-.001f);
                        orbit.radius=std::clamp(orbit.radius*static_cast<float>(value.distanceScale),minimumFor(focus),controls.maxDistance);
                        zoomTarget=zoomDisplayed=orbit.radius;controls.minDistance=minimumFor(focus);
                        camera->position.copy(controls.target).add(offset.setFromSpherical(orbit));camera->lookAt(controls.target);registry.cameraChanged();
                    }
                },command);
                return albert::core::Reply{albert::core::Code::Ok,{},registry.revision()};
            });

            // Posicionamento fisico dos corpos
            double utc = 2440587.5+simulatedUtc/86400.;
            double tdb = jdTDB(utc);
            double c2t[3][3];terrestrial(utc,c2t); // The Earth's frame: its globe's and the objects fixed on it.
            registry.builtinFrame("earth",simulatedUtc,{c2t[0][0],c2t[0][1],c2t[0][2],c2t[1][0],c2t[1][1],c2t[1][2],c2t[2][0],c2t[2][1],c2t[2][2]},
                2*std::numbers::pi*1.00273781191135448/86400.);

            registry.builtinState("earth",{simulatedUtc,{0,0,0}});
            try {
                // JPL tables where they cover the instant. Elsewhere the ERFA analytic models,
                // shifted by their last difference from the tables (none at a new date), so
                // positions never stop while tables load; the difference eases in on arrival.
                constexpr float CORRECTION_EASE = .6f;
                // Comets fall back to their two-body elements instead of an ERFA model.
                auto place=[&](const std::string& id,const std::vector<State>& table,std::optional<Vec3> fallback={})->Vec3{
                    const auto model=albert::core::approximateGeocentric(id,tdb);
                    if(!model&&!fallback)return interpolate(table,tdb);
                    const Vec3 analytic=fallback?*fallback:Vec3{model->x/1000.,model->y/1000.,model->z/1000.};
                    const bool fresh=!corrections.contains(id);auto& c=corrections[id];
                    const bool tabulated=table.size()>=2&&tdb>=table.front().jd&&tdb<=table.back().jd;
                    if(tabulated){
                        if(!c.tabulated&&!fresh){c.from=c.shown;c.age=0.f;}
                        c.target=interpolate(table,tdb)-analytic;
                    }
                    c.tabulated=tabulated;c.age+=elapsed; // Real seconds: slow frames still finish the ease.
                    c.shown=c.age<CORRECTION_EASE?c.from+(c.target-c.from)*static_cast<double>(smoothstep(c.age/CORRECTION_EASE)):c.target;
                    return analytic+c.shown;
                };
                Vec3 sunState = place("sun", ephemerides.sun);
                Vec3 moonState = place("moon", ephemerides.moon);
                // Geometric states: the heliocentric Earth is the opposite of the geocentric Sun.
                Vec3 earthHelio = sunState * -1.;

                const double timestamp=simulatedUtc;
                registry.builtinState("moon",{timestamp,{moonState.x*1000.,moonState.y*1000.,moonState.z*1000.}});
                registry.builtinState("sun",{timestamp,{sunState.x*1000.,sunState.y*1000.,sunState.z*1000.}});
                for(const auto& [id,samples]:ephemerides.bodies){const auto value=place(id,samples);registry.builtinState(id,{timestamp,{value.x*1000.,value.y*1000.,value.z*1000.}});}
                // Comets: Horizons tables for the apparition of the date, else the elements.
                tabulatedComets.clear();cometStates.clear();
                const bool sunMoves=covered(ephemerides.sun,tdb);
                static const std::vector<State> untabulated;
                for(const auto& comet:albert::core::cometActivities()){
                    const std::string id(comet.id);const auto table=ephemerides.comets.find(id);
                    const auto& samples=table!=ephemerides.comets.end()?table->second:untabulated; // Failed: elements.
                    const auto* orbit=albert::core::localOrbit(id);if(!orbit)continue;
                    const auto local=albert::core::localState(*orbit,tdb);
                    const Vec3 elements=Vec3{local.positionMeters.x/1000.,local.positionMeters.y/1000.,local.positionMeters.z/1000.}+sunState;
                    const Vec3 value=place(id,samples,elements);
                    // Velocities from the tables where they cover the instant, else the elements
                    // (heliocentric) plus the Sun's tabulated geocentric motion when known.
                    const Vec3 sunVelocity=sunMoves?interpolateVelocity(ephemerides.sun,tdb):Vec3{};
                    const Vec3 orbital{local.velocityMetersPerSecond.x/1000.,local.velocityMetersPerSecond.y/1000.,local.velocityMetersPerSecond.z/1000.};
                    const Vec3 geocentric=covered(samples,tdb)?interpolateVelocity(samples,tdb):orbital+sunVelocity;
                    const Vec3 helio=value-sunState,helioVelocity=sunMoves?geocentric-sunVelocity:orbital;
                    cometStates[id]={{helio.x*1000.,helio.y*1000.,helio.z*1000.},{helioVelocity.x*1000.,helioVelocity.y*1000.,helioVelocity.z*1000.}};
                    tabulatedComets[id]=albert::core::Sample{timestamp,{value.x*1000.,value.y*1000.,value.z*1000.},{geocentric.x*1000.,geocentric.y*1000.,geocentric.z*1000.},std::nullopt,sunMoves||covered(samples,tdb)};
                }
                // Orbitas heliocentricas convertidas
                // para a origem geocentrica da cena.
                earthOrbit->origin=earthHelio;
                updateOrbitTime(moonOrbit, tdb);
                updateOrbitTime(earthOrbit, tdb);
                for(const auto& [id,path]:bodyOrbits){path->origin=earthHelio;updateOrbitTime(path,tdb);}

                dataValid = true;
            } catch (const std::exception& e) {
                if (smokeTest) throw;
                if (dataValid) {
                    std::cerr << e.what() << '\n';
                }

                dataValid = false;
                refreshError = e.what();
            }

            // Provider failure holds built-ins; independent objects and camera still advance.
            albert::core::updateLocalSystem(registry,simulatedUtc,tdb,tabulatedComets);
            // The floating origin follows the eye, flying or orbiting a focus (the orbit's follow then moves the camera with it): about a
            // body's centre, a unit off, every part near the camera was ~0.4 m off its float place, and the parts of the tower, the
            // vehicles and the ground's nodes shimmered and fought.
            const bool eyeOrigin=true;
            const auto nextOriginId=eyeOrigin?Focus("@flight"):targetId(focus);
            // During flight the floating origin follows the eye, preserving the
            // departure body's detail instead of rebasing immediately to a planet AU away.
            const auto nextOrigin=eyeOrigin?sceneOrigin+albert::core::Vector{
                camera->position.x*albert::core::earthRadiusMeters,-camera->position.z*albert::core::earthRadiusMeters,camera->position.y*albert::core::earthRadiusMeters}:
                registry.evaluate(nextOriginId,simulatedUtc).state.positionMeters;
            const auto shift=albert::renderPosition(sceneOrigin,nextOrigin);
            if(eyeOrigin||originId!=nextOriginId){camera->position.add(shift);controls.target.add(shift);originId=nextOriginId;}
            if(transition.active)transition.startTarget.add(shift);
            sceneOrigin=nextOrigin;
#ifdef ALBERT_HAS_RPC
            if(rpc)for(const auto& x:rpc->flightSounds())if(x.id.rfind("site",0)==0&&x.id.find('/')==std::string::npos)objects.deluge(x.id,static_cast<float>(x.emission.pad.waterFlow/1000)); // Its pad's deluge (kg/s) as the trench's spray.
            if(rpc)for(const auto& g:rpc->flightGauges())objects.count(g.id,g.countState=="HOLD"?g.countTime:simulatedUtc-g.zero,g.countState=="SCRUB"); // The vents' pressurisation closing.
#endif
            objects.delugeEffects(preferences.deluge);
            objects.sync(registry,simulatedUtc,sceneOrigin);
            earthPosition.copy(earth->position);
            sunPosition.copy(sun->position);
            moonPosition.copy(moon->position);
            bordersObject->position.copy(earthPosition);

            sun->position.copy(sunPosition);
            moon->position.copy(moonPosition);

            lightTarget->position.copy(targetFor(focus));
            Vector3 direction = sunPosition;direction.sub(lightTarget->position);
            if(direction.lengthSq()<1e-12f){lightTarget->position.copy(earthPosition);direction.copy(sunPosition).sub(earthPosition);}
            direction.normalize();
            sunlight->position.copy(direction).multiplyScalar(150.f).add(lightTarget->position);
            sun->lookAt(earthPosition);
            moon->setRotationFromMatrix(albert::moonOrientation(tdb));
            if(pendingEvent&&pendingEvent->generation==simulationGeneration){
                const auto& event=*pendingEvent->event;std::optional<Vector3> arrival;
                if(event.view==EventView::FromSun)arrival=Vector3(sunPosition).sub(earthPosition);
                else if(event.view==EventView::FromEarth)arrival=Vector3(earthPosition).sub(moonPosition);
                // Comets: far enough to frame the coma and the young tail (beta 0.1, ten days).
                std::optional<float> distance;
                if(const auto state=cometStates.find(event.focus);state!=cometStates.end()){
                    const auto& p=state->second.position;const double r=std::hypot(p.x,p.y,p.z);
                    const double tail=.5*.1*albert::core::solarGM/(r*r)*std::pow(10*86400.,2);
                    distance=static_cast<float>(std::max(30*albert::core::radiationApex(*albert::core::cometActivity(event.focus),r/149597870700.),.5*tail)/albert::core::earthRadiusMeters);
                }
                setFocus(event.focus,false,arrival,distance);if(event.enhance)comets.enhance(event.focus,true);
                pendingEvent.reset();
            }

            auto rotation = earthOrientation(c2t);
            earth->setRotationFromMatrix(rotation);
            bordersObject->setRotationFromMatrix(rotation);


            orbitVisibility=albert::animateValue(orbitVisibility,showOrbits ? 1.f : 0.f,dt);
            borderVisibility=albert::animateValue(borderVisibility,visualSettings.borders ? 1.f : 0.f,dt);
            bordersObject->visible = borderVisibility>0.f;
            // Show the selected physical path in close views, and both in context.
            moonOrbit->visible = orbitVisibility>0.f && focus != targets::Sun;
            earthOrbit->visible = orbitVisibility>0.f && (focus == targets::Earth || focus == targets::Sun || systemView(focus));

            for(const auto& [id,path]:bodyOrbits)path->visible=orbitVisibility>0.f&&(focus==id||systemView(focus)||focus==targets::Sun);

            // Camera segue o objeto sem acumular erro
            Vector3 target = targetFor(focus);

            if (transition.active) {
                // A slow upload/shader frame must not skip a chunk of the flight.
                // Astronomical time continues independently of this presentation clock.
                transition.elapsed += std::min(elapsed,.1f);

                const float progress=std::clamp(transition.elapsed/transition.duration,0.f,1.f);
                const float destinationDistance=transition.arrivalDistance.value_or(distanceFor(transition.destination));
                const float startDistance=std::max(1e-9f,transition.startOffset.length());
                if(transition.preserveDirection){
                    const float distance=albert::framingDistance(startDistance,destinationDistance,albert::flightEase(progress));
                    controls.target.copy(target);camera->position.copy(target).add(transition.startOffset.clone().normalize().multiplyScalar(distance));
                }else{
                    // Overlap movement and orientation instead of stopping at
                    // phase boundaries. Context views open directly to their
                    // final framing, without a distant overshoot and return.
                    const bool context=transition.destination.front()=='@';
                    const float retreatDistance=startDistance*1.35f;
                    Vector3 startEye=transition.startTarget;startEye.add(transition.startOffset);
                    const float startRange=startEye.distanceTo(target);
                    const float arrivalDistance=context ? destinationDistance :
                        std::max(destinationDistance,std::min(destinationDistance*12.f,startRange));
                    Vector3 departure=transition.startTarget;departure.add(transition.startOffset.clone().normalize().multiplyScalar(retreatDistance));
                    Vector3 arrival=target;arrival.add(transition.arrivalDirection.clone().multiplyScalar(arrivalDistance));
                    const float retreat=albert::flightEase(progress/.34f);
                    const float transit=albert::flightTravel(albert::flightEase((progress-.12f)/(context ? .88f : .62f)),departure.distanceTo(arrival),startDistance,arrivalDistance);
                    const float approach=context ? 0.f : albert::flightEase((progress-.48f)/.52f);
                    camera->position.copy(lerpVector(lerpVector(startEye,departure,retreat),arrival,transit));
                    camera->position.add(transition.arrivalDirection.clone().multiplyScalar(
                        (albert::framingDistance(arrivalDistance,destinationDistance,approach)-arrivalDistance)*transit));
                    const float aim=albert::flightEase(progress/(context ? .95f : .70f));
                    Vector3 oldDirection=transition.startOffset;oldDirection.negate().normalize();
                    Vector3 newDirection=target;newDirection.sub(camera->position);
                    const float range=newDirection.length();
                    if(range<1e-9f)newDirection.copy(transition.arrivalDirection).negate();else newDirection.divideScalar(range);
                    const auto gaze=arcDirection(oldDirection,newDirection,aim);
                    controls.target.copy(camera->position).add(gaze.clone().multiplyScalar(std::lerp(startDistance,std::max(1e-9f,range),aim)));
                }

                controls.minDistance = std::min(.05f,minimumFor(transition.destination));

                if (transition.elapsed >= transition.duration) {
                    transition.active = false;
                    automaticOrbit=focus.front()!='@'&&preferences.autoRotate;automaticOrbitAge=0.f;
                    zoomTarget = zoomDisplayed = transition.arrivalDistance.value_or(distanceFor(focus));
                    controls.minDistance = minimumFor(focus);
                }
            } else if (freeCamera) {
                // Free flight: the nearest surface gives the local up and the height, which sets the speed.
                const auto& io=ImGui::GetIO();const float r=static_cast<float>(albert::core::earthRadiusMeters);
                Object3D* closest=earth.get();float closestRadius=1.f,best=camera->position.distanceTo(earth->position)-1.f;const albert::Terrain* closestTerrain=planets.earthTerrain();
                auto consider=[&](Object3D* body,float radius,const albert::Terrain* terrain){const float h=camera->position.distanceTo(body->position)-radius;
                    if(h<best){best=h;closest=body;closestRadius=radius;closestTerrain=terrain;}};
                consider(moon.get(),static_cast<float>(1737.4/6378.137),planets.moonTerrain());
                for(const auto& [id,node]:solids.nodes())if(albert::core::surfaceBody(id))consider(node.get(),static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters),solids.terrain(id));
                auto floorUnder=[&](){closest->updateMatrixWorld();Vector3 eye=camera->position;closest->worldToLocal(eye);
                    const auto ground=closestTerrain?closestTerrain->ground(eye):std::nullopt;return std::pair{eye,ground?static_cast<float>(*ground):closestRadius};};
                const auto [eye,floor]=floorUnder();const float height=std::max(eye.length()-floor,0.f); // World units: the bodies are unscaled spheres.
                Vector3 up=camera->position;up.sub(closest->position).normalize();
                Vector3 forward;camera->getWorldDirection(forward);
                if(!io.WantCaptureMouse&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){ // Look: yaw about the local up, pitch about the right.
                    const float look=.003f*preferences.sensitivity;forward.applyAxisAngle(up,-io.MouseDelta.x*look);Vector3 side=forward;side.cross(up).normalize();
                    Vector3 pitched=forward;pitched.applyAxisAngle(side,-io.MouseDelta.y*look*(preferences.invertY?-1.f:1.f));if(std::abs(pitched.dot(up))<.995f)forward=pitched;}
                if(!io.WantCaptureMouse&&io.MouseWheel!=0.f)freeSpeed=std::clamp(freeSpeed*std::pow(1.25f,io.MouseWheel),.01f,1000.f);
                const float speed=std::max(2.f,.6f*height*r)/r*freeSpeed*(io.KeyShift?5.f:1.f);
                Vector3 side=forward;side.cross(up).normalize();Vector3 move;
                if(!io.WantCaptureKeyboard){
                    if(ImGui::IsKeyDown(ImGuiKey_W))move.add(forward);
                    if(ImGui::IsKeyDown(ImGuiKey_S))move.sub(forward);
                    if(ImGui::IsKeyDown(ImGuiKey_D))move.add(side);
                    if(ImGui::IsKeyDown(ImGuiKey_A))move.sub(side);
                    if(ImGui::IsKeyDown(ImGuiKey_Space)||ImGui::IsKeyDown(ImGuiKey_E))move.add(up);
                    if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl)||ImGui::IsKeyDown(ImGuiKey_Q))move.sub(up);}
                if(move.lengthSq()>0)camera->position.add(move.normalize().multiplyScalar(speed*dt));
                { // Never below the drawn ground under the eye: 2 m over it.
                    auto [now,under]=floorUnder();const float minimum=under+2.f/r;
                    if(now.length()<minimum){now.multiplyScalar(minimum/std::max(now.length(),1e-12f));closest->localToWorld(now);camera->position.copy(now);}}
                camera->up.copy(up);Vector3 look=camera->position;look.add(forward);camera->lookAt(look);
                controls.target.copy(camera->position).add(forward.clone().multiplyScalar(std::max(height,100.f/r)));
            } else {
                // Acompanha objetos em movimento.
                Vector3 movement = target;
                movement.sub(controls.target);

                camera->position.add(movement);
                controls.target.copy(target);
                // Around a vehicle (anything not a body): orbiting about the local vertical under it, the horizon level, eased in;
                // about the scene's axis again around the bodies themselves.
                Vector3 wanted(0,1,0);
                if(focus.front()!='@'&&!registry.get(focus).builtin){const Vector3 earth=targetFor(targets::Earth),moon=targetFor(targets::Moon); // The nearer body, by its size.
                    Vector3 away=target;away.sub(target.distanceTo(moon)<.273f*target.distanceTo(earth)?moon:earth);if(away.lengthSq()>0)wanted=away.normalize();}
                camera->up.lerp(wanted,1.f-std::exp(-4.f*dt)).normalize();
            }

            // Damping de rotacao e zoom do mouse
            controls.enabled =
                introductionElapsed >= introductionDuration &&
                !transition.active && !freeCamera &&
                !ImGui::GetIO().WantCaptureMouse;

            const float interactionRadius=radiusFor(focus);
            const float relativeAltitude=interactionRadius>0 ? std::max(0.f,camera->position.distanceTo(controls.target)-interactionRadius)/interactionRadius : 1.f;
            controls.rotateSpeed=.75f*std::clamp(relativeAltitude,.025f,1.f)*preferences.sensitivity;
            if(camera->fov!=preferences.fov){camera->fov=preferences.fov;camera->updateProjectionMatrix();}
            controls.zoomSpeed=std::clamp(relativeAltitude,.18f,1.f);
            automaticOrbitAge=automaticOrbit ? automaticOrbitAge+dt : 0.f;
            controls.autoRotate=automaticOrbit&&controls.enabled;
            // OrbitControls uses frame-based increments: compensate for actual dt.
            controls.autoRotateSpeed=.12f*dt*60.f*albert::flightEase(automaticOrbitAge);
            controls.dampingFactor = 1.f - std::exp(-8.f * dt);
            if(!freeCamera)controls.update();
            if (freeCamera) {
                zoomTarget = zoomDisplayed = camera->position.distanceTo(controls.target);
            } else if (transition.active) {
                // Flush old drag inertia so it cannot alter a programmed flight.
                controls.dampingFactor = 1.f;
                controls.update();
                zoomTarget = zoomDisplayed = camera->position.distanceTo(controls.target);
            } else {
                Vector3 offset = camera->position;
                offset.sub(controls.target);
                const float requested = std::max(1e-9f, offset.length());
                const float zoomDelta = requested / std::max(1e-9f, zoomDisplayed);
                if (std::abs(std::log(zoomDelta)) > .0001f) zoomTarget *= zoomDelta;
                zoomTarget = std::clamp(zoomTarget, controls.minDistance, controls.maxDistance);
                zoomDisplayed = std::exp(std::lerp(std::log(std::max(1e-9f, zoomDisplayed)), std::log(zoomTarget), 1.f - std::exp(-14.f * dt)));
                camera->position.copy(controls.target).add(offset.normalize().multiplyScalar(zoomDisplayed));
                camera->lookAt(controls.target);
            }
            // An external target near a solid surface: keep the eye above the ground under it, and above the vehicle's foot
            // while it stands on (or just over) the ground (the orbit offset can point below a pad); in flight, free to look up at it.
            if(!transition.active&&!freeCamera&&focus.front()!='@'&&objects.nodes().contains(focus)&&!registry.get(focus).builtin){
                const float base=static_cast<float>(registry.get(focus).radiusMeters/albert::core::earthRadiusMeters),margin=static_cast<float>(10./albert::core::earthRadiusMeters);
                auto keepAbove=[&](Object3D& body,float radius,const albert::Terrain* terrain){
                    body.updateMatrixWorld();Vector3 eye=camera->position,aim=controls.target;body.worldToLocal(eye);body.worldToLocal(aim);
                    if(aim.length()>radius*1.05f)return;
                    const auto ground=terrain?terrain->ground(eye):std::nullopt,under=terrain?terrain->ground(aim):std::nullopt; // The drawn relief, else the reference sphere.
                    const float foot=aim.length()-base,floor=std::max(ground?static_cast<float>(*ground):radius,foot<(under?static_cast<float>(*under):radius)+base?foot:0.f)+margin;
                    if(eye.length()>=floor)return;
                    eye.multiplyScalar(floor/std::max(eye.length(),1e-12f));body.localToWorld(eye);camera->position.copy(eye);camera->lookAt(controls.target);
                };
                keepAbove(*earth,1.f,planets.earthTerrain());keepAbove(*moon,static_cast<float>(1737.4/6378.137),planets.moonTerrain());
                for(const auto& [id,node]:solids.nodes())if(albert::core::surfaceBody(id))keepAbove(*node,static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters),solids.terrain(id));
            }

            stars->position.copy(camera->position);
            albert::setStarEpoch(*stars, tdb); // Proper motion for the simulated date.
            const float distance = camera->position.distanceTo(controls.target);
            controls.maxDistance=std::max(65000.f,distanceFor(targets::Overview)*2.f);
            controls.maxDistance=std::max(controls.maxDistance,static_cast<float>(150000.*149597870700./albert::core::earthRadiusMeters));
            camera->farPlane=std::max({100000.f,distance*4.f,distanceFor(targets::Overview)*2.f});
            debris.update(sunPosition,camera->position,showDebris,elapsed);
            const float bodyRadius = radiusFor(focus);
            // Clear of the focus and of the ground under the eye: the drawn relief, else the reference
            // sphere when above it (a far orbit dragged down to a lander's horizon must not clip the ground).
            float ground=std::numeric_limits<float>::max();
            auto over=[&](Object3D& body,float radius,const albert::Terrain* terrain){
                body.updateMatrixWorld();Vector3 eye=camera->position;body.worldToLocal(eye);
                if(const auto g=terrain?terrain->ground(eye):std::nullopt)ground=std::min(ground,eye.length()-static_cast<float>(*g));
                else if(eye.length()>radius)ground=std::min(ground,eye.length()-radius);
            };
            over(*earth,1.f,planets.earthTerrain());over(*moon,static_cast<float>(1737.4/6378.137),planets.moonTerrain());
            for(const auto& [id,node]:solids.nodes())if(albert::core::surfaceBody(id))over(*node,static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters),solids.terrain(id));
            const float clearance = std::max(1e-8f, std::min(distance - bodyRadius, ground));
            camera->nearPlane = std::clamp(std::min(distance * .02f, clearance * .25f), 1e-9f, 100.f);
            camera->updateProjectionMatrix();
            const auto display=ImGui::GetIO().DisplaySize;
            camera->updateMatrixWorld();
            if(systemView(focus)){
                std::vector<ImVec2> occupied;
                auto screenPoint=[&](const Vector3& position)->std::optional<ImVec2>{
                    Vector3 projected=position;projected.project(*camera);
                    if(projected.z< -1||projected.z>1||std::abs(projected.x)>1.1f||std::abs(projected.y)>1.1f)return std::nullopt;
                    return ImVec2((projected.x*.5f+.5f)*display.x,(.5f-projected.y*.5f)*display.y);
                };
                for(const auto& [id,node]:objects.nodes())if(node->visible&&(id=="earth"||id=="sun"||albert::core::naturalBody(id)))if(auto point=screenPoint(node->position))occupied.push_back(*point);
                std::vector<const albert::core::LocalOrbit*> candidates;
                for(const auto& body:albert::core::localOrbits())if(body.category!=albert::core::Category::Moon)candidates.push_back(&body);
                std::sort(candidates.begin(),candidates.end(),[](const auto* a,const auto* b){if(a->category!=b->category)return a->category<b->category;return a->radius>b->radius;});
                for(const auto* body:candidates){
                    const auto& node=objects.nodes().at(std::string(body->id));const auto point=screenPoint(node->position);
                    float& visibility=secondaryVisibility[std::string(body->id)];
                    const float gap=visibility>.5f ? 21.f : 29.f;
                    const bool crowded=point&&std::any_of(occupied.begin(),occupied.end(),[&](const auto& other){return std::abs(point->x-other.x)<gap*1.6f&&std::abs(point->y-other.y)<gap;});
                    const bool visible=point&&!crowded&&camera->position.distanceTo(node->position)<distanceFor(focus)*5.f;
                    visibility=albert::animateValue(visibility,visible?1.f:0.f,dt);
                    if(visible)occupied.push_back(*point);
                }
            }
            auto orbitView=[&](const std::shared_ptr<Mesh>& path, const Vector3& body, float daysPerUnit,float visibility=1.f) {
                auto* material=path->material()->as<ShaderMaterial>();
                material->uniforms.at("referenceVisibility").setValue(1.f);
                const float bodyDistance=std::max(.00001f,camera->position.distanceTo(body));
                const float reference=distanceFor(focus)*(focus.front()=='@' ? 1.f : 6.f);
                const float magnification=std::clamp(std::pow(reference/bodyDistance,.22f),.65f,2.25f);
                material->uniforms.at("strokeWidth").setValue(3.2f*magnification);
                material->uniforms.at("visibility").setValue(orbitVisibility*visibility);
                material->uniforms.at("viewport").setValue(Vector2(display.x,display.y));
                material->uniforms.at("nearClip").setValue(camera->nearPlane);
                material->uniforms.at("dashDays").setValue(std::max(.0001f,
                    camera->position.distanceTo(body)*.93f*24.f*magnification/std::max(1.f,display.y)*daysPerUnit));
            };
            if(moonOrbit->visible) moonOrbit->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
            if(earthOrbit->visible) earthOrbit->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
            orbitView(moonOrbit,moonPosition,.0721f);
            orbitView(earthOrbit,earthPosition,.00248f);
            for(const auto& [id,path]:bodyOrbits)if(path->visible){
                path->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
                const auto& samples=orbits.bodies.at(id);
                const auto sample=std::lower_bound(samples.begin(),samples.end(),tdb,[](const State& s,double t){return s.jd<t;});
                const auto& velocity=(sample==samples.end()?samples.back():*sample).velocity;
                const double speed=std::hypot(velocity.x,velocity.y,velocity.z);
                orbitView(path,solids.nodes().at(id)->position,static_cast<float>(EARTH_RADIUS/(std::max(speed,.001)*86400.)));
            }
            // Comet paths follow the osculating orbit of their current state (tables of the
            // apparition, or anchored elements): redrawn after 20 simulated days, a new date
            // or a change of source (once its ease ends), at most every two seconds.
            for(const auto& [id,state]:cometStates){
                auto& built=cometPaths[id];const auto& source=corrections.at(id);
                const bool current=std::abs(tdb-built.jd)<20.&&built.generation==simulationGeneration&&built.tabulated==source.tabulated;
                if(current||source.age<.6f||steadyNow-built.time<std::chrono::seconds(2))continue;
                const auto revolution=albert::core::revolution(state,tdb,256);if(revolution.empty())continue;
                std::vector<State> samples;samples.reserve(revolution.size());
                for(const auto& [jd,s]:revolution)samples.push_back({jd,{s.position.x/1000.,s.position.y/1000.,s.position.z/1000.},{s.velocity.x/1000.,s.velocity.y/1000.,s.velocity.z/1000.}});
                auto& local=localPaths.at(id);auto next=makeOrbit(samples);scene->remove(*local.path);scene->add(next);
                local={next,(samples.front().jd+samples.back().jd)*.5,samples.back().jd-samples.front().jd};
                built={tdb,simulationGeneration,steadyNow,source.tabulated};
            }
            for(const auto& body:albert::core::localOrbits()){
                auto& orbit=localPaths.at(std::string(body.id));
                const float secondary=systemView(focus)&&body.category!=albert::core::Category::Moon ? secondaryVisibility[std::string(body.id)] : 1.f;
                const auto* focused=albert::core::localOrbit(focus);
                const bool family=body.parent==focus||(focused&&focused->category==albert::core::Category::Moon&&body.parent==focused->parent);
                orbit.path->visible=orbitVisibility*secondary>.001f&&(body.id==focus||family||((systemView(focus)||focus==targets::Sun)&&body.category!=albert::core::Category::Moon));
                if(!orbit.path->visible)continue;
                const auto parent=registry.evaluate(std::string(body.parent),simulatedUtc).state.positionMeters;
                orbit.path->origin={-parent.x/1000.,-parent.y/1000.,-parent.z/1000.};
                orbit.path->material()->as<ShaderMaterial>()->uniforms.at("present").setValue(static_cast<float>(std::remainder(tdb-orbit.epoch,orbit.period)));
                orbit.path->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
                const auto state=albert::core::localState(body,tdb);const double speed=std::hypot(state.velocityMetersPerSecond.x,state.velocityMetersPerSecond.y,state.velocityMetersPerSecond.z);
                orbitView(orbit.path,objects.nodes().at(std::string(body.id))->position,static_cast<float>(albert::core::earthRadiusMeters/(speed*86400.)),secondary);
            }
            // Photographic exposure adapts to the irradiance at the focused planet;
            // physical solar illumination and geometry remain unchanged.
            // Under Earth's air (below ~30 km) the view is mostly sky and lit ground, not a lit planet on
            // black: one stop more, as a camera on the ground would take.
            const float airAltitude=(camera->position.distanceTo(planets.earth->position)-1.f)*6378.137f;
            // The exposure of the focus, or of a surface within three radii of the camera (a lander's view).
            std::string exposed=focus;
            if(camera->position.distanceTo(planets.moon->position)<3.f*static_cast<float>(1737.4/6378.137))exposed=targets::Moon;
            for(const auto& [id,node]:solids.nodes())if(camera->position.distanceTo(node->position)<3.f*static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters))exposed=id;
            // The sunlight where the camera is: near another body its irradiance at that distance (Mars:
            // 2.4 / 1.52²), so its sky, air light, terrain and vehicles share one scale; the exposure lifts it.
            const float bodyAu=solids.contains(exposed)?sunPosition.distanceTo(solids.nodes().at(exposed)->position)*6378137.f/149597870700.f:1.f;
            // It glides with the exposure (same filter in log space), so their product never steps at the 3-radius boundary.
            sunlight->intensity=std::exp(std::lerp(std::log(sunlight->intensity),std::log(2.4f/(bodyAu*bodyAu)),-std::expm1(-dt/.65f)));
            // At the Moon the eye adapts in the dark: past quarter phase the exposure follows the light it sends (earthshine on the night side).
            float adaptation=1;
            if(exposed==targets::Moon){const Vector3 m=planets.moon->position,e=planets.earth->position;
                auto angle=[](Vector3 a,Vector3 b){return std::acos(std::clamp(static_cast<double>(a.normalize().dot(b.normalize())),-1.,1.))*180/PI;};
                adaptation=albert::lunarAdaptation(angle(sunPosition.clone().sub(m),camera->position.clone().sub(m)),angle(e.clone().sub(m),camera->position.clone().sub(m)),
                    angle(sunPosition.clone().sub(e),m.clone().sub(e)),m.distanceTo(e)/planets.earth->scale.x);}
            const float photographicExposure=(exposed==targets::Moon?2.1f*adaptation:solids.contains(exposed)?std::clamp(1.05f*bodyAu*bodyAu,.25f,1200.f):1.05f)
                *(1.f+planets.atmosphereVisibility()*(1.f-std::clamp(airAltitude/30.f,0.f,1.f)));
            graphics.setExposure(std::exp(std::lerp(std::log(graphics.exposure()),std::log(photographicExposure),-std::expm1(-dt/.65f))));
            // The body whose regional detail the view wants: the focus, or the body under a focused vehicle near
            // its ground with the camera (its site: the Cape, a lunar or Martian lander).
            std::string detailFocus=std::string(targetId(focus));
            if(objects.nodes().contains(focus)&&!albert::core::naturalBody(focus)&&focus!="earth"&&focus!="sun"){
                const Vector3 vehicle=objects.nodes().at(focus)->position;
                auto over=[&](const Object3D& body,float radius){return camera->position.distanceTo(body.position)<1.05f*radius&&vehicle.distanceTo(body.position)<1.01f*radius;}; // Not "near": a Windows macro.
                if(over(*earth,1.f))detailFocus="earth";else if(over(*moon,static_cast<float>(1737.4/6378.137)))detailFocus="moon";
                else for(const auto& [id,node]:solids.nodes())if(albert::core::surfaceBody(id)&&over(*node,static_cast<float>(albert::core::surfaceRadius(id)/albert::core::earthRadiusMeters)))detailFocus=id;
            }
            solids.update(*camera,sunPosition,tdb,detailFocus,visualSettings.quality,dt,visualSettings.auxiliary,visualSettings.terrain);
            {std::map<std::string,Vector3> nuclei;for(const auto& [id,node]:objects.nodes())if(comets.known(id)&&registry.get(id).visible)nuclei.emplace(id,node->position);
             comets.update(*camera,sunPosition,nuclei,cometStates,tdb,dt);}
            objects.updateLighting(*camera,sunPosition,visualSettings.auxiliary,dt);

            if (appliedQuality != visualSettings.quality) {
                sunlight->shadow->dispose();
                sunlight->shadow->mapSize.set(visualSettings.quality ? 2048.f : 1024.f, visualSettings.quality ? 2048.f : 1024.f);
                appliedQuality = visualSettings.quality;
            }
            graphics.setShadows(visualSettings.shadows && (focus == targets::Earth || focus == targets::Moon));
            static auto lastShadow = steadyNow - std::chrono::seconds(1);
            if (steadyNow - lastShadow >= std::chrono::milliseconds(100)) {
                auto& shadowCamera = static_cast<OrthographicCamera&>(*sunlight->shadow->camera);
                const float extent = focus == targets::Moon ? 0.6f : 1.4f;
                shadowCamera.left = shadowCamera.bottom = -extent;
                shadowCamera.right = shadowCamera.top = extent;
                shadowCamera.nearPlane = 0.1f;
                shadowCamera.farPlane = 300.f;
                shadowCamera.updateProjectionMatrix();
                lastShadow = steadyNow;
            }
            Vector3 solarDirection = sunPosition;solarDirection.sub(earthPosition);
            auto shownSettings=visualSettings;shownSettings.clouds=visualSettings.clouds&&cloudsCurrent;
            albert::TerrainFrame earthFrame; // The globe's place in double precision: its turn from the celestial-to-terrestrial matrix, as earthOrientation() maps it (x, z, -y), its flattening on y.
            if(const auto* at=objects.exact("earth")){earthFrame.at=*at;const double flat=6356.752314245/6378.137;
                auto turned=[&](double a,double b,double c){const double x=c2t[0][0]*a+c2t[1][0]*b+c2t[2][0]*c,y=c2t[0][1]*a+c2t[1][1]*b+c2t[2][1]*c,z=c2t[0][2]*a+c2t[1][2]*b+c2t[2][2]*c;return std::array<double,3>{x,z,-y};};
                const auto x=turned(1,0,0),y=turned(0,0,1),z=turned(0,-1,0);earthFrame.linear={x[0],x[1],x[2],y[0]*flat,y[1]*flat,y[2]*flat,z[0],z[1],z[2]};}
            planets.cullToView(insetId.empty()); // Last frame's second view: its ground must not be cut to this one's.
            planets.update(solarDirection.normalize(), camera->position, shownSettings, dt, camera.get(),objects.exact("earth")?&earthFrame:nullptr,objects.exact("moon"));
            Vector3 earthCamera = camera->position;
            earth->worldToLocal(earthCamera);
            planets.updateImageDetail(camera->position, visualSettings.quality);
            if (visualSettings.quality && shownSettings.clouds && focus == targets::Earth && earthCamera.length()<2.2f) {
                const double latitude = albert::geodeticLatitude(std::asin(std::clamp(earthCamera.y/earthCamera.length(),-1.f,1.f)))*180./PI;
                const double lon = albert::longitude(std::atan2(earthCamera.z,-earthCamera.x)*180./PI-180.);
                cloudSync.requestDetail(albert::cloudRegion(lon,latitude,earthCamera.length(),camera->aspect,camera->fov));
            } else cloudSync.requestDetail(std::nullopt);
            // A vehicle near a body's ground keeps the detail it asked for (it was released 3 s after each load,
            // as if the camera had left the body, and loaded again).
            const bool planetReleased=planets.releaseDistantDetails(detailFocus,camera->position,visualSettings.quality,elapsed);
            const bool solidReleased=solids.takeEviction();
            const bool released=planetReleased||solidReleased;
            if(released)regional.retry();
            memory.update(released);
            // A finer level of the focused body arrived: reload its regional detail once.
            if(const auto generation=albert::visualAssetGeneration();generation!=assetGeneration||targetId(focus)!=detailRefresh.body()){
                assetGeneration=generation;const std::string body=targetId(focus);
                if(detailRefresh.observe(body,albert::visualAssetPack()->availableLevel(body+"-day")))regional.retry();
            }
            if((focus!=lastRegionalFocus&&(solids.contains(focus)||solids.contains(lastRegionalFocus)))||(lastDetailQuality!=static_cast<bool>(visualSettings.quality)&&solids.contains(focus)))regional.retry();
            lastRegionalFocus=focus;lastDetailQuality=visualSettings.quality;
            Vector3 focusSurface; // An external object near the ground: launch-site detail around its ground point.
            const bool nearGround=objects.nodes().contains(focus)&&!albert::core::naturalBody(focus)&&focus!="earth"&&focus!="sun"
                &&(focusSurface=objects.nodes().at(focus)->position,earth->worldToLocal(focusSurface),focusSurface.length()<1.01f)&&earthCamera.length()<1.05f;
            if(!nearGround)siteRegion.reset();
            if(nearGround){
                const double latitude=albert::geodeticLatitude(std::asin(std::clamp(focusSurface.y/focusSurface.length(),-1.f,1.f)))*180./PI;
                const double lon=albert::longitude(std::atan2(focusSurface.z,-focusSurface.x)*180./PI-180.);
                constexpr double cell=.0439453125,half=.0439453125; // ~10 km box on a 0.044° lattice: 30 m relief.
                // The site's box kept while what is in focus stays in its middle half, and once it is 3 km up (a climbing vehicle leaves
                // it for good): moved on each cell crossed, it was loaded again eight times in a climb, the ground blinking at each swap.
                const double up=(focusSurface.length()-(1-std::pow(std::sin(latitude*PI/180),2)/298.257223563))*6378137.;
                const auto inner=[&](const albert::GeoRegion& r){const double w=r.east-r.west,h=r.north-r.south;return lon>r.west+w/4&&lon<r.east-w/4&&latitude>r.south+h/4&&latitude<r.north-h/4;};
                if(!siteRegion||(up<3000&&!inner(*siteRegion))){const double south=std::floor(latitude/cell)*cell-half,west=std::floor(lon/cell)*cell-half;siteRegion=albert::GeoRegion{west,south,west+2*half+cell,south+2*half+cell};}
                regional.request(*siteRegion);
            } else if(visualSettings.quality && focus==targets::Earth && earthCamera.length()<2.2f) {
                const double latitude=albert::geodeticLatitude(std::asin(std::clamp(earthCamera.y/earthCamera.length(),-1.f,1.f)))*180./PI;
                const double lon=albert::longitude(std::atan2(earthCamera.z,-earthCamera.x)*180./PI-180.);
                const double altitude=std::max(.1,(earthCamera.length()-1.)*6378.137); // km; finer cells near the ground, down to ~10 km.
                const int finer=std::clamp(static_cast<int>(std::floor(std::log2(150./altitude))),0,4);
                regional.request(albert::viewRegion(lon,latitude,earthCamera.length(),1.,camera->aspect,camera->fov,1.40625/(1<<finer)));
            } else if(visualSettings.quality && detailFocus=="moon" && camera->position.distanceTo(moonPosition)<.8f) {
                Vector3 local=camera->position;moon->worldToLocal(local);
                const double latitude=std::asin(std::clamp(local.y/local.length(),-1.f,1.f))*180./PI;
                const double lon=albert::longitude(std::atan2(local.z,-local.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,local.length(),1737.4/6378.137,camera->aspect,camera->fov,1.40625),"moon");
            } else if(visualSettings.quality&&solids.contains(detailFocus)&&(!solids.hazy(detailFocus)||solids.surfaceView(detailFocus))&&camera->position.distanceTo(solids.nodes().at(detailFocus)->position)<radiusFor(detailFocus)*2.2f){
                Vector3 local=camera->position;solids.nodes().at(detailFocus)->worldToLocal(local);
                const double latitude=std::asin(std::clamp(local.y/local.length(),-1.f,1.f))*180./PI;
                const double lon=albert::longitude(std::atan2(local.z,-local.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,local.length(),radiusFor(detailFocus),camera->aspect,camera->fov,1.40625),detailFocus);
            } else regional.request(std::nullopt);
            if(auto images=(planets.uploading()||solids.uploading()) ? std::nullopt : regional.poll(); images) {
                if(!images->error.empty()) std::cerr<<"Detalhes regionais: "<<images->error<<std::endl;
                if(images->day || images->night) {
                std::cout<<"Detalhes locais carregados: "<<images->body<<std::endl;
                if(solids.contains(images->body))solids.stage(std::move(*images));else planets.stageRegionalImages(std::move(*images));
                }
            }
            if (auto detail = cloudSync.pollDetail(); detail && detail->observation.image) {
                planets.setCloudDetail(std::move(*detail->observation.image),detail->region);
                std::cout << "Nuvens regionais: " << detail->region.west << ", " << detail->region.south << " a " << detail->region.east << ", " << detail->region.north << std::endl;
            }
            if(steadyNow >= nextCacheCheck) {
                cache.refresh();
                nextCacheCheck=steadyNow+std::chrono::seconds(5);
            }
            if(bordersObject->visible) {
                bordersObject->updateDetail(camera->position.distanceTo(earthPosition));
                auto* material=bordersObject->style.get();
                planets.updateBorderMaterial(*material,camera->position);
                material->uniforms.at("viewport").setValue(Vector2(display.x,display.y));
                material->uniforms.at("nearClip").setValue(camera->nearPlane);
                material->uniforms.at("visibility").setValue(borderVisibility);
            }
            vectorLayers.update(registry,objects,sceneOrigin,Vector2(display.x,display.y),camera->nearPlane,dt,[&](ShaderMaterial& material){planets.updateBorderMaterial(material,camera->position);});
            if(rpcPort||catalog.active()) {
                const double r=albert::core::earthRadiusMeters;
                auto snapshot=registry.snapshot(simulatedUtc,{focus,
                    {sceneOrigin.x+camera->position.x*r,sceneOrigin.y-camera->position.z*r,sceneOrigin.z+camera->position.y*r},
                    camera->position.distanceTo(controls.target)*r,transition.active});
                snapshot.clock=frameTime;snapshot.simulation=moment;snapshot.simulation.utcSeconds=simulatedUtc;snapshot.approximatePositions=approximatePositions;commands.publish(std::move(snapshot));
            }
            eclipses.update(visualSettings.shadows);
            { // The air: Mars's within three of its radii (its model's ground lies under every surface), else
              // the Earth's, whose ground drops to the terrain under the camera where that lies below the
              // ellipsoid (an eye under the model's ground, 10 m over the Cape's, speckled the aerial perspective).
                const auto mars=solids.nodes().find("mars");const bool marsAir=mars!=solids.nodes().end()
                    &&camera->position.distanceTo(mars->second->position)<3.f*static_cast<float>(albert::core::surfaceRadius("mars")/albert::core::earthRadiusMeters);
                solids.showAir("mars",marsAir,planets.atmosphereVisibility());
                // The sea's wind: a live-weather flight's near the ground, else the ocean mean (~7 m/s, a trade wind from the
                // east); Mars has neither.
                if(flightTelemetry&&flightTelemetry->active&&steadyNow-flightTelemetryAt<std::chrono::seconds(10)&&flightTelemetry->height<1000&&flightTelemetry->atmosphere.find("GFS")!=std::string::npos){
                    seaWindEast=static_cast<float>(flightTelemetry->windEast);seaWindNorth=static_cast<float>(flightTelemetry->windNorth);
                    if(std::hypot(seaWindEast,seaWindNorth)<.5f){seaWindEast=-.5f;seaWindNorth=0;}}
                else{seaWindEast=-7.f;seaWindNorth=0;}
                const float wind=std::hypot(seaWindEast,seaWindNorth);
                // The cloud shadows' height over the ellipsoid: the shell's, or the volumetric layer's middle as it takes over.
                graphics.setWeather(marsAir?nullptr:planets.cloudMap(),12.f+(3.5f-12.f)*cloudVolume,planets.cloudOpacity(),marsAir?0.f:wind,earth->scale.y);
                if(marsAir)graphics.setAir(mars->second.get(),6378.137f,planets.atmosphereVisibility(),0.f,true);
                else{Vector3 eye=camera->position;earth->worldToLocal(eye);const auto* terrain=planets.earthTerrain();const auto ground=terrain?terrain->ground(eye):std::nullopt;
                    graphics.setAir(planets.earth.get(),6378.137f,planets.atmosphereVisibility(),ground?std::min(0.f,static_cast<float>((*ground-1)*6378.137)):0.f);}}
            graphics.setEarthshine(earth.get(),earth->scale.x); // The Earth mesh is a unit sphere.
            { // Wind waves on the Earth's water near the eye (Ajustes "Ondas no mar"): the pattern anchored on the sea under the eye, the
              // anchor snapped to its 512 m period (so it never jumps) in a tangent plane redrawn after 200 km; computed in double
              // relative to the eye so the floating origin keeps it exact.
                namespace ph=albert::physics;const double r=albert::core::earthRadiusMeters;const auto& c=camera->position;seaClock+=dt;
                const ph::V3 gcrf{c.x*r+sceneOrigin.x,-c.z*r+sceneOrigin.y,c.y*r+sceneOrigin.z};const ph::Tt tt=ph::ttFromUtc(simulatedUtc);
                const ph::M3 fixed=ph::Rotation::iau(ph::secondsSinceJ2000(tt)-(simulatedUtc-946728000.)).at(tt).fixed;const ph::V3 eye=fixed*gcrf;
                const ph::Geodetic here=ph::geodetic(ph::earth,eye);const bool on=preferences.oceanWaves&&here.height<30e3&&here.height>-100;
                if(on){
                    if(!seaOrigin||ph::norm(eye-seaOrigin->position)>200e3){const double sl=std::sin(here.latitude),cl=std::cos(here.latitude),so=std::sin(here.longitude),co=std::cos(here.longitude);
                        seaOrigin=SeaFrame{ph::fixedPosition(ph::earth,{here.latitude,here.longitude,ph::geoidHeight(here.latitude,here.longitude)}),{-so,co,0},{-sl*co,-sl*so,cl}};} // On the mean sea (the geoid): the shader keeps the waves to what lies at its level.
                    const auto& o=*seaOrigin;const ph::V3 d=eye-o.position;
                    const ph::V3 anchor=o.position+o.east*(512*std::round(ph::dot(d,o.east)/512))+o.north*(512*std::round(ph::dot(d,o.north)/512));
                    auto toWorld=[&](ph::V3 a){const ph::V3 g=ph::transpose(fixed)*a;return Vector3(static_cast<float>(g.x),static_cast<float>(g.z),static_cast<float>(-g.y));}; // ECEF → world axes.
                    Vector3 at=toWorld((anchor-eye)*(1/r));at.add(camera->position);
                    graphics.setSea(true,seaClock,seaWindEast,seaWindNorth,at,toWorld(o.east).normalize(),toWorld(o.north).normalize(),static_cast<float>(r));
                }else graphics.setSea(false,seaClock,seaWindEast,seaWindNorth,{},{},{},static_cast<float>(r));
                // Volumetric clouds (Ajustes "Nuvens volumetricas") up to 50 km, handing over to the flat shell from 30 km; their noise
                // drifts with the wind at their height, taken as 1.5 times the sea's (the 850 hPa wind over the 10 m one).
                // The shell gives way only where the volume was drawn (the air's tables ready, a cloud map), so neither vanishes alone.
                const double fade=std::clamp((here.height-30e3)/20e3,0.,1.);
                cloudShare=albert::animateValue(cloudShare,preferences.volumetricClouds?static_cast<float>(1-fade*fade*(3-2*fade)):0.f,dt);
                const float volume=cloudShare*planets.cloudOpacity()/.88f;cloudVolume=graphics.cloudsShown()?volume:0.f;
                graphics.setClouds(volume>0,volume,seaClock,1.5f*seaWindEast,1.5f*seaWindNorth);planets.setCloudShell(1-cloudVolume);
            }
            // Out of focus (another window over it, a workspace away) the compositor may hold the synced frames back: then unsynced,
            // the frame cap pacing it, so the flights keep moving and their samples keep flowing.
            graphics.setSamples(preferences.antialias?4:1);graphics.setVsync(preferences.vsync&&graphics.focused());ImGui::GetIO().FontGlobalScale=preferences.uiScale;
#ifdef ALBERT_HAS_RPC
            { // Retarded spectra stay separate so distant vents and the stack keep their directions.
                namespace ph=albert::physics;namespace ac=ph::acoustics;
                const auto sources=rpc?rpc->flightSounds():std::vector<albert::rpc::FlightServices::Sound>{};
                struct Candidate {albert::Sound::Voice voice;double level=-1e9;std::array<ac::Boom,2> booms{};unsigned count=0;
                    struct Transient {ac::Heard heard;bool ignition=false;double age=0;};std::array<Transient,64> tails{};unsigned tailCount=0;};
                std::array<Candidate,albert::Sound::voices> selected{};
                if(soundOn&&!sound&&!smokeTest)sound=std::make_unique<albert::Sound>(true);else if(!soundOn&&sound)sound.reset();
                if(sound)sound->setVolume(preferences.volume);
                for(auto it=heardFlights.begin();it!=heardFlights.end();)if(std::none_of(sources.begin(),sources.end(),[&](const auto& x){return x.id==it->first;}))it=heardFlights.erase(it);else ++it;
                if(!sources.empty()){
                    // The camera in the Earth's rotating frame (renderPosition inverted); its velocity there between shown instants.
                    const double r=albert::core::earthRadiusMeters;const auto& c=camera->position;
                    const ph::V3 gcrf{c.x*r+sceneOrigin.x,-c.z*r+sceneOrigin.y,c.y*r+sceneOrigin.z};const ph::Tt tt=ph::ttFromUtc(simulatedUtc);
                    const auto fixed=ph::Rotation::iau(ph::secondsSinceJ2000(tt)-(simulatedUtc-946728000.)).at(tt).fixed;const ph::V3 here=fixed*gcrf;
                    Vector3 right;right.setFromMatrixColumn(*camera->matrixWorld,0);right.normalize();const ph::V3 cameraRight=fixed*ph::V3{right.x,-right.z,right.y};
                    if(lastListener&&simulatedUtc>lastListener->first)listenerVelocity=(here-lastListener->second)*(1/(simulatedUtc-lastListener->first));
                    else if(!lastListener||simulatedUtc<lastListener->first)listenerVelocity={};
                    lastListener={simulatedUtc,here};
                    for(const auto& source:sources){auto& h=heardFlights[source.id];auto& emissions=h.emissions;
                        if(!emissions.empty()&&source.utc<emissions.back().time)h={}; // A new flight, or a replay.
                        if(simulatedUtc<h.playedUtc){for(auto& t:h.tails)t.active=false;h.serial=0;}h.playedUtc=simulatedUtc;
                        if(emissions.empty()||source.utc>=emissions.back().time+.05)emissions.push_back(source.emission);
                        else if(source.utc==emissions.back().time)emissions.back()=source.emission; // Clock-only updates can close a valve without advancing physics.
                        if(emissions.size()>2&&emissions.front().time<source.utc-660)
                            emissions.erase(emissions.begin(),std::lower_bound(emissions.begin(),emissions.end(),source.utc-600,[](const ac::Emission& e,double t){return e.time<t;}));
                        const auto air=source.air;const auto at=[&](const ph::Geodetic& g){return air?air->at(tt,g.latitude,g.longitude,g.height):ph::standardAtmosphere(g.height);};
                        const auto heard=ac::hear(emissions,simulatedUtc,here,listenerVelocity,source.planet,at);
                        ph::V3 position=source.emission.position;
                        if(heard.audible){const auto next=std::upper_bound(emissions.begin(),emissions.end(),heard.emitted,[](double t,const ac::Emission& e){return t<e.time;});
                            if(next!=emissions.begin()){const auto& prev=*(next-1);position=prev.position;if(next!=emissions.end()&&next->time>prev.time)position=position+(next->position-position)*std::clamp((heard.emitted-prev.time)/(next->time-prev.time),0.,1.);}}
                        const auto direction=position-here;const double distance=ph::norm(direction);const float side=distance>0?static_cast<float>(std::clamp(ph::dot(direction,cameraRight)/distance,-1.,1.)):0.f;
                        Candidate candidate;auto& voice=candidate.voice;voice.id=14695981039346656037ull;for(unsigned char c:source.id)voice.id=(voice.id^c)*1099511628211ull;if(!voice.id)voice.id=1;
                        voice.levels=heard.levels;voice.audible=heard.audible;voice.pan=side;voice.crackle=static_cast<float>(.6*std::clamp((heard.rocket-105)/20,0.,1.));
                        if(heard.audible)candidate.level=ac::overall(heard.levels);
                        if(const auto* vehicle=objects.vehicle(source.id)){ // The visual clocks date each bell's main stage and cutoff, including staggered starts.
                            const auto instance=registry.get(source.id).instance;if(instance!=h.instance){h.instance=instance;h.serial=0;}
                            const double shares=vehicle->thrust()*vehicle->engineCount();
                            if(source.emission.noise.thrust>0&&shares>1e-4){h.reference=source.emission.noise;h.reference.thrust/=shares;}
                            const auto end=vehicle->soundSerial(),begin=std::max(h.serial+1,end>255?end-255:1);
                            for(auto serial=begin;serial<=end;++serial){const auto& event=vehicle->engineSounds()[serial%vehicle->engineSounds().size()];if(event.serial!=serial||h.reference.thrust<=0)continue;
                                auto slot=std::find_if(h.tails.begin(),h.tails.end(),[](const auto& x){return !x.active;});if(slot==h.tails.end())break;
                                auto& e=slot->emission;e=source.emission;e.time=event.utc;e.position=e.position+e.velocity*(event.utc-source.utc);
                                e.noise=h.reference;e.noise.exitDiameter=2*event.radius;e.on=true;e.pad={};e.dragPower=e.blast=0;slot->ignition=event.ignition;slot->active=true;}
                            h.serial=end;
                        }
                        for(auto& tail:h.tails)if(tail.active){ // A short source fixed at release; the usual atmosphere, delay and Doppler carry its sound.
                            if(simulatedUtc-tail.emission.time>660){tail.active=false;continue;}
                            const auto path=here-tail.emission.position;const double length=ph::norm(path);double delay=0;
                            for(int k=0;k<8;++k){const auto a=at(ph::geodetic(source.planet,tail.emission.position+path*((k+.5)/8)));delay+=length/(8*(a.sound>0?a.sound:331));}
                            if(simulatedUtc<tail.emission.time+delay)continue; // Waiting pulses need no band spectra or absorption calculation.
                            auto& pulse=h.pulse;pulse[0]=pulse[1]=tail.emission;pulse[1].time+=.001;
                            const auto arrived=ac::hear(pulse,simulatedUtc,here,listenerVelocity,source.planet,at);
                            if(arrived.audible){tail.active=false;const double age=std::max(0.,(arrived.emitted-tail.emission.time)/arrived.doppler);
                                if(age<(tail.ignition?.09:.18)/arrived.doppler&&candidate.tailCount<candidate.tails.size()){
                                    candidate.tails[candidate.tailCount++]={arrived,tail.ignition,age};candidate.level=std::max(candidate.level,ac::overall(arrived.levels));}}
                        }
                        const auto booms=ac::booms(emissions,simulatedUtc,here,source.planet,at); // One more cone over the camera, just now: its boom.
                        if(booms.size()>h.booms&&booms.back().time>simulatedUtc-1)candidate.booms[candidate.count++]=booms.back();
                        h.booms=booms.size();
                        if(!h.blasted)if(const auto e=std::find_if(emissions.begin(),emissions.end(),[](const ac::Emission& x){return x.blast>0;});e!=emissions.end()){ // It blew up: its blast, once arrived.
                            const double r=ph::norm(here-e->position),c=at(ph::geodetic(source.planet,here)).sound;
                            if(simulatedUtc>=e->time+r/(c>0?c:340.)){h.blasted=true;const auto b=ac::blast(e->blast,r,at(ph::geodetic(source.planet,here)).pressure);
                                if(b.overpressure>.01)candidate.booms[candidate.count++]={simulatedUtc,b.overpressure,std::max(4*b.duration,.08)};}}
                        for(unsigned k=0;k<candidate.count;++k)candidate.level=std::max(candidate.level,20*std::log10(std::max(candidate.booms[k].overpressure,1e-20)/20e-6));
                        if(voice.audible||candidate.count||candidate.tailCount)for(unsigned k=0;k<selected.size();++k)if(candidate.level>selected[k].level){
                            for(unsigned j=static_cast<unsigned>(selected.size())-1;j>k;--j)selected[j]=selected[j-1];selected[k]=candidate;break;}
                        if(soundStats&&steadyNow>=soundReport) // ALBERT_SOUND_STATS: once a second.
                            std::printf("sound %s: %s, %.1f dB SPL (%.1f dB above 20 Hz, engines %.1f dB) from %.0f m, %.2f s late, Doppler %.3f, %zu booms\n",source.id.c_str(),
                                heard.audible?"audible":"silent",heard.audible?ac::overall(heard.levels):-1e9,[&]{ac::Spectrum a=heard.levels;if(!heard.audible)a.fill(-1e9);for(int b=0;b<12;++b)a[static_cast<std::size_t>(b)]=-1e9;return ac::overall(a);}(),
                                heard.rocket,heard.distance,heard.delay,heard.doppler,booms.size());
                    }
                }else lastListener.reset();
                std::array<albert::Sound::Voice,albert::Sound::voices> voices{};double total=0;
                for(unsigned k=0;k<selected.size();++k){voices[k]=selected[k].voice;if(voices[k].audible)total+=std::pow(10.,ac::overall(voices[k].levels)/10);}
                if(sound){sound->hear(voices,dt);for(const auto& c:selected){for(unsigned k=0;k<c.count;++k)sound->boom(c.booms[k].overpressure,c.booms[k].duration,c.voice.pan,c.voice.id);
                    for(unsigned k=0;k<c.tailCount;++k){const auto& t=c.tails[k];sound->transient(t.heard,t.ignition,c.voice.pan,c.voice.id,t.age);}}}
                if(soundStats&&steadyNow>=soundReport){soundReport=steadyNow+std::chrono::seconds(1);
                    std::printf("sound: %s, %.1f dB SPL, adaptation %.1f dB\n",sound&&sound->playing()?"device":"muted",total>0?10*std::log10(total):-1e9,sound?sound->adaptation():0.);std::fflush(stdout);}
            }
#endif
            const bool insetFrame=(++insetFrames&1)==0;if(insetFrame)insetTexture=0; // The second view every other frame: half its cost.
            const bool wasInsetEngines=insetEngines;insetEngines=false;insetId.clear();
            if(auto flightId=[](const std::string& id){return id=="flight"||id.rfind("flight.",0)==0;};flightId(focus)){
                if(insetTestId!=focus){insetTestId=focus;insetTestUntil={};}
                const auto focused=objects.nodes().find(focus);const auto& look=registry.get(focus).look;
                if(focused!=objects.nodes().end()&&look&&!look->engines.empty()&&flightTelemetry&&flightTelemetry->active&&flightTelemetry->clamped&&!flightTelemetry->burning
                    &&albert::Vehicle::gimballing(*focused->second,*look)>.5f*static_cast<float>(PI/180))insetTestUntil=steadyNow+std::chrono::seconds(3);
                insetEngines=focused!=objects.nodes().end()&&look&&!look->engines.empty()&&steadyNow<insetTestUntil;
                if(insetEngines)insetId=focus;
                else if(preferences.inset&&!transition.active&&!freeCamera)for(const auto& [id,node]:objects.nodes())if(id!=focus&&flightId(id)&&node->visible){insetId=id;break;}
                if(!insetId.empty()){
                    const auto& io=ImGui::GetIO();const float scale=io.DisplayFramebufferScale.x>0?io.DisplayFramebufferScale.x:1.f;
                    const unsigned w=static_cast<unsigned>(std::max(io.DisplaySize.x*.28f*scale,160.f)),h=w*9/16;
                    if(insetEngines){ // Beside the bells, above the pad and mount, with the vehicle upright in the view.
                        const auto& node=*focused->second;float lowest=1e9f,reach=.1f;Vector3 target;std::size_t i=0;
                        for(const auto& pivot:node.children){if(pivot->children.empty())continue;const auto bell=pivot->children.front()->material();if(!bell||bell->name!="albert:bell")continue;
                            if(i>=look->engines.size())break;const float r=static_cast<float>(look->engines[i++].exitRadius),length=(r>1?2.6f:2.2f)*r;
                            Vector3 exit(0,-length,0);exit.add(pivot->position); // At rest: the camera holds still while the bells swing in its view (aimed at them as swung, it shook).
                            lowest=std::min(lowest,exit.y);reach=std::max(reach,std::hypot(exit.x,exit.z)+r);target.add(exit);}
                        // On a mount's deck (its base metres over its site's ground): from under the bells, just over the flame bucket, looking up
                        // at them through the deck's opening, as the pads' trench cameras do; else beside them, over the base.
                        float clear=0;if(const auto site=objects.nodes().find("site"+focus.substr(6));site!=objects.nodes().end()){Vector3 up(0,0,1),aft(0,-1,0),d=node.position;
                            up.applyQuaternion(site->second->quaternion);aft.applyQuaternion(node.quaternion);d.sub(site->second->position);
                            clear=static_cast<float>(d.dot(up)*albert::core::earthRadiusMeters+look->length/2*aft.dot(up));}
                        const bool under=clear>8;const float half=static_cast<float>(look->length)*.5f;target.multiplyScalar(1.f/static_cast<float>(std::max<std::size_t>(i,1)));
                        const float distance=under?6.f:std::clamp(reach*2.6f,10.f,14.f),floor=under?-half-clear+5.6f:-half+.5f;
                        Vector3 eye(distance*std::cos(.52f),under?floor:std::max(lowest-.35f,floor),distance*std::sin(.52f)),up(0,1,0); // Beside: the base clearance takes precedence over looking up into a low bell.
                        target.multiplyScalar(node.scale.x).applyQuaternion(node.quaternion).add(node.position);eye.multiplyScalar(node.scale.x).applyQuaternion(node.quaternion).add(node.position);up.applyQuaternion(node.quaternion);
                        insetCamera->position.copy(eye);insetCamera->up.copy(up);insetCamera->lookAt(target);
                        const float away=under?std::hypot(distance,lowest-floor):distance;
                        insetCamera->fov=std::min(100.f,2*std::atan2(std::max(reach*1.15f*static_cast<float>(h)/static_cast<float>(w),2.f),away)*static_cast<float>(180/PI));insetCamera->nearPlane=std::clamp(away*node.scale.x*.02f,1e-9f,100.f);
                    }else{
                    // Its own camera, the main one's moves aside: three-quarters off the vehicle's side (its local x turned 30° toward
                    // local z, rolling with it), the horizon level (up: away from the Earth's centre; along the vehicle when looking straight down).
                    const auto& node=*objects.nodes().at(insetId);const Vector3 target=node.position;Vector3 axis(0,1,0);axis.applyQuaternion(node.quaternion);
                    Vector3 side(std::cos(.52f),0,std::sin(.52f));side.applyQuaternion(node.quaternion);
                    const float distance=distanceFor(insetId)*.5f;side.normalize().multiplyScalar(distance);
                    Vector3 up=target;if(const auto earthNode=objects.nodes().find("earth");earthNode!=objects.nodes().end())up.sub(earthNode->second->position);up.normalize();
                    if(std::abs(up.dot(Vector3(side).normalize()))>.98f)up.copy(axis);
                    insetCamera->position.copy(target).add(side);insetCamera->up.copy(up);insetCamera->lookAt(target);
                    insetCamera->fov=camera->fov;insetCamera->nearPlane=std::clamp(distance*.02f,1e-9f,100.f);
                    }
                    insetCamera->aspect=static_cast<float>(w)/static_cast<float>(h);
                    insetCamera->updateProjectionMatrix();insetCamera->updateMatrixWorld();
                    if(insetFrame||insetEngines!=wasInsetEngines)insetTexture=graphics.renderInset(*scene,*insetCamera,w,h);
                }
            }else{insetTestId.clear();insetTestUntil={};}
            graphics.render(*scene, *camera);
            graphics.ui();
            // Display submits the scene and ImGui together; read back only once both have reached the queue.
            const bool streamNow=frameStream&&std::chrono::duration<double>(std::chrono::steady_clock::now()-streamAt).count()>=1/streamFps&&frameStream->wanted();if(streamNow)streamAt=std::chrono::steady_clock::now();
            if(!pendingCaptures.empty()||streamNow)albert::gpu::Context::beforePresent([&,streamNow](albert::gpu::Context& gpu){ // One slot a frame: the captures and the stream together.
                auto paths=std::move(pendingCaptures);pendingCaptures.clear();
                for(const auto& path:paths)try{gpu.capturePng(path.string());std::cout<<"Captura: "<<path.string()<<std::endl;}
                    catch(const std::exception& e){std::cerr<<"Capture failed: "<<e.what()<<std::endl;}
                if(streamNow)try{std::vector<unsigned char> rgba;unsigned w=0,h=0;gpu.readRgba(rgba,w,h);frameStream->put(std::move(rgba),w,h);}
                    catch(const std::exception& e){std::cerr<<"Transmissao interrompida: "<<e.what()<<std::endl;frameStream.reset();}
            });
#ifdef ALBERT_HAS_RPC
            if(rpcPort && !rpc && introductionElapsed>=introductionDuration) {
                rpc=std::make_unique<albert::rpc::Server>(commands,*rpcPort,&catalog);applyPhysics();
                std::cout<<"gRPC: 127.0.0.1:"<<rpc->port()<<std::endl;
            }
#endif
            if (!introductionStart) introductionStart = SteadyClock::now();
            if (smokeTest && ++renderedFrames >= 5) {
                std::cout << "Smoke test OK: efemerides reais e 5 quadros renderizados."
                          << std::endl;
                graphics.close();
            }
        })) {const int cap=graphics.focused()?preferences.frameCap:preferences.frameCap>0?std::min(preferences.frameCap,30):30; // Out of focus (unsynced): 30 at most.
            if(cap>0){ // The frame limit: work and heat at most this often.
            nextFrame=std::max(nextFrame+std::chrono::duration_cast<SteadyClock::duration>(std::chrono::duration<double>(1./cap)),SteadyClock::now()-std::chrono::milliseconds(100));
            std::this_thread::sleep_until(nextFrame);}}

        catalog.close();
        commands.close();
#ifdef ALBERT_HAS_RPC
        rpc.reset();
#endif
        refreshProgress.cancelled = true;
        orbitProgress.cancelled = true;
        if (refreshing) pending.wait();
        if (refreshingOrbits) pendingOrbits.wait();
        if (smokeTest && renderedFrames < 5) return 1;

    } catch (const std::exception& e) {
        std::cerr << "Albert Engine: "
                  << e.what() << '\n';
#ifdef _WIN32
        if (!smokeTest && !GetConsoleWindow()) MessageBoxA(nullptr, e.what(), "Albert Engine", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    return 0;
}
