
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
#include "paths.hpp"
#include "borders.hpp"
#include "objects.hpp"
#include "core/bridge.hpp"
#ifdef ALBERT_HAS_RPC
#include "rpc/server.hpp"
#endif
#include "threepp/renderers/gl/GLShadowMap.hpp"
#include "threepp/renderers/gl/GLInfo.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
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
constexpr int LOAD_STEPS = 11;

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
    std::vector<State> sun;
    std::vector<State> moon;
};

struct Orbits {
    std::vector<State> moon;
    std::vector<State> earth;
};

using Focus=albert::core::ObjectId;
namespace targets {
const Focus Earth="earth",Moon="moon",Sun="sun",EarthMoon="@earth-moon",Overview="@system";
}

double radians(double deg) {
    return deg * PI / 180.0;
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
    using namespace std::chrono;

    double seconds = duration<double>(
        system_clock::now().time_since_epoch()
    ).count();

    return 2440587.5 + seconds / 86400.0;
}

double jdTT(double utc) {
    int year, month, day;
    double fraction;

    if (eraJd2cal(
        2400000.5, utc - 2400000.5,
        &year, &month, &day, &fraction
    ) != 0) {
        throw std::runtime_error("Data UTC invalida");
    }

    double leap = 0;

    if (eraDat(year, month, day, fraction, &leap) < 0) {
        throw std::runtime_error("Erro na escala UTC");
    }

    return utc + (leap + 32.184) / 86400.0;
}

double jdTDB(double utc) {
    double tt = jdTT(utc);

    double g = radians(
        357.53 + 0.9856003 * (tt - 2451545.0)
    );

    return tt + (
        0.001658 * std::sin(g) +
        0.000014 * std::sin(2 * g)
    ) / 86400.0;
}

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

std::string encode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("CURL indisponivel");

    char* escaped = curl_easy_escape(
        curl, value.c_str(), static_cast<int>(value.size())
    );

    if (!escaped) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Erro na URL");
    }

    std::string result(escaped);
    curl_free(escaped);
    curl_easy_cleanup(curl);
    return result;
}

