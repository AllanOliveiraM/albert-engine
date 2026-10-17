
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
#include "layers.hpp"
#include "asset_pack.hpp"
#include "asset_downloads.hpp"
#include "solids.hpp"
#include "core/bridge.hpp"
#include "core/time.hpp"
#include "core/astro_time.hpp"
#include "core/local_system.hpp"
#include "debris.hpp"
#include "providers/horizons.hpp"
#include "catalog/catalog.hpp"
#ifdef ALBERT_HAS_RPC
#include "rpc/server.hpp"
#endif
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
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace threepp;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr double PI = 3.14159265358979323846;

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
    std::vector<State> sun;
    std::vector<State> moon;
    std::map<std::string,std::vector<State>> bodies;
};

struct Orbits {
    bool cached = false;
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

int loadSteps(){static const int steps=11+2*static_cast<int>(availableBodies().size());return steps;}

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

double jdUTC() {
    return 2440587.5 + albert::time::clock().utcSeconds()/86400.;
}

double jdTT(double utc){return albert::astro::julianTT(utc);}
double jdTDB(double utc){return albert::astro::julianTDB(utc);}

Matrix4 earthOrientation(double utc) {
    double tt = jdTT(utc);
    double ut1 = utc + DUT1 / 86400.0;

    double m[3][3];

    eraC2t06a(
        2400000.5, tt - 2400000.5,
        2400000.5, ut1 - 2400000.5,
        XP, YP, m
    );

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

std::vector<State> queryHorizons(int body,const std::string& center,double start,double end,const std::string& step,albert::DownloadProgress* progress=nullptr,bool bootstrap=false,bool* cached=nullptr){
    const auto table=albert::providers::horizons().query({body,center,(start-2440587.5)*86400.,(end-2440587.5)*86400.,step,bootstrap,!bootstrap},progress);
    if(cached)*cached|=table.fromCache;
    std::vector<State> result;result.reserve(table.points.size());
    for(const auto& point:table.points)result.push_back({point.jdTDB,{point.positionMeters.x/1000.,point.positionMeters.y/1000.,point.positionMeters.z/1000.},{point.velocityMetersPerSecond.x/1000.,point.velocityMetersPerSecond.y/1000.,point.velocityMetersPerSecond.z/1000.}});
    return result;
}

Ephemerides loadEphemerides(albert::DownloadProgress* progress = nullptr, int offset = 0, bool bootstrap = false) {
    double now = jdUTC();

    Ephemerides e;

    if (progress) progress->stage("Efemerides do Sol", 2 + offset);
    e.sun = queryHorizons(
        10, "500@399",
        now - 1.0/1440.0,
        now + 1.0,
        "5 min", progress, bootstrap, &e.cached
    );

    if (progress) progress->stage("Efemerides da Lua", 3 + offset);
    e.moon = queryHorizons(
        301, "500@399",
        now - 1.0/1440.0,
        now + 1.0,
        "5 min", progress, bootstrap, &e.cached
    );

    int bodyIndex=0;
    for(const auto* body:availableBodies()){
        if(progress)progress->stage("Efemerides: "+std::string(body->name),4+offset+bodyIndex++);
        e.bodies[std::string(body->id)]=queryHorizons(body->horizonsId,"500@399",now-1./1440.,now+1.,"5 min",progress,bootstrap,&e.cached);
    }
    return e;
}

Orbits loadOrbits(albert::DownloadProgress* progress = nullptr, int offset = 0, bool bootstrap = false) {
    double now = jdUTC();

    Orbits orbits;

    // Trajetoria geocentrica da Lua (~35 dias).
    if (progress) progress->stage("Orbita da Lua", 4 + offset);
    orbits.moon = queryHorizons(
        301, "500@399",
        now - 14.0,
        now + 14.0,
        "3 h", progress, bootstrap, &orbits.cached
    );

    // Trajetoria heliocentrica da Terra (~370 dias).
    if (progress) progress->stage("Orbita da Terra", 5 + offset);
    orbits.earth = queryHorizons(
        399, "500@10",
        now - 185.0,
        now + 185.0,
        "1 d", progress, bootstrap, &orbits.cached
    );

    int bodyIndex=0;
    for(const auto* body:availableBodies()){
        if(progress)progress->stage("Orbita: "+std::string(body->name),6+offset+bodyIndex++);
        orbits.bodies[std::string(body->id)]=queryHorizons(body->horizonsId,"500@10",now-body->orbitHalfDays,now+body->orbitHalfDays,std::to_string(static_cast<int>(body->orbitStepHours))+" h",progress,bootstrap,&orbits.cached);
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
    std::uint64_t timeGeneration=0;
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
        pending_ = std::async(std::launch::async, [this, reloadAssets, bootstrap] {
            LoadedData result;
            if (reloadAssets) {
                if (assets_) waitForEssential(*assets_, progress);
                result.assets = albert::loadVisualAssets(countries_.parent_path(), progress);
                progress.stage("Preparando cartografia local", 6);
                result.borders = loadBorders(countries_);
            }
            result.timeGeneration=albert::time::clock().status().generation;
            result.ephemerides = loadEphemerides(&progress, 5, bootstrap);
            result.orbits = loadOrbits(&progress, 5+static_cast<int>(availableBodies().size()), bootstrap);
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
    path->frustumCulled=false;
    path->renderOrder=1;
    return path;
}

void updateOrbitTime(const std::shared_ptr<Mesh>& line, const std::vector<State>& samples, double now) {
    auto* material = line->material()->as<ShaderMaterial>();
    material->uniforms.at("present").setValue(static_cast<float>(now - (samples.front().jd+samples.back().jd)*.5));
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

        fs::path countries =
            albert::assetDirectory() / "countries.geojson";

        Canvas::Parameters window;
        window.title("Albert Engine").antialiasing(4).size(WindowSize(1280,800)).favicon(albert::assetDirectory()/"branding/albert.png");
        Canvas canvas{window};
        canvas.exitOnKeyEscape(false);
        GLRenderer renderer{canvas};
        albert::setWindowIcon(canvas,albert::assetDirectory()/"branding/albert.png");
        renderer.toneMapping = ToneMapping::ACESFilmic;
        renderer.toneMappingExposure = 1.05f;
        const float displayPixelRatio = std::min(2.f, renderer.getTargetPixelRatio());
        renderer.setPixelRatio(displayPixelRatio);
        renderer.shadowMap().enabled = true;
        renderer.shadowMap().type = ShadowMap::PFC;
        renderer.shadowMap().autoUpdate = false;
        // Light distributions carry only the catalogue: download what is missing,
        // essential first. Complete libraries never start this service.
        std::unique_ptr<albert::AssetDownloads> assetDownloads;
        if(const auto catalog=albert::assetDirectory()/"catalog.json";fs::is_regular_file(catalog)){
            bool complete=false;try{complete=albert::visualAssetsComplete();}catch(const std::exception&){}
            if(!complete){assetDownloads=std::make_unique<albert::AssetDownloads>(albert::readAssetCatalog(catalog),albert::AssetDownloads::Options{});assetDownloads->start();}
        }
        DataLoader downloads(countries,assetDownloads.get());
        std::function<void()> drawInterface;
        std::ifstream fontFile(albert::assetDirectory() / "fonts" / "Inter.ttf", std::ios::binary);
        std::vector<char> fontBytes(std::istreambuf_iterator<char>(fontFile), {});
        ImguiFunctionalContext ui(canvas, [&] {
            applyInterfaceTheme();
            if (drawInterface) drawInterface();
        });
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
            if (ImGui::Button("Sair", ImVec2(-1, 32))) canvas.close();
            ImGui::End();
            ImGui::PopStyleVar();
        };
        drawInterface = [&] { drawLoading(1.f); };
        canvas.onWindowResize([&](WindowSize size) { renderer.setSize(size); });
        bool initialDownloadsStarted=false;
        const double initialClockWait=albert::time::monotonicSeconds();
        std::optional<LoadedData> loaded;
        while (canvas.isOpen() && !loaded) {
            if(!initialDownloadsStarted && (!timeService.status().synchronizing || albert::time::monotonicSeconds()-initialClockWait>12.)) {
                downloads.start(true,true);initialDownloadsStarted=true;
                const auto timing=timeService.status();
                std::cout<<"Tempo: "<<albert::time::qualityName(timing.quality)<<" ("<<timing.source<<"), incerteza "<<timing.uncertaintySeconds<<" s"<<std::endl;
            }
            loaded = downloads.poll();
            if (smokeTest && !downloads.error.empty()) throw std::runtime_error(downloads.error);
            if (loaded) break;
            if (!canvas.animateOnce([&] {
                renderer.clear();
                ui.render();
            })) break;
        }
        if (!loaded || !canvas.isOpen()) return smokeTest ? 1 : 0;
        auto loadedAssets = std::move(loaded->assets);
        auto borders = std::move(loaded->borders);
        auto ephemerides = std::move(loaded->ephemerides);
        auto orbits = std::move(loaded->orbits);
        const auto initialTimeGeneration=loaded->timeGeneration;
        loaded.reset();

        auto scene = Scene::create();
        scene->background = Color(0x010208);

        auto camera = PerspectiveCamera::create(
            60, canvas.aspect(), 0.01f, 100000.f
        );

        camera->position.set(0, 0, 3.5f);

        OrbitControls controls{*camera, canvas};

        controls.enableDamping = true;
        controls.dampingFactor = CAMERA_EASING;
        controls.rotateSpeed = 0.6f;
        controls.zoomSpeed = 0.8f;
        controls.enablePan = false;
        controls.minDistance = 1.01f;
        controls.maxDistance = 65000.f;

        auto stars = albert::makeStarField(albert::assetDirectory() / "stars.csv", jdTDB(jdUTC()));
        scene->add(stars);
        auto solidAssets=std::move(loadedAssets->surfaces);
        albert::PlanetVisuals planets(*scene, std::move(*loadedAssets));
        albert::SolidBodies solids(*scene,std::move(solidAssets));
        auto earth = planets.earth;
        auto moon = planets.moon;
        albert::EclipseVisuals eclipses(earth,moon,planets.sun);
        auto sun = planets.sun;
        albert::VisualSettings visualSettings;
        albert::core::Scene registry;
        albert::core::Bridge commands;
        albert::catalog::Manager catalog(commands,albert::providers::horizons());
#ifdef ALBERT_HAS_RPC
        std::unique_ptr<albert::rpc::Server> rpc;
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
            object.appearance.sizeMeters={200000,200000,200000};object.samples.push_back({(jdUTC()-2440587.5)*86400.,{7500000,0,0}});registry.create(std::move(object));}

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
            const double now=jdTDB(jdUTC()),period=body.period/86400.;
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
        auto targetFor = [&](const Focus& f) {return albert::renderPosition(registry.evaluate(targetId(f),(jdUTC()-2440587.5)*86400.).state.positionMeters,sceneOrigin);};
        auto distanceFor = [&](const Focus& f) -> float {
            if(f==targets::EarthMoon)return 180.f;
            if(f==targets::Inner)return static_cast<float>(4.2*149597870700./albert::core::earthRadiusMeters/std::sin(std::atan(std::tan(camera->fov*PI/360.)*std::min(1.f,camera->aspect))));
            if(f==targets::Overview){
                double extent=0;const double now=timeService.utcSeconds();
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

        auto setFocus = [&](Focus next, bool resetZoom = false) {
            registry.get(targetId(next));
            if (focus == next && !transition.active && !resetZoom) return;
            if(assetDownloads){const auto id=targetId(next);assetDownloads->prioritize(id!=targets::Moon&&albert::core::category(id)==albert::core::Category::Moon?std::string("moons"):id);}

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
            transition.arrivalDirection.copy(camera->position).sub(targetFor(next));
            if(transition.arrivalDirection.lengthSq()<1e-12f)transition.arrivalDirection.copy(transition.startOffset);
            if(next==targets::Overview||next==targets::Inner||next==targets::EarthMoon)transition.arrivalDirection.set(.2f,.9f,.7f);
            transition.arrivalDirection.normalize();
            const float scaleChange = std::abs(std::log(distanceFor(next) / std::max(1e-9f, transition.startOffset.length())));
            const float travel=transition.startTarget.distanceTo(targetFor(next));
            transition.duration = albert::flightDuration(scaleChange,
                travel/std::max(.01f,radiusFor(next)),resetZoom);
            if(!resetZoom)transition.duration*=1.15f;

            focus = next;registry.cameraChanged();
        };

        bool refreshing = false;
        bool refreshingOrbits = false;
        bool menuOpen = false;
        float menuProgress = 0.f;
        float resetZoomProgress = 0.f;
        float detailLoaderAge=0.f,detailLoaderAlpha=0.f;
        int menuPage = 0;

        std::array<char,96> focusSearch{};bool uiPopupWasOpen=false;
        float zoomTarget = 3.5f, zoomDisplayed = 3.5f;
        // Start with the day/terminator visible, independent of UTC season.
        Vector3 initialSun = graphical(interpolate(ephemerides.sun, jdTDB(jdUTC())));
        initialSun.normalize();
        Vector3 tangent(-initialSun.z, 0, initialSun.x);
        tangent.normalize();
        camera->position.copy(initialSun).multiplyScalar(1.8f).add(tangent.multiplyScalar(3.f));
        camera->lookAt(controls.target);
        int appliedQuality = -1;
        Focus lastRegionalFocus=focus;bool lastDetailQuality=visualSettings.quality;
        std::uint64_t assetGeneration=albert::visualAssetGeneration();std::string levelBody;int levelSeen=-1;
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
                const double searchTime=timeService.utcSeconds();
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
                if(focus.front()!='@' && !registry.get(focus).builtin) {
                    const auto state=registry.evaluate(focus,(jdUTC()-2440587.5)*86400.);
                    ImGui::TextDisabled(state.stale ? "Dados interrompidos: ultima posicao mantida" : registry.get(focus).providerManaged ? "Posicoes reais; visual simplificado" : "Dados fornecidos pelo integrador");
                }
                ImGui::TextDisabled("Arraste para girar. Role para aproximar.");
                ImGui::Spacing();
                const char* pages[] = {"Cena", "Dados", "Sobre"};
                const float tabWidth = (ImGui::GetContentRegionAvail().x - 16) / 3;
                for (int i = 0; i < 3; ++i) {
                    if (i) ImGui::SameLine();
                    const bool selectedPage = menuPage == i;
                    if (selectedPage) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.09f,.29f,.36f,1));
                    if (ImGui::Button(pages[i], ImVec2(tabWidth, 34))) menuPage = i;
                    if (selectedPage) ImGui::PopStyleColor();
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (menuPage == 0) {
                    ImGui::TextWrapped("Personalize sua vista");
                    ImGui::Spacing();
                    if (earthControls) {
                        if(focus!=targets::Earth)ImGui::TextDisabled("TERRA");
                        ImGui::Checkbox("Atmosfera", &visualSettings.atmosphere);
                        ImGui::Checkbox("Nuvens em tempo real", &visualSettings.clouds);
                        ImGui::TextDisabled("Observacoes de satelite, com atraso.");
                        ImGui::Checkbox("Luzes das cidades", &visualSettings.nightLights);
                        ImGui::Checkbox("Fronteiras dos paises", &visualSettings.borders);
                    }
                    if (focus == targets::Earth || focus == targets::Moon) ImGui::Checkbox("Sombras", &visualSettings.shadows);
                    ImGui::Checkbox("Caminhos das orbitas", &showOrbits);
                    ImGui::Checkbox("Cinturoes e nuvem de Oort", &showDebris);
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
                    ImGui::Spacing();
                    ImGui::TextDisabled("QUALIDADE DA IMAGEM");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::Combo("##Qualidade", &visualSettings.quality, "Mais leve\0Mais detalhes\0");
                    ImGui::TextWrapped("Detalhes carregados automaticamente ao aproximar.");
                    ImGui::TextDisabled("Mais leve reduz uso de memoria.");
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
                    display.y - margin + 18.f * (1.f - reveal)),
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
            if (display.x > 900) {
                ImGui::SetNextWindowPos(ImVec2(display.x-margin,margin),ImGuiCond_Always,ImVec2(1,0));
                ImGui::SetNextWindowSize(ImVec2(210,80),ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(.65f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,14));
                ImGui::Begin("Estado", nullptr, (flags & ~ImGuiWindowFlags_AlwaysAutoResize) | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                protectPanel();
                const auto timing=timeService.status();
                const auto time = albert::utcTime(static_cast<std::time_t>(timing.utcSeconds));
                ImGui::TextDisabled("%02d:%02d:%02d UTC", time.tm_hour, time.tm_min, time.tm_sec);
                if(timing.quality==albert::time::Quality::Local)ImGui::TextColored(ImVec4(1,.7f,.4f,1),"Horario local");
                else if(timing.quality!=albert::time::Quality::Synchronized)ImGui::TextColored(ImVec4(1,.7f,.4f,1),"Horario estimado");
                else if (refreshing || refreshingOrbits || cloudSync.active || catalog.busy()) ImGui::TextColored(ImVec4(.4f,.85f,.9f,1), "Atualizando dados");
                else if (!cloudSync.error.empty() || !refreshError.empty()) ImGui::TextColored(ImVec4(1,.7f,.4f,1), "Atualizacao pendente");
                else ImGui::TextColored(ImVec4(.4f,.85f,.7f,1), "Dados sincronizados");
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // Hover and click use the same nearest visible object.
            if(!ImGui::GetIO().WantCaptureMouse && !transition.active&&!covered(ImGui::GetMousePos(),ImGui::GetMousePos())){
                const auto mouse=ImGui::GetMousePos();Raycaster ray;ray.setFromCamera(Vector2(mouse.x/display.x*2.f-1.f,1.f-mouse.y/display.y*2.f),*camera);
                ray.nearPlane=camera->nearPlane;ray.farPlane=camera->farPlane;
                if(const auto picked=objects.pick(registry,ray)){
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

        canvas.onWindowResize([&](WindowSize size) {
            camera->aspect = size.aspect();
            camera->updateProjectionMatrix();
            renderer.setSize(size);
        });

        using SteadyClock = std::chrono::steady_clock;

        auto nextRefresh = SteadyClock::now() + std::chrono::seconds(ephemerides.cached ? 0 : REFRESH_SECONDS);
        auto nextOrbitRefresh = SteadyClock::now() + std::chrono::hours(orbits.cached ? 0 : 24);
        int ephemerisFailures = 0, orbitFailures = 0;

        albert::DownloadProgress refreshProgress;
        albert::DownloadProgress orbitProgress;
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
        std::uint64_t refreshEpoch=0,ephemerisRequestEpoch=0,orbitRequestEpoch=0;
        double lastFrameMonotonic=albert::time::monotonicSeconds();
        canvas.animate([&] {
            const auto frameTime=timeService.status();
            const float elapsed = clock.getDelta();
            const float dt = std::min(elapsed, 0.1f);
            auto steadyNow = SteadyClock::now();
            const double frameMonotonic=albert::time::monotonicSeconds();
            if(frameTime.generation!=timeGeneration||frameMonotonic-lastFrameMonotonic>30.){
                timeGeneration=frameTime.generation;++refreshEpoch;nextRefresh=steadyNow;nextOrbitRefresh=steadyNow;
            }
            lastFrameMonotonic=frameMonotonic;

            planets.updateUploads(renderer);
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
                nextRefresh = updated->timeGeneration==frameTime.generation ? steadyNow+std::chrono::seconds(REFRESH_SECONDS):steadyNow;
                nextOrbitRefresh = updated->timeGeneration==frameTime.generation ? steadyNow+std::chrono::hours(24):steadyNow;
                ephemerisFailures = orbitFailures = 0;
            }

            if (auto observed = cloudSync.poll(visualSettings.clouds); observed && observed->image) {
                planets.setCloudImage(std::move(*observed->image));
                std::cout << "Nuvens observadas: " << observed->date << " cobertura " << observed->coverage << std::endl;
            }

            // Sincronizacao das efemerides
            if (refreshing &&
                pending.wait_for(std::chrono::seconds(0)) ==
                    std::future_status::ready) {
                try {
                    ephemerides = pending.get();
                    ephemerisFailures = 0;
                    nextRefresh = ephemerisRequestEpoch==refreshEpoch ? steadyNow+std::chrono::seconds(REFRESH_SECONDS):steadyNow;
                    dataValid = true;
                    refreshError.clear();
                } catch (const std::exception& e) {
                    nextRefresh = steadyNow + std::chrono::seconds(std::min(REFRESH_SECONDS, 15 * (1 << std::min(ephemerisFailures++, 5))));
                    refreshError = e.what();
                    std::cerr << e.what() << '\n';
                }
                refreshing = false;
            }

            if (!refreshing && !downloads.active && steadyNow >= nextRefresh) {
                refreshing = true;ephemerisRequestEpoch=refreshEpoch;

                pending = std::async(
                    std::launch::async,
                    [&] { return loadEphemerides(&refreshProgress); }
                );
            }

            // Renovacao diaria dos trajetos orbitais
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

                    scene->add(moonOrbit);
                    scene->add(earthOrbit);
                } catch (const std::exception& e) {
                    nextOrbitRefresh = steadyNow + std::chrono::seconds(std::min(300, 30 * (1 << std::min(orbitFailures++, 4))));
                    refreshError = e.what();
                    std::cerr << e.what() << '\n';
                }
                refreshingOrbits = false;
            }

            if (!refreshingOrbits && !downloads.active && steadyNow >= nextOrbitRefresh) {
                refreshingOrbits = true;orbitRequestEpoch=refreshEpoch;

                pendingOrbits = std::async(
                    std::launch::async,
                    [&] { return loadOrbits(&orbitProgress); }
                );
            }

            commands.drain([&](albert::core::Command& command) {
                std::visit([&](auto& value) {
                    using T=std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<T,albert::core::Create>) registry.create(std::move(value.object));
                    else if constexpr(std::is_same_v<T,albert::core::Remove>) {if(registry.get(value.id).builtin)throw std::invalid_argument("Built-in objects cannot be removed");if(focus==value.id)setFocus(targets::Earth);registry.remove(value.id);visualSettings.auxiliary.erase(value.id);}
                    else if constexpr(std::is_same_v<T,albert::core::Visibility>) registry.visible(value.id,value.enabled);
                    else if constexpr(std::is_same_v<T,albert::core::Append>) registry.samples(value.id,std::move(value.values));
                    else if constexpr(std::is_same_v<T,albert::core::Replace>)registry.replace(value.id,std::move(value.values));
                    else if constexpr(std::is_same_v<T,albert::core::InstallModel>)registry.installModel(std::move(value.model));
                    else if constexpr(std::is_same_v<T,albert::core::RemoveModel>)registry.removeModel(value.id);
                    else if constexpr(std::is_same_v<T,albert::core::CreateLayer>)registry.createLayer(std::move(value.layer));
                    else if constexpr(std::is_same_v<T,albert::core::RemoveLayer>)registry.removeLayer(value.id);
                    else if constexpr(std::is_same_v<T,albert::core::LayerVisibility>)registry.layerVisible(value.id,value.enabled);
                    else if constexpr(std::is_same_v<T,albert::core::Focus>) setFocus(value.id,value.reset);
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
            double utc = 2440587.5+frameTime.utcSeconds/86400.;
            double tdb = jdTDB(utc);

            registry.builtinState("earth",{frameTime.utcSeconds,{0,0,0}});
            try {
                Vec3 sunState = interpolate(ephemerides.sun, tdb);
                Vec3 moonState = interpolate(ephemerides.moon, tdb);
                Vec3 earthHelio = interpolate(orbits.earth, tdb);

                const double timestamp=frameTime.utcSeconds;
                registry.builtinState("moon",{timestamp,{moonState.x*1000.,moonState.y*1000.,moonState.z*1000.}});
                registry.builtinState("sun",{timestamp,{sunState.x*1000.,sunState.y*1000.,sunState.z*1000.}});
                for(const auto& [id,samples]:ephemerides.bodies){const auto value=interpolate(samples,tdb);registry.builtinState(id,{timestamp,{value.x*1000.,value.y*1000.,value.z*1000.}});}
                // Orbitas heliocentricas convertidas
                // para a origem geocentrica da cena.
                earthOrbit->origin=earthHelio;
                updateOrbitTime(moonOrbit, orbits.moon, tdb);
                updateOrbitTime(earthOrbit, orbits.earth, tdb);
                for(const auto& [id,path]:bodyOrbits){path->origin=earthHelio;updateOrbitTime(path,orbits.bodies.at(id),tdb);}

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
            albert::core::updateLocalSystem(registry,frameTime.utcSeconds,tdb);
            const auto nextOriginId=transition.active?Focus("@flight"):targetId(focus);
            // During flight the floating origin follows the eye, preserving the
            // departure body's detail instead of rebasing immediately to a planet AU away.
            const auto nextOrigin=transition.active?sceneOrigin+albert::core::Vector{
                camera->position.x*albert::core::earthRadiusMeters,-camera->position.z*albert::core::earthRadiusMeters,camera->position.y*albert::core::earthRadiusMeters}:
                registry.evaluate(nextOriginId,frameTime.utcSeconds).state.positionMeters;
            const auto shift=albert::renderPosition(sceneOrigin,nextOrigin);
            if(transition.active||originId!=nextOriginId){camera->position.add(shift);controls.target.add(shift);originId=nextOriginId;}
            if(transition.active)transition.startTarget.add(shift);
            sceneOrigin=nextOrigin;
            objects.sync(registry,frameTime.utcSeconds,sceneOrigin);
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

            auto rotation = earthOrientation(utc);
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
                const float destinationDistance=distanceFor(transition.destination);
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
                    automaticOrbit=focus.front()!='@';automaticOrbitAge=0.f;
                    zoomTarget = zoomDisplayed = distanceFor(focus);
                    controls.minDistance = minimumFor(focus);
                }
            } else {
                // Acompanha objetos em movimento.
                Vector3 movement = target;
                movement.sub(controls.target);

                camera->position.add(movement);
                controls.target.copy(target);
            }

            // Damping de rotacao e zoom do mouse
            controls.enabled =
                introductionElapsed >= introductionDuration &&
                !transition.active &&
                !ImGui::GetIO().WantCaptureMouse;

            const float interactionRadius=radiusFor(focus);
            const float relativeAltitude=interactionRadius>0 ? std::max(0.f,camera->position.distanceTo(controls.target)-interactionRadius)/interactionRadius : 1.f;
            controls.rotateSpeed=.75f*std::clamp(relativeAltitude,.025f,1.f);
            controls.zoomSpeed=std::clamp(relativeAltitude,.18f,1.f);
            automaticOrbitAge=automaticOrbit ? automaticOrbitAge+dt : 0.f;
            controls.autoRotate=automaticOrbit&&controls.enabled;
            // OrbitControls uses frame-based increments: compensate for actual dt.
            controls.autoRotateSpeed=.12f*dt*60.f*albert::flightEase(automaticOrbitAge);
            controls.dampingFactor = 1.f - std::exp(-8.f * dt);
            controls.update();
            if (transition.active) {
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

            stars->position.copy(camera->position);
            const float distance = camera->position.distanceTo(controls.target);
            controls.maxDistance=std::max(65000.f,distanceFor(targets::Overview)*2.f);
            controls.maxDistance=std::max(controls.maxDistance,static_cast<float>(150000.*149597870700./albert::core::earthRadiusMeters));
            camera->farPlane=std::max({100000.f,distance*4.f,distanceFor(targets::Overview)*2.f});
            debris.update(sunPosition,camera->position,showDebris,elapsed);
            const float bodyRadius = radiusFor(focus);
            const float clearance = std::max(1e-8f, distance - bodyRadius);
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
            for(const auto& body:albert::core::localOrbits()){
                auto& orbit=localPaths.at(std::string(body.id));
                const float secondary=systemView(focus)&&body.category!=albert::core::Category::Moon ? secondaryVisibility[std::string(body.id)] : 1.f;
                const auto* focused=albert::core::localOrbit(focus);
                const bool family=body.parent==focus||(focused&&focused->category==albert::core::Category::Moon&&body.parent==focused->parent);
                orbit.path->visible=orbitVisibility*secondary>.001f&&(body.id==focus||family||((systemView(focus)||focus==targets::Sun)&&body.category!=albert::core::Category::Moon));
                if(!orbit.path->visible)continue;
                const auto parent=registry.evaluate(std::string(body.parent),frameTime.utcSeconds).state.positionMeters;
                orbit.path->origin={-parent.x/1000.,-parent.y/1000.,-parent.z/1000.};
                orbit.path->material()->as<ShaderMaterial>()->uniforms.at("present").setValue(static_cast<float>(std::remainder(tdb-orbit.epoch,orbit.period)));
                orbit.path->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
                const auto state=albert::core::localState(body,tdb);const double speed=std::hypot(state.velocityMetersPerSecond.x,state.velocityMetersPerSecond.y,state.velocityMetersPerSecond.z);
                orbitView(orbit.path,objects.nodes().at(std::string(body.id))->position,static_cast<float>(albert::core::earthRadiusMeters/(speed*86400.)),secondary);
            }
            // Photographic exposure adapts to the irradiance at the focused planet;
            // physical solar illumination and geometry remain unchanged.
            const float photographicExposure=focus==targets::Moon?2.1f:solids.contains(focus)?std::clamp(1.05f*std::pow(sunPosition.distanceTo(solids.nodes().at(focus)->position)*6378137.f/149597870700.f,2.f),.25f,1200.f):1.05f;
            renderer.toneMappingExposure=std::exp(std::lerp(std::log(renderer.toneMappingExposure),std::log(photographicExposure),-std::expm1(-dt/.65f)));
            solids.update(renderer,*camera,sunPosition,tdb,focus,visualSettings.quality,dt,visualSettings.auxiliary);
            objects.updateLighting(*camera,sunPosition,visualSettings.auxiliary,dt);

            if (appliedQuality != visualSettings.quality) {
                renderer.setPixelRatio(std::min(visualSettings.quality ? 2.f : 1.f, displayPixelRatio));
                sunlight->shadow->dispose();
                sunlight->shadow->mapSize.set(visualSettings.quality ? 2048.f : 1024.f, visualSettings.quality ? 2048.f : 1024.f);
                renderer.shadowMap().needsUpdate = true;
                appliedQuality = visualSettings.quality;
            }
            renderer.shadowMap().enabled = visualSettings.shadows && (focus == targets::Earth || focus == targets::Moon);
            static auto lastShadow = steadyNow - std::chrono::seconds(1);
            if (steadyNow - lastShadow >= std::chrono::milliseconds(100)) {
                auto& shadowCamera = static_cast<OrthographicCamera&>(*sunlight->shadow->camera);
                const float extent = focus == targets::Moon ? 0.6f : 1.4f;
                shadowCamera.left = shadowCamera.bottom = -extent;
                shadowCamera.right = shadowCamera.top = extent;
                shadowCamera.nearPlane = 0.1f;
                shadowCamera.farPlane = 300.f;
                shadowCamera.updateProjectionMatrix();
                renderer.shadowMap().needsUpdate = true;
                lastShadow = steadyNow;
            }
            Vector3 solarDirection = sunPosition;solarDirection.sub(earthPosition);
            planets.update(solarDirection.normalize(), camera->position, visualSettings, dt);
            Vector3 earthCamera = camera->position;
            earth->worldToLocal(earthCamera);
            planets.updateImageDetail(camera->position, visualSettings.quality);
            if (visualSettings.quality && visualSettings.clouds && focus == targets::Earth && earthCamera.length()<2.2f) {
                const double latitude = albert::geodeticLatitude(std::asin(std::clamp(earthCamera.y/earthCamera.length(),-1.f,1.f)))*180./PI;
                const double lon = albert::longitude(std::atan2(earthCamera.z,-earthCamera.x)*180./PI-180.);
                cloudSync.requestDetail(albert::cloudRegion(lon,latitude,earthCamera.length(),camera->aspect,camera->fov));
            } else cloudSync.requestDetail(std::nullopt);
            const bool planetReleased=planets.releaseDistantDetails(focus,camera->position,visualSettings.quality,elapsed);
            const bool solidReleased=solids.takeEviction();
            const bool released=planetReleased||solidReleased;
            if(released)regional.retry();
            memory.update(released);
            // A finer level of the focused body arrived: reload its regional detail once.
            if(const auto generation=albert::visualAssetGeneration();generation!=assetGeneration){
                assetGeneration=generation;const std::string body=targetId(focus);const int level=albert::visualAssetPack()->availableLevel(body+"-day");
                if(body==levelBody&&level>levelSeen)regional.retry();
                levelBody=body;levelSeen=level;
            }
            if((focus!=lastRegionalFocus&&(solids.contains(focus)||solids.contains(lastRegionalFocus)))||(lastDetailQuality!=static_cast<bool>(visualSettings.quality)&&solids.contains(focus)))regional.retry();
            lastRegionalFocus=focus;lastDetailQuality=visualSettings.quality;
            if(visualSettings.quality && focus==targets::Earth && earthCamera.length()<2.2f) {
                const double latitude=albert::geodeticLatitude(std::asin(std::clamp(earthCamera.y/earthCamera.length(),-1.f,1.f)))*180./PI;
                const double lon=albert::longitude(std::atan2(earthCamera.z,-earthCamera.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,earthCamera.length(),1.,camera->aspect,camera->fov,1.40625));
            } else if(visualSettings.quality && focus==targets::Moon && camera->position.distanceTo(moonPosition)<.8f) {
                Vector3 local=camera->position;moon->worldToLocal(local);
                const double latitude=std::asin(std::clamp(local.y/local.length(),-1.f,1.f))*180./PI;
                const double lon=albert::longitude(std::atan2(local.z,-local.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,local.length(),1737.4/6378.137,camera->aspect,camera->fov,1.40625),"moon");
            } else if(visualSettings.quality&&solids.contains(focus)&&camera->position.distanceTo(solids.nodes().at(focus)->position)<radiusFor(focus)*2.2f){
                Vector3 local=camera->position;solids.nodes().at(focus)->worldToLocal(local);
                const double latitude=std::asin(std::clamp(local.y/local.length(),-1.f,1.f))*180./PI;
                const double lon=albert::longitude(std::atan2(local.z,-local.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,local.length(),radiusFor(focus),camera->aspect,camera->fov,1.40625),focus);
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
                auto snapshot=registry.snapshot(frameTime.utcSeconds,{focus,
                    {sceneOrigin.x+camera->position.x*r,sceneOrigin.y-camera->position.z*r,sceneOrigin.z+camera->position.y*r},
                    camera->position.distanceTo(controls.target)*r,transition.active});
                snapshot.clock=frameTime;commands.publish(std::move(snapshot));
            }
            eclipses.update(visualSettings.shadows);
            renderer.render(*scene, *camera);
            ui.render();
#ifdef ALBERT_HAS_RPC
            if(rpcPort && !rpc && introductionElapsed>=introductionDuration) {
                rpc=std::make_unique<albert::rpc::Server>(commands,*rpcPort,&catalog);
                std::cout<<"gRPC: 127.0.0.1:"<<rpc->port()<<std::endl;
            }
#endif
            if (!introductionStart) introductionStart = SteadyClock::now();
            if (smokeTest && ++renderedFrames >= 5) {
                std::cout << "Smoke test OK: efemerides reais e 5 quadros renderizados."
                          << std::endl;
                canvas.close();
            }
        });

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