std::string dateString(double unixSeconds) {
    std::time_t t =
        static_cast<std::time_t>(std::floor(unixSeconds));

    const auto utc = albert::utcTime(t);

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

// ----------------------------------------
// JPL Horizons
// ----------------------------------------

std::string horizonsURL(
    int body,
    const std::string& center,
    double startUTC,
    double endUTC,
    const std::string& step
) {
    auto tdbSeconds = [](double utc) {
        return (jdTDB(utc) - 2440587.5) * 86400.0;
    };

    std::vector<std::pair<std::string, std::string>> args = {
        {"format", "text"},
        {"COMMAND", "'" + std::to_string(body) + "'"},
        {"OBJ_DATA", "NO"},
        {"MAKE_EPHEM", "YES"},
        {"EPHEM_TYPE", "VECTORS"},
        {"CENTER", "'" + center + "'"},
        {"REF_PLANE", "FRAME"},
        {"REF_SYSTEM", "ICRF"},
        {"OUT_UNITS", "KM-S"},
        {"VEC_TABLE", "2"},
        {"VEC_LABELS", "NO"},
        {"VEC_CORR", "NONE"},
        {"CSV_FORMAT", "YES"},
        {"START_TIME", "'" + dateString(tdbSeconds(startUTC)) + "'"},
        {"STOP_TIME", "'" + dateString(tdbSeconds(endUTC)) + "'"},
        {"STEP_SIZE", "'" + step + "'"}
    };

    std::string url =
        "https://ssd.jpl.nasa.gov/api/horizons.api?";

    for (const auto& [key, value] : args) {
        if (url.back() != '?') url += "&";
        url += key + "=" + encode(value);
    }

    return url;
}

std::vector<State> parseVectors(const std::string& text) {
    size_t begin = text.find("$$SOE");
    size_t end = text.find("$$EOE");

    if (begin == std::string::npos ||
        end == std::string::npos || end <= begin) {
        throw std::runtime_error("Resposta Horizons invalida");
    }

    std::istringstream input(
        text.substr(begin + 5, end - begin - 5)
    );

    std::vector<State> states;
    std::string line;

    while (std::getline(input, line)) {
        std::stringstream stream(line);
        std::vector<std::string> fields;
        std::string field;

        while (std::getline(stream, field, ',')) {
            fields.push_back(field);
        }

        if (fields.size() < 8) continue;

        State s;
        s.jd = std::stod(fields[0]);
        s.position = {
            std::stod(fields[2]),
            std::stod(fields[3]),
            std::stod(fields[4])
        };
        s.velocity = {
            std::stod(fields[5]),
            std::stod(fields[6]),
            std::stod(fields[7])
        };

        states.push_back(s);
    }

    if (states.size() < 2) {
        throw std::runtime_error("Efemerides insuficientes");
    }

    return states;
}

std::vector<State> queryHorizons(
    int body,
    const std::string& center,
    double start,
    double end,
    const std::string& step,
    albert::DownloadProgress* progress = nullptr
) {
    return parseVectors(
        httpGet(horizonsURL(body, center, start, end, step), progress)
    );
}

Ephemerides loadEphemerides(albert::DownloadProgress* progress = nullptr, int offset = 0) {
    double now = jdUTC();

    Ephemerides e;

    if (progress) progress->stage("Efemerides do Sol", 2 + offset);
    e.sun = queryHorizons(
        10, "500@399",
        now - 1.0/1440.0,
        now + 30.0/1440.0,
        "5 min", progress
    );

    if (progress) progress->stage("Efemerides da Lua", 3 + offset);
    e.moon = queryHorizons(
        301, "500@399",
        now - 1.0/1440.0,
        now + 30.0/1440.0,
        "5 min", progress
    );

    return e;
}

Orbits loadOrbits(albert::DownloadProgress* progress = nullptr, int offset = 0) {
    double now = jdUTC();

    Orbits orbits;

    // Trajetoria geocentrica da Lua (~35 dias).
    if (progress) progress->stage("Orbita da Lua", 4 + offset);
    orbits.moon = queryHorizons(
        301, "500@399",
        now - 14.0,
        now + 14.0,
        "3 h", progress
    );

    // Trajetoria heliocentrica da Terra (~370 dias).
    if (progress) progress->stage("Orbita da Terra", 5 + offset);
    orbits.earth = queryHorizons(
        399, "500@10",
        now - 185.0,
        now + 185.0,
        "1 d", progress
    );

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
};

// Network work owns its progress state and never accesses scene/UI objects.
// Scene replacements are applied on the render thread only after full success.
class DataLoader {
public:
    explicit DataLoader(fs::path countries): countries_(std::move(countries)) {}

    ~DataLoader() {
        progress.cancelled = true;
        if (pending_.valid()) pending_.wait();
    }

    void start(bool reloadAssets = true) {
        if (active) return;
        reloadAssets_ = reloadAssets;
        error.clear();
        progress.cancelled = false;
        progress.stage("Preparando simulacao", 0);
        pending_ = std::async(std::launch::async, [this, reloadAssets] {
            LoadedData result;
            if (reloadAssets) {
                result.assets = albert::loadVisualAssets(countries_.parent_path(), progress);
                progress.stage("Preparando cartografia local", 6);
                result.borders = loadBorders(countries_);
            }
            result.ephemerides = loadEphemerides(&progress, 5);
            result.orbits = loadOrbits(&progress, 5);
            progress.stage("Pronto", LOAD_STEPS);
            return result;
        });
        active = true;
    }

    void retry() { start(reloadAssets_); }

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

private:
    fs::path countries_;
    bool reloadAssets_ = true;
    int failures_ = 0;
    std::chrono::steady_clock::time_point nextAttempt_{};
    std::future<LoadedData> pending_;
};

void drawDownloadStatus(DataLoader& loader) {
    const auto status = loader.progress.snapshot();
    ImGui::TextDisabled("CARREGANDO A CENA");
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
    ImGui::TextWrapped("%s", status.stage.c_str());
    ImGui::ProgressBar(static_cast<float>(status.completed) / static_cast<float>(LOAD_STEPS),
                       ImVec2(-1, 5), "");
    if (loader.active) {
        ImGui::TextDisabled("Etapa %d de %d", std::min(status.completed + 1, LOAD_STEPS), LOAD_STEPS);
        if (status.total > 0) {
            const float fraction = static_cast<float>(status.received) / static_cast<float>(status.total);
            ImGui::ProgressBar(fraction, ImVec2(-1, 0));
            ImGui::TextDisabled("%.1f / %.1f MB", status.received / 1e6, status.total / 1e6);
        } else if (status.received > 0) {
            if (status.received < 1000000) {
                ImGui::TextDisabled("%.1f KB recebidos", status.received / 1e3);
            } else {
                ImGui::TextDisabled("%.1f MB recebidos", status.received / 1e6);
            }
        } else {
            constexpr const char* activity[] = {".  ", ".. ", "..."};
            ImGui::TextDisabled("Aguarde%s", activity[static_cast<int>(ImGui::GetTime() * 3) % 3]);
        }
    }
}

void applyInterfaceTheme() {
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 14.f;
    style.FrameRounding = 10.f;
    style.ChildRounding = 10.f;
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
    auto material = albert::orbitMaterial(static_cast<float>(std::min(182.6, (samples.back().jd-samples.front().jd)*.4875)));
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

        fs::path countries =
            albert::assetDirectory() / "countries.geojson";

        Canvas::Parameters window;
        window.title("Albert Engine").antialiasing(4).size(WindowSize(1280,800)).favicon(albert::assetDirectory()/"branding/albert.png");
        Canvas canvas{window};
        canvas.exitOnKeyEscape(false);
        GLRenderer renderer{canvas};
        renderer.toneMapping = ToneMapping::ACESFilmic;
        renderer.toneMappingExposure = 1.05f;
        const float displayPixelRatio = std::min(2.f, renderer.getTargetPixelRatio());
        renderer.setPixelRatio(displayPixelRatio);
        renderer.shadowMap().enabled = true;
        renderer.shadowMap().type = ShadowMap::PFC;
        renderer.shadowMap().autoUpdate = false;
        DataLoader downloads(countries);
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
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) canvas.close();
            ImGui::Text("ALBERT ENGINE");
            ImGui::TextDisabled("PREPARANDO A SIMULACAO");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            drawDownloadStatus(downloads);
            ImGui::Spacing();
            ImGui::TextWrapped("Imagens incluidas no programa. Buscando as posicoes reais da Terra, Lua e Sol.");
            if (ImGui::Button("Sair", ImVec2(-1, 32))) canvas.close();
            ImGui::End();
            ImGui::PopStyleVar();
        };
        drawInterface = [&] { drawLoading(1.f); };
        canvas.onWindowResize([&](WindowSize size) { renderer.setSize(size); });
        downloads.start();
        std::optional<LoadedData> loaded;
        while (canvas.isOpen() && !loaded) {
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
        controls.minDistance = 1.05f;
        controls.maxDistance = 65000.f;

        auto stars = albert::makeStarField(albert::assetDirectory() / "stars.csv", jdTDB(jdUTC()));
        scene->add(stars);
        albert::PlanetVisuals planets(*scene, std::move(*loadedAssets));
        auto earth = planets.earth;
        auto moon = planets.moon;
        auto sun = planets.sun;
        albert::VisualSettings visualSettings;
        albert::core::Scene registry;
        albert::core::Bridge commands;
#ifdef ALBERT_HAS_RPC
        std::unique_ptr<albert::rpc::Server> rpc;
#endif
        albert::SceneObjects objects(*scene);
        objects.bind("earth",earth);objects.bind("moon",moon);objects.bind("sun",sun);
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

        albert::Cache cache(albert::dataDirectory());
        albert::RegionalImagery regional;
        auto nextCacheCheck = std::chrono::steady_clock::now();
        bool showOrbits = true;
        float orbitVisibility=1.f,borderVisibility=0.f;
        std::string refreshError;

        Focus focus = targets::Earth;
        CameraTransition transition;

        Vector3 sunPosition{0, 0, 0};
        Vector3 moonPosition{0, 0, 0};
        Vector3 earthPosition{0, 0, 0};

        auto targetId = [&](const Focus& f) -> Focus {return f==targets::Overview ? targets::Sun : f==targets::EarthMoon ? targets::Earth : f;};
        auto focusName = [&](const Focus& f) -> const char* {return f==targets::Overview ? "Sistema solar" : f==targets::EarthMoon ? "Terra e Lua" : registry.get(f).name.c_str();};
        auto targetFor = [&](const Focus& f) {return albert::renderPosition(registry.evaluate(targetId(f),(jdUTC()-2440587.5)*86400.).state.positionMeters,sceneOrigin);};
        auto distanceFor = [&](const Focus& f) -> float {
            if(f==targets::EarthMoon)return 180.f;
            if(f==targets::Overview)return 65000.f;
            return static_cast<float>(registry.get(f).defaultDistanceMeters/albert::core::earthRadiusMeters);
        };
        auto radiusFor = [&](const Focus& f) -> float {return f.front()=='@' ? 0.f : static_cast<float>(registry.get(f).radiusMeters/albert::core::earthRadiusMeters);};
        auto minimumFor = [&](const Focus& f) -> float {return f.front()=='@' ? .05f : static_cast<float>(registry.get(f).minDistanceMeters/albert::core::earthRadiusMeters);};

        auto setFocus = [&](Focus next, bool resetZoom = false) {
            registry.get(targetId(next));
            if (focus == next && !transition.active && !resetZoom) return;

            controls.dampingFactor = 1.f;
            controls.update(); // Finish drag inertia before capturing the flight.
            transition.active = true;
            transition.preserveDirection = resetZoom;
            transition.elapsed = 0.f;
            transition.startTarget.copy(controls.target);
            transition.startOffset.copy(camera->position);
            transition.startOffset.sub(controls.target);
            transition.destination = next;
            const float scaleChange = std::abs(std::log(distanceFor(next) / std::max(1e-9f, transition.startOffset.length())));
            transition.duration = std::clamp(1.4f + scaleChange * .07f, 1.4f, 2.2f);
            if (resetZoom) transition.duration = std::clamp(.9f + scaleChange * .06f, .9f, 1.3f);

            focus = next;registry.cameraChanged();
        };

        bool refreshing = false;
        bool refreshingOrbits = false;
        bool menuOpen = false;
        float menuProgress = 0.f;
        float resetZoomProgress = 0.f;
        float detailLoaderAge=0.f,detailLoaderAlpha=0.f;
        int menuPage = 0;
        float zoomTarget = 3.5f, zoomDisplayed = 3.5f;
        // Start with the day/terminator visible, independent of UTC season.
        Vector3 initialSun = graphical(interpolate(ephemerides.sun, jdTDB(jdUTC())));
        initialSun.normalize();
        Vector3 tangent(-initialSun.z, 0, initialSun.x);
        tangent.normalize();
        camera->position.copy(initialSun).multiplyScalar(1.8f).add(tangent.multiplyScalar(3.f));
        camera->lookAt(controls.target);
        int appliedQuality = -1;

        drawInterface = [&] {
            const auto display = ImGui::GetIO().DisplaySize;
            constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                if (menuOpen) menuOpen = false;
                else canvas.close();
            }
            const float margin = 16.f;
            const float barWidth = std::min(520.f, display.x - margin * 2);
            ImGui::SetNextWindowPos(ImVec2(margin, margin), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(barWidth, 58));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            ImGui::Begin("Navegacao", nullptr, flags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
            const Focus bodies[] = {targets::Earth, targets::Moon, targets::Sun, targets::Overview};
            const char* labels[] = {"Terra", "Lua", "Sol", "Tudo"};
            // On narrow windows focus selection moves into the same side menu.
            auto focusSelector = [&](bool compact) {
                if (!compact) ImGui::SameLine();
                const float width = (ImGui::GetContentRegionAvail().x - (compact ? 8 : 24)) / (compact ? 2 : 4);
                for (int i = 0; i < 4; ++i) {
                    if ((compact && i % 2) || (!compact && i)) ImGui::SameLine();
                    const bool selected = focus == bodies[i] || (bodies[i] == targets::Overview && focus == targets::EarthMoon);
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.09f,.29f,.36f,1));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.65f,.96f,1.f,1));
                    }
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f);
                    const auto chip = ImGui::GetCursorScreenPos();
                    if (ImGui::Button(labels[i], ImVec2(width, compact ? 36.f : 34.f))) setFocus(bodies[i]);
                    if (selected) ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(chip.x + width*.34f, chip.y+30), ImVec2(chip.x + width*.66f, chip.y+30),
                        ImGui::GetColorU32(ImVec4(108/255.f, 226/255.f, 240/255.f, 230/255.f)), 1.5f);
                    ImGui::PopStyleVar();
                    if (selected) ImGui::PopStyleColor(2);
                }
            };
            const bool compact = display.x < 620;
            if (!compact) focusSelector(false);
            else {
                ImGui::SameLine();
                if (ImGui::Button(focusName(focus), ImVec2(-1, 44))) menuOpen = !menuOpen;
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
                ImGui::TextColored(ImVec4(.5f,.86f,.93f,1), "%s", focusName(focus));
                if(registry.objects().size()>3 && ImGui::BeginCombo("Objeto",focusName(focus))) {
                    for(const auto& [id,object]:registry.objects()) {ImGui::PushID(id.c_str());if(ImGui::Selectable(object.name.c_str(),focus==id)) setFocus(id);ImGui::PopID();}
                    ImGui::EndCombo();
                }
                if(focus.front()!='@' && !registry.get(focus).builtin) {
                    const auto state=registry.evaluate(focus,(jdUTC()-2440587.5)*86400.);
                    ImGui::TextDisabled(state.stale ? "Dados interrompidos: ultima posicao mantida" : "Dados fornecidos pelo integrador");
                }
                ImGui::TextDisabled("Arraste para girar. Role para aproximar.");
                if (compact) focusSelector(true);
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
                    if (focus == targets::Earth || focus == targets::EarthMoon || focus == targets::Overview) {
                        ImGui::Checkbox("Atmosfera", &visualSettings.atmosphere);
                        ImGui::Checkbox("Nuvens em tempo real", &visualSettings.clouds);
                        ImGui::TextDisabled("Observacoes de satelite, com atraso.");
                        ImGui::Checkbox("Luzes das cidades", &visualSettings.nightLights);
                        ImGui::Checkbox("Fronteiras dos paises", &visualSettings.borders);
                    }
                    if (focus == targets::Earth || focus == targets::Moon) ImGui::Checkbox("Sombras", &visualSettings.shadows);
                    ImGui::Checkbox("Caminhos das orbitas", &showOrbits);
                    if (showOrbits) {
                        if (ImGui::Button("Terra e Lua", ImVec2(140, 32))) setFocus(targets::EarthMoon);
                        ImGui::SameLine();
                        if (ImGui::Button("Sistema solar", ImVec2(140, 32))) setFocus(targets::Overview);
                        ImGui::TextDisabled("Distancias e tamanhos em escala real.");
                        ImGui::TextColored(ImVec4(.29f,.67f,.79f,1), "Passado: linha continua com fade");
                        ImGui::TextColored(ImVec4(.94f,.70f,.40f,1), "Futuro: linha tracejada com fade");
                    }
                    if (focus == targets::Moon || focus == targets::Earth) {
                        const bool lunar=focus == targets::Moon;
                        bool& assist=lunar ? visualSettings.moonAssist : visualSettings.earthAssist;
                        float& intensity=lunar ? visualSettings.moonAssistIntensity : visualSettings.earthAssistIntensity;
                        ImGui::Checkbox(lunar ? "Clarear o lado escuro da Lua" : "Clarear o lado noturno da Terra", &assist);
                        if (assist) {
                            ImGui::SetNextItemWidth(-1);
                            int brightness=static_cast<int>(std::round(intensity*100.f));
                            if(ImGui::SliderInt("##Brilho auxiliar", &brightness, 10, 150, "Brilho auxiliar: %d%%"))
                                intensity=brightness/100.f;
                            ImGui::TextDisabled("Ajuda visual para a regiao sem Sol.");
                        }
                    }
                    if (focus == targets::Sun) ImGui::TextWrapped("Explore a superficie solar arrastando para girar a vista.");
                    ImGui::Spacing();
                    ImGui::TextDisabled("QUALIDADE DA IMAGEM");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::Combo("##Qualidade", &visualSettings.quality, "Mais leve\0Mais detalhes\0");
                    ImGui::TextWrapped("Detalhes carregados automaticamente ao aproximar.");
                    ImGui::TextDisabled("Mais leve reduz uso de memoria.");
                } else if (menuPage == 1) {
                    ImGui::TextWrapped("Atualizacao automatica ativada");
                    ImGui::TextDisabled("Voce pode continuar explorando.");
                    if (!refreshError.empty()) ImGui::TextColored(ImVec4(1,.7f,.4f,1), "Posicoes aguardando atualizacao");
                    ImGui::Spacing();
                    ImGui::BeginDisabled(downloads.active || refreshing || refreshingOrbits);
                    if (ImGui::Button("Atualizar posicoes agora", ImVec2(-1, 36))) downloads.start(false);
                    ImGui::EndDisabled();
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
                        ImGui::TextWrapped("O limite vale para downloads de nuvens. A biblioteca incluida permanece intacta.");
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
                    if (ImGui::CollapsingHeader("Manutencao dos arquivos")) {
                        ImGui::TextWrapped("Atualiza os downloads de nuvens sem apagar as texturas incluidas.");
                        ImGui::BeginDisabled(cloudSync.active || cloudSync.detailActive());
                        if (ImGui::Button("Limpar e baixar nuvens", ImVec2(-1, 36))) { visualSettings.clouds=true; cloudSync.refresh(true); }
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(downloads.active || refreshing || refreshingOrbits);
                        if (ImGui::Button("Recarregar texturas incluidas", ImVec2(-1, 36))) { regional.retry(); planets.retryDetails(); downloads.start(true); }
                        ImGui::EndDisabled();
                    }
                    if (ImGui::CollapsingHeader("Detalhes tecnicos")) {
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
                    ImGui::TextWrapped("Terra, Lua e Sol em escala real, com posicoes astronomicas e estrelas catalogadas.");
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Fontes e creditos")) {
                        ImGui::TextWrapped("Imagens: NASA Earth Observatory, SDO/HMI, LROC/ASU, LOLA e GEBCO. Fronteiras: Natural Earth. Estrelas: HYG / David Nash (CC BY-SA 4.0). Fonte: Inter (OFL).");
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
                ImGui::BeginDisabled(!resetZoomVisible);
                if (ImGui::Button("Restaurar zoom", ImVec2(160,42))) setFocus(focus, true);
                ImGui::EndDisabled();
                ImGui::End();
                ImGui::PopStyleVar(4);
            }

            const bool loadingDetails=planets.detailActive() || cloudSync.detailActive() || regional.active();
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
            if (display.x > 900) {
                ImGui::SetNextWindowPos(ImVec2(display.x-margin,margin),ImGuiCond_Always,ImVec2(1,0));
                ImGui::SetNextWindowSize(ImVec2(210,80),ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(.65f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,14));
                ImGui::Begin("Estado", nullptr, (flags & ~ImGuiWindowFlags_AlwaysAutoResize) | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                const auto time = albert::utcTime(std::time(nullptr));
                ImGui::TextDisabled("%02d:%02d:%02d UTC", time.tm_hour, time.tm_min, time.tm_sec);
                if (refreshing || refreshingOrbits || cloudSync.active) ImGui::TextColored(ImVec4(.4f,.85f,.9f,1), "Atualizando dados");
                else if (!cloudSync.error.empty() || !refreshError.empty()) ImGui::TextColored(ImVec4(1,.7f,.4f,1), "Atualizacao pendente");
                else ImGui::TextColored(ImVec4(.4f,.85f,.7f,1), "Dados sincronizados");
                ImGui::End();
                ImGui::PopStyleVar();
            }

            if (focus == targets::Overview || focus == targets::EarthMoon) {
                if(showOrbits) {
                    auto* legend=ImGui::GetForegroundDrawList();
                    const float x=display.x*.5f-130.f, y=display.y-26.f;
                    legend->AddLine(ImVec2(x,y),ImVec2(x+24,y),IM_COL32(74,171,201,220),1.5f);
                    legend->AddText(ImVec2(x+32,y-8),IM_COL32(169,190,204,230),"Passado");
                    for(int i=0;i<3;++i) legend->AddLine(ImVec2(x+136+i*8,y),ImVec2(x+140+i*8,y),IM_COL32(240,179,102,230),1.5f);
                    legend->AddText(ImVec2(x+168,y-8),IM_COL32(210,196,174,230),"Futuro");
                }
                // Screen annotations identify sub-pixel bodies without inflating
                // their physical geometry or changing astronomical distances.
                auto mark = [&](Vector3 position, const char* label, ImU32 color) {
                    position.project(*camera);
                    if (position.z < -1.f || position.z > 1.f) return;
                    const ImVec2 screen((position.x*.5f+.5f)*display.x, (.5f-position.y*.5f)*display.y);
                    if (screen.x < 8 || screen.x > display.x-8 || screen.y < 100 || screen.y > display.y-8) return;
                    auto* draw = ImGui::GetForegroundDrawList();
                    draw->AddCircle(screen, 5.f, color, 16, 1.2f);
                    draw->AddText(ImVec2(screen.x+10,screen.y-8), color, label);
                };
                mark(earthPosition, focus == targets::Overview ? "Terra / Lua" : "Terra", IM_COL32(145,208,229,230));
                if (focus == targets::EarthMoon) mark(moonPosition, "Lua", IM_COL32(215,220,230,230));
                else mark(sunPosition, "Sol", IM_COL32(255,239,204,230));
            }

            if (downloads.active || !downloads.error.empty()) {
                ImGui::SetNextWindowPos(ImVec2(display.x * .5f, display.y * .5f), ImGuiCond_Always, ImVec2(.5f,.5f));
                ImGui::SetNextWindowSize(ImVec2(std::min(460.f, display.x - 32), 0));
                ImGui::Begin("Sincronizacao", nullptr, flags);
                drawDownloadStatus(downloads);
                ImGui::End();
            }
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

        auto nextRefresh = SteadyClock::now() + std::chrono::seconds(REFRESH_SECONDS);
        auto nextOrbitRefresh = SteadyClock::now() + std::chrono::hours(24);
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

        canvas.animate([&] {
            const float elapsed = clock.getDelta();
            const float dt = std::min(elapsed, 0.1f);
            auto steadyNow = SteadyClock::now();
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
                orbits = std::move(updated->orbits);
                refreshError.clear();
                nextRefresh = steadyNow + std::chrono::seconds(REFRESH_SECONDS);
                nextOrbitRefresh = steadyNow + std::chrono::hours(24);
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
                    nextRefresh = steadyNow + std::chrono::seconds(REFRESH_SECONDS);
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
                refreshing = true;

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

                    orbits = std::move(updated);
                    orbitFailures = 0;
                    nextOrbitRefresh = steadyNow + std::chrono::hours(24);

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
                refreshingOrbits = true;

                pendingOrbits = std::async(
                    std::launch::async,
                    [&] { return loadOrbits(&orbitProgress); }
                );
            }

            commands.drain([&](albert::core::Command& command) {
                std::visit([&](auto& value) {
                    using T=std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<T,albert::core::Create>) registry.create(std::move(value.object));
                    else if constexpr(std::is_same_v<T,albert::core::Remove>) {if(registry.get(value.id).builtin)throw std::invalid_argument("Built-in objects cannot be removed");if(focus==value.id)setFocus(targets::Earth);registry.remove(value.id);}
                    else if constexpr(std::is_same_v<T,albert::core::Visibility>) registry.visible(value.id,value.enabled);
                    else if constexpr(std::is_same_v<T,albert::core::Append>) registry.samples(value.id,std::move(value.values));
                    else if constexpr(std::is_same_v<T,albert::core::Focus>) setFocus(value.id,value.reset);
                    else if constexpr(std::is_same_v<T,albert::core::Orbit>) {
                        if(!std::isfinite(value.azimuthRadians)||!std::isfinite(value.elevationRadians)||!std::isfinite(value.distanceScale)||value.distanceScale<=0||value.distanceScale>1000||std::abs(value.azimuthRadians)>2*PI||std::abs(value.elevationRadians)>PI)
                            throw std::invalid_argument("Invalid orbit parameters");
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
            double utc = jdUTC();
            double tdb = jdTDB(utc);

            try {
                Vec3 sunState = interpolate(ephemerides.sun, tdb);
                Vec3 moonState = interpolate(ephemerides.moon, tdb);
                Vec3 earthHelio = interpolate(orbits.earth, tdb);

                const double timestamp=(utc-2440587.5)*86400.;
                registry.builtinState("earth",{timestamp,{0,0,0}});
                registry.builtinState("moon",{timestamp,{moonState.x*1000.,moonState.y*1000.,moonState.z*1000.}});
                registry.builtinState("sun",{timestamp,{sunState.x*1000.,sunState.y*1000.,sunState.z*1000.}});
                const auto nextOriginId=targetId(focus);
                const auto nextOrigin=registry.evaluate(nextOriginId,timestamp).state.positionMeters;
                const auto shift=albert::renderPosition(sceneOrigin,nextOrigin);
                if(originId!=nextOriginId){camera->position.add(shift);controls.target.add(shift);originId=nextOriginId;}
                if(transition.active)transition.startTarget.add(shift);
                sceneOrigin=nextOrigin;
                objects.sync(registry,timestamp,sceneOrigin);
                earthPosition.copy(earth->position);
                sunPosition.copy(sun->position);
                moonPosition.copy(moon->position);
                bordersObject->position.copy(earthPosition);

                sun->position.copy(sunPosition);
                moon->position.copy(moonPosition);

                Vector3 direction = sunPosition;direction.sub(earthPosition);
                direction.normalize();
                lightTarget->position.copy(focus == targets::Moon ? moonPosition : earthPosition);
                sunlight->position.copy(direction).multiplyScalar(150.f).add(lightTarget->position);
                sun->lookAt(earthPosition);
                moon->setRotationFromMatrix(albert::moonOrientation(tdb));

                auto rotation = earthOrientation(utc);
                earth->setRotationFromMatrix(rotation);
                bordersObject->setRotationFromMatrix(rotation);

                // Orbitas heliocentricas convertidas
                // para a origem geocentrica da cena.
                earthOrbit->origin=earthHelio;
                updateOrbitTime(moonOrbit, orbits.moon, tdb);
                updateOrbitTime(earthOrbit, orbits.earth, tdb);

                dataValid = true;
            } catch (const std::exception& e) {
                if (smokeTest) throw;
                if (dataValid) {
                    std::cerr << e.what() << '\n';
                }

                dataValid = false;
                refreshError = e.what();
            }

            orbitVisibility=albert::animateValue(orbitVisibility,showOrbits ? 1.f : 0.f,dt);
            borderVisibility=albert::animateValue(borderVisibility,visualSettings.borders ? 1.f : 0.f,dt);
            bordersObject->visible = borderVisibility>0.f;
            // Show the selected physical path in close views, and both in context.
            moonOrbit->visible = orbitVisibility>0.f && focus != targets::Sun;
            earthOrbit->visible = orbitVisibility>0.f && (focus == targets::Earth || focus == targets::Sun || focus == targets::Overview);

            // Camera segue o objeto sem acumular erro
            Vector3 target = targetFor(focus);

            if (transition.active) {
                transition.elapsed += elapsed;

                float t = smoothstep(
                    transition.elapsed / transition.duration
                );

                Vector3 offset = transition.startOffset;

                float startDistance = offset.length();
                if (startDistance < 1e-9f) {
                    offset.set(0, 0, 1);
                    startDistance = 1.f;
                }

                offset.normalize();
                if (!transition.preserveDirection) {
                    if (transition.destination == targets::Overview || transition.destination == targets::EarthMoon) {
                        Vector3 above(0.2f, 0.9f, 0.7f);
                        offset = arcDirection(offset, above, t);
                    } else if (transition.destination == targets::Sun) {
                        Vector3 earthView = earthPosition;
                        earthView.sub(sunPosition).normalize();
                        offset = arcDirection(offset, earthView, t);
                    } else {
                        Vector3 day = sunPosition;
                        day.sub(target).normalize();
                        Vector3 tangent(-day.z, 0, day.x);
                        tangent.normalize();
                        day.multiplyScalar(.515f).add(tangent.multiplyScalar(.857f));
                        offset = arcDirection(offset, day, t);
                    }
                }

                // Interpolacao logaritmica do zoom:
                // transicoes suaves entre escalas astronomicas.
                float a = std::log(startDistance);
                float b = std::log(
                    distanceFor(transition.destination)
                );

                float distance = std::exp(std::lerp(a, b, t));
                const float travel = transition.startTarget.distanceTo(target);
                distance += travel * .10f * std::pow(std::sin(static_cast<float>(PI)*t), 2.f);

                Vector3 nextTarget = lerpVector(
                    transition.startTarget,
                    target, t
                );

                camera->position.copy(nextTarget);
                camera->position.add(
                    offset.multiplyScalar(distance)
                );

                controls.target.copy(nextTarget);

                controls.minDistance = std::min(.05f,minimumFor(transition.destination));

                if (transition.elapsed >= transition.duration) {
                    transition.active = false;
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
            const float bodyRadius = radiusFor(focus);
            const float clearance = std::max(1e-8f, distance - bodyRadius);
            camera->nearPlane = std::clamp(std::min(distance * .02f, clearance * .25f), 1e-9f, 100.f);
            camera->updateProjectionMatrix();
            const auto display=ImGui::GetIO().DisplaySize;
            auto orbitView=[&](const std::shared_ptr<Mesh>& path, const Vector3& body, float daysPerUnit) {
                auto* material=path->material()->as<ShaderMaterial>();
                material->uniforms.at("visibility").setValue(orbitVisibility);
                material->uniforms.at("viewport").setValue(Vector2(display.x,display.y));
                material->uniforms.at("nearClip").setValue(camera->nearPlane);
                material->uniforms.at("dashDays").setValue(std::max(.0001f,
                    camera->position.distanceTo(body)*.93f*24.f/std::max(1.f,display.y)*daysPerUnit));
            };
            if(moonOrbit->visible) moonOrbit->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
            if(earthOrbit->visible) earthOrbit->rebase(camera->position,{sceneOrigin.x/1000.,sceneOrigin.y/1000.,sceneOrigin.z/1000.});
            orbitView(moonOrbit,moonPosition,.0721f);
            orbitView(earthOrbit,earthPosition,.00248f);

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
            if(visualSettings.quality && focus==targets::Earth && earthCamera.length()<2.2f) {
                const double latitude=albert::geodeticLatitude(std::asin(std::clamp(earthCamera.y/earthCamera.length(),-1.f,1.f)))*180./PI;
                const double lon=albert::longitude(std::atan2(earthCamera.z,-earthCamera.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,earthCamera.length(),1.,camera->aspect,camera->fov,1.40625));
            } else if(visualSettings.quality && focus==targets::Moon && camera->position.distanceTo(moonPosition)<.8f) {
                Vector3 local=camera->position;moon->worldToLocal(local);
                const double latitude=std::asin(std::clamp(local.y/local.length(),-1.f,1.f))*180./PI;
                const double lon=albert::longitude(std::atan2(local.z,-local.x)*180./PI-180.);
                regional.request(albert::viewRegion(lon,latitude,local.length(),1737.4/6378.137,camera->aspect,camera->fov,1.40625),true);
            } else regional.request(std::nullopt);
            if(auto images=planets.uploading() ? std::nullopt : regional.poll(); images) {
                if(!images->error.empty()) std::cerr<<"Detalhes regionais: "<<images->error<<std::endl;
                if(images->day || images->night) {
                std::cout<<"Detalhes locais carregados: "<<(images->moon ? "Lua LROC" : "Terra")<<std::endl;
                planets.stageRegionalImages(std::move(*images));
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
            if(rpcPort) {
                const double r=albert::core::earthRadiusMeters;
                commands.publish(registry.snapshot((utc-2440587.5)*86400.,{focus,
                    {sceneOrigin.x+camera->position.x*r,sceneOrigin.y-camera->position.z*r,sceneOrigin.z+camera->position.y*r},
                    camera->position.distanceTo(controls.target)*r,transition.active}));
            }
            renderer.render(*scene, *camera);
            ui.render();
#ifdef ALBERT_HAS_RPC
            if(rpcPort && !rpc && introductionElapsed>=introductionDuration) {
                rpc=std::make_unique<albert::rpc::Server>(commands,*rpcPort);
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
