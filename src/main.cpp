#include "raylib.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "raymath.h"
#include "rlgl.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr float kDefaultFov = 90.0f;
constexpr float kMYaw = 0.022f;
constexpr float kMPitch = 0.022f;
constexpr float kDefaultSensitivity = 3.0f;
constexpr int kBubbleCount = 8;
constexpr float kBubbleRadius = 0.42f;
constexpr float kMinBubbleHeight = 1.1f;
constexpr float kMaxBubbleHeight = 6.5f;
constexpr float kPi = 3.14159265358979323846f;

struct Bubble {
    Vector3 position{};
    float radius = kBubbleRadius;
    Color color = SKYBLUE;
};

struct Stats {
    int hits = 0;
    int misses = 0;
    double startedAt = 0.0;
};

struct Config {
    float fov = kDefaultFov;
    float sensitivity = kDefaultSensitivity;
    float mYaw = kMYaw;
    float mPitch = kMPitch;
    std::string crosshair = "assets/ch9.png";
    float crosshairScale = 1.0f;
    Color crosshairColor = Color{110, 250, 255, 255};
    std::string skybox = "assets/space1";
    float skyboxSize = 96.0f;
    Color skyboxTint = WHITE;
};

struct Crosshair {
    Texture2D texture{};
    bool loaded = false;
};

struct Skybox {
    Model model{};
    TextureCubemap cubemap{};
    Shader shader{};
    std::string status = "skybox off";
    bool loaded = false;
};

static json ColorJson(Color color) {
    return json::array({color.r, color.g, color.b, color.a});
}

static Color ParseColor(const json &data, Color fallback) {
    if (!data.is_array() || data.size() < 3) return fallback;

    const auto component = [&](size_t index) {
        const int value = data[index].get<int>();
        return static_cast<unsigned char>(std::clamp(value, 0, 255));
    };

    return Color{
        component(0),
        component(1),
        component(2),
        data.size() >= 4 ? component(3) : fallback.a,
    };
}

static json ConfigJson(const Config &config) {
    return json{
        {"fov", config.fov},
        {"sensitivity", config.sensitivity},
        {"m_yaw", config.mYaw},
        {"m_pitch", config.mPitch},
        {"crosshair", config.crosshair},
        {"crosshair_scale", config.crosshairScale},
        {"crosshair_color", ColorJson(config.crosshairColor)},
        {"skybox", config.skybox},
        {"skybox_size", config.skyboxSize},
        {"skybox_tint", ColorJson(config.skyboxTint)},
    };
}

static void SaveConfig(const fs::path &path, const Config &config) {
    std::ofstream out(path);
    if (!out) return;
    out << ConfigJson(config).dump(2) << '\n';
}

static Config LoadConfig() {
    Config config;
    const fs::path path = fs::path(GetApplicationDirectory()) / "q2aimtrainer.json";

    std::ifstream in(path);
    if (!in) {
        SaveConfig(path, config);
        return config;
    }

    try {
        const json data = json::parse(in);
        config.fov = data.value("fov", config.fov);
        config.sensitivity = data.value("sensitivity", config.sensitivity);
        config.mYaw = data.value("m_yaw", config.mYaw);
        config.mPitch = data.value("m_pitch", config.mPitch);
        config.crosshair = data.value("crosshair", config.crosshair);
        config.crosshairScale = data.value("crosshair_scale", config.crosshairScale);
        if (data.contains("crosshair_color")) config.crosshairColor = ParseColor(data["crosshair_color"], config.crosshairColor);
        config.skybox = data.value("skybox", config.skybox);
        config.skyboxSize = data.value("skybox_size", config.skyboxSize);
        if (data.contains("skybox_tint")) config.skyboxTint = ParseColor(data["skybox_tint"], config.skyboxTint);
    } catch (const std::exception &e) {
        std::cerr << "Failed to read " << path << ": " << e.what() << '\n';
    }

    config.fov = std::clamp(config.fov, 45.0f, 140.0f);
    config.sensitivity = std::clamp(config.sensitivity, 0.1f, 20.0f);
    config.mYaw = std::clamp(config.mYaw, 0.001f, 0.2f);
    config.mPitch = std::clamp(config.mPitch, 0.001f, 0.2f);
    config.crosshairScale = std::clamp(config.crosshairScale, 0.25f, 8.0f);
    config.skyboxSize = std::clamp(config.skyboxSize, 32.0f, 512.0f);
    return config;
}

static fs::path ResolveAppPath(const std::string &path) {
    fs::path result(path);
    if (result.is_relative()) result = fs::path(GetApplicationDirectory()) / result;
    return result;
}

static Crosshair LoadCrosshair(const Config &config) {
    Crosshair crosshair;
    if (config.crosshair.empty()) return crosshair;

    const fs::path path = ResolveAppPath(config.crosshair);
    if (path.extension() != ".png") {
        std::cerr << "Only .png crosshairs are supported: " << path << '\n';
        return crosshair;
    }

    crosshair.texture = LoadTexture(path.string().c_str());
    crosshair.loaded = crosshair.texture.id != 0;
    if (!crosshair.loaded) std::cerr << "Failed to load crosshair: " << path << '\n';
    return crosshair;
}

static fs::path SkyboxFacePath(const std::string &base, const char *suffix) {
    fs::path path = ResolveAppPath(base);
    return path.parent_path() / (path.filename().string() + suffix + ".tga");
}

static Image LoadSkyboxStrip(const Config &config, std::string &error) {
    // raylib cubemaps expect vertical faces in this order: +X, -X, +Y, -Y, +Z, -Z.
    constexpr std::array<const char *, 6> suffixes = {"rt", "lf", "up", "dn", "bk", "ft"};

    std::array<Image, 6> faces{};
    int faceSize = 0;
    for (size_t i = 0; i < suffixes.size(); ++i) {
        const fs::path path = SkyboxFacePath(config.skybox, suffixes[i]);
        faces[i] = LoadImage(path.string().c_str());
        if (faces[i].data == nullptr) {
            error = "skybox load failed: " + path.filename().string();
            for (size_t j = 0; j < i; ++j) UnloadImage(faces[j]);
            return {};
        }

        if (i == 0) faceSize = faces[i].width;
        if (faces[i].width != faceSize || faces[i].height != faceSize) {
            error = "skybox face size mismatch: " + path.filename().string();
            for (size_t j = 0; j <= i; ++j) UnloadImage(faces[j]);
            return {};
        }
        ImageFormat(&faces[i], PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

        if (i == 2 || i == 3) ImageRotate(&faces[i], 180);
    }

    Image strip = GenImageColor(faceSize, faceSize * 6, BLANK);
    ImageFormat(&strip, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    constexpr float skyboxEdgeCrop = 2.0f;
    for (int i = 0; i < 6; ++i) {
        ImageDraw(
            &strip,
            faces[static_cast<size_t>(i)],
            {skyboxEdgeCrop, skyboxEdgeCrop, static_cast<float>(faceSize) - skyboxEdgeCrop * 2.0f, static_cast<float>(faceSize) - skyboxEdgeCrop * 2.0f},
            {0.0f, static_cast<float>(faceSize * i), static_cast<float>(faceSize), static_cast<float>(faceSize)},
            WHITE
        );
        UnloadImage(faces[static_cast<size_t>(i)]);
    }

    return strip;
}

static Skybox LoadSkybox(const Config &config) {
    Skybox skybox;
    if (config.skybox.empty()) return skybox;

    std::string error;
    Image strip = LoadSkyboxStrip(config, error);
    if (strip.data == nullptr) {
        skybox.status = error;
        std::cerr << skybox.status << '\n';
        return skybox;
    }

    skybox.cubemap = LoadTextureCubemap(strip, CUBEMAP_LAYOUT_LINE_VERTICAL);
    UnloadImage(strip);
    if (skybox.cubemap.id == 0) {
        skybox.status = "skybox cubemap failed";
        std::cerr << skybox.status << '\n';
        return skybox;
    }
    SetTextureWrap(skybox.cubemap, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(skybox.cubemap, TEXTURE_FILTER_POINT);

    constexpr const char *skyboxVs = R"glsl(
#version 330
in vec3 vertexPosition;
uniform mat4 matProjection;
uniform mat4 matView;
out vec3 fragPosition;
void main()
{
    fragPosition = vertexPosition;
    mat4 rotView = mat4(mat3(matView));
    vec4 clipPos = matProjection*rotView*vec4(vertexPosition, 1.0);
    gl_Position = clipPos.xyww;
}
)glsl";

    constexpr const char *skyboxFs = R"glsl(
#version 330
in vec3 fragPosition;
uniform samplerCube environmentMap;
uniform vec4 skyboxTint;
out vec4 finalColor;
void main()
{
    finalColor = texture(environmentMap, fragPosition)*skyboxTint;
}
)glsl";

    skybox.shader = LoadShaderFromMemory(skyboxVs, skyboxFs);
    const int environmentMap = MATERIAL_MAP_CUBEMAP;
    SetShaderValue(skybox.shader, GetShaderLocation(skybox.shader, "environmentMap"), &environmentMap, SHADER_UNIFORM_INT);
    const float tint[4] = {
        static_cast<float>(config.skyboxTint.r) / 255.0f,
        static_cast<float>(config.skyboxTint.g) / 255.0f,
        static_cast<float>(config.skyboxTint.b) / 255.0f,
        static_cast<float>(config.skyboxTint.a) / 255.0f,
    };
    SetShaderValue(skybox.shader, GetShaderLocation(skybox.shader, "skyboxTint"), tint, SHADER_UNIFORM_VEC4);

    skybox.model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    skybox.model.materials[0].shader = skybox.shader;
    skybox.model.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = skybox.cubemap;

    skybox.loaded = true;
    skybox.status = "skybox " + config.skybox;
    return skybox;
}

static void UnloadSkybox(Skybox &skybox) {
    if (!skybox.loaded) return;
    UnloadShader(skybox.shader);
    UnloadTexture(skybox.cubemap);
    UnloadModel(skybox.model);
    skybox.loaded = false;
}

static float DegToRad(float degrees) {
    return degrees * kPi / 180.0f;
}

static Vector3 ForwardFromAngles(float yawDeg, float pitchDeg) {
    const float yaw = DegToRad(yawDeg);
    const float pitch = DegToRad(pitchDeg);
    const float cp = std::cos(pitch);
    return Vector3Normalize({std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp});
}

static Vector3 RandomBubblePositionInView(std::mt19937 &rng, const Camera3D &camera) {
    const float margin = 120.0f;
    std::uniform_real_distribution<float> xDist(margin, std::max(margin, static_cast<float>(GetScreenWidth()) - margin));
    std::uniform_real_distribution<float> yDist(margin, std::max(margin, static_cast<float>(GetScreenHeight()) - margin * 1.35f));
    std::uniform_real_distribution<float> distanceDist(8.0f, 24.0f);

    for (int attempt = 0; attempt < 64; ++attempt) {
        const Ray ray = GetScreenToWorldRay({xDist(rng), yDist(rng)}, camera);
        const Vector3 position = Vector3Add(camera.position, Vector3Scale(ray.direction, distanceDist(rng)));
        if (position.y >= kMinBubbleHeight && position.y <= kMaxBubbleHeight) return position;
    }

    const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 fallback = Vector3Add(camera.position, Vector3Scale(forward, 14.0f));
    fallback.y = std::clamp(fallback.y, kMinBubbleHeight, kMaxBubbleHeight);
    return fallback;
}

static Color BubbleColor(int index) {
    constexpr std::array<Color, 5> colors = {
        Color{80, 220, 255, 255},
        Color{255, 95, 210, 255},
        Color{120, 255, 120, 255},
        Color{255, 210, 80, 255},
        Color{180, 130, 255, 255},
    };
    return colors[static_cast<size_t>(index) % colors.size()];
}

static void ResetBubbles(std::vector<Bubble> &bubbles, std::mt19937 &rng, const Camera3D &camera) {
    bubbles.clear();
    for (int i = 0; i < kBubbleCount; ++i) {
        bubbles.push_back({RandomBubblePositionInView(rng, camera), kBubbleRadius, BubbleColor(i)});
    }
}

static bool BubbleIsVisible(const Bubble &bubble, const Camera3D &camera) {
    if (bubble.position.y < kMinBubbleHeight || bubble.position.y > kMaxBubbleHeight) return false;

    const Vector3 cameraForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 toBubble = Vector3Normalize(Vector3Subtract(bubble.position, camera.position));
    if (Vector3DotProduct(cameraForward, toBubble) <= 0.0f) return false;

    const Vector2 screen = GetWorldToScreen(bubble.position, camera);
    return screen.x >= 0.0f && screen.x <= static_cast<float>(GetScreenWidth()) &&
           screen.y >= 0.0f && screen.y <= static_cast<float>(GetScreenHeight());
}

static void ResetStats(Stats &stats) {
    stats.hits = 0;
    stats.misses = 0;
    stats.startedAt = GetTime();
}

static void ToggleFullscreenWindow() {
    if (IsWindowFullscreen()) {
        ToggleFullscreen();
        SetWindowSize(1280, 720);
        return;
    }

    const int monitor = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
    ToggleFullscreen();
}

static int ShootBubble(const Camera3D &camera, std::vector<Bubble> &bubbles) {
    const Vector2 center = {GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
    const Ray ray = GetScreenToWorldRay(center, camera);

    int hitIndex = -1;
    float nearest = 1000000.0f;
    for (int i = 0; i < static_cast<int>(bubbles.size()); ++i) {
        const RayCollision hit = GetRayCollisionSphere(ray, bubbles[static_cast<size_t>(i)].position, bubbles[static_cast<size_t>(i)].radius);
        if (hit.hit && hit.distance < nearest) {
            nearest = hit.distance;
            hitIndex = i;
        }
    }
    return hitIndex;
}

static void DrawSkybox(const Skybox &skybox, const Config &config, const Camera3D &camera) {
    (void)config;
    (void)camera;
    if (!skybox.loaded) return;

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawModel(skybox.model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

static void DrawArena(const Skybox &skybox, const Config &config, const Camera3D &camera) {
    ClearBackground(Color{5, 7, 11, 255});
    DrawSkybox(skybox, config, camera);
    DrawPlane({0.0f, -0.02f, 12.0f}, {64.0f, 64.0f}, Color{15, 18, 24, 255});
    DrawGrid(64, 1.0f);
}

static void DrawCrosshair(const Crosshair &crosshair, const Config &config) {
    const int cx = GetScreenWidth() / 2;
    const int cy = GetScreenHeight() / 2;
    if (crosshair.loaded) {
        const float width = static_cast<float>(crosshair.texture.width) * config.crosshairScale;
        const float height = static_cast<float>(crosshair.texture.height) * config.crosshairScale;
        DrawTexturePro(
            crosshair.texture,
            {0.0f, 0.0f, static_cast<float>(crosshair.texture.width), static_cast<float>(crosshair.texture.height)},
            {static_cast<float>(cx), static_cast<float>(cy), width, height},
            {width * 0.5f, height * 0.5f},
            0.0f,
            config.crosshairColor
        );
        return;
    }

    const Color cyan = Color{110, 250, 255, 255};
    DrawLine(cx - 14, cy, cx - 5, cy, cyan);
    DrawLine(cx + 5, cy, cx + 14, cy, cyan);
    DrawLine(cx, cy - 14, cx, cy - 5, cyan);
    DrawLine(cx, cy + 5, cx, cy + 14, cyan);
    DrawCircleLines(cx, cy, 3.0f, Color{255, 255, 255, 210});
}

static void DrawHud(const Stats &stats, const Config &config, const Skybox &skybox) {
    const double elapsed = std::max(0.001, GetTime() - stats.startedAt);
    const int shots = stats.hits + stats.misses;
    const float accuracy = shots > 0 ? 100.0f * static_cast<float>(stats.hits) / static_cast<float>(shots) : 100.0f;
    const float hpm = static_cast<float>(stats.hits) * 60.0f / static_cast<float>(elapsed);

    DrawRectangle(18, 16, 430, 162, Color{4, 8, 12, 190});
    DrawRectangleLinesEx({18.0f, 16.0f, 430.0f, 162.0f}, 2.0f, Color{80, 220, 255, 255});

    char line[160]{};
    std::snprintf(line, sizeof(line), "hits %d   misses %d   acc %.1f%%", stats.hits, stats.misses, accuracy);
    DrawText(line, 34, 32, 20, Color{220, 245, 255, 255});
    std::snprintf(line, sizeof(line), "hits/min %.1f   time %.1fs", hpm, elapsed);
    DrawText(line, 34, 58, 20, Color{170, 220, 230, 255});
    std::snprintf(line, sizeof(line), "fov %.0f   sens %.2f", config.fov, config.sensitivity);
    DrawText(line, 34, 84, 20, Color{170, 220, 230, 255});
    std::snprintf(line, sizeof(line), "m_yaw %.3f   m_pitch %.3f", config.mYaw, config.mPitch);
    DrawText(line, 34, 110, 20, Color{170, 220, 230, 255});
    std::snprintf(line, sizeof(line), "%s", skybox.status.c_str());
    DrawText(line, 34, 136, 20, skybox.loaded ? Color{170, 220, 230, 255} : Color{255, 120, 120, 255});

    DrawText("LMB shoot   R reset   -/= sens   F11 fullscreen   Esc quit", 18, GetScreenHeight() - 32, 18, Color{170, 220, 230, 230});
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "q2aimtrainer");
    ToggleFullscreenWindow();
    DisableCursor();
    SetTargetFPS(240);

    Config config = LoadConfig();
    Crosshair crosshair = LoadCrosshair(config);
    Skybox skybox = LoadSkybox(config);

    std::mt19937 rng(std::random_device{}());
    std::vector<Bubble> bubbles;

    Stats stats;
    ResetStats(stats);

    Camera3D camera{};
    camera.position = {0.0f, 1.7f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = config.fov;
    camera.projection = CAMERA_PERSPECTIVE;

    float yaw = 0.0f;
    float pitch = 0.0f;
    camera.target = Vector3Add(camera.position, ForwardFromAngles(yaw, pitch));
    ResetBubbles(bubbles, rng, camera);

    double lowVisibleBubblesSince = 0.0;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11)) ToggleFullscreenWindow();
        if (IsKeyPressed(KEY_R)) {
            ResetStats(stats);
            ResetBubbles(bubbles, rng, camera);
        }
        if (IsKeyPressed(KEY_MINUS)) config.sensitivity = std::max(0.1f, config.sensitivity - 0.1f);
        if (IsKeyPressed(KEY_EQUAL)) config.sensitivity = std::min(20.0f, config.sensitivity + 0.1f);

        const Vector2 mouse = GetMouseDelta();
        yaw -= mouse.x * config.sensitivity * config.mYaw;
        pitch -= mouse.y * config.sensitivity * config.mPitch;
        pitch = std::clamp(pitch, -89.0f, 89.0f);

        const Vector3 forward = ForwardFromAngles(yaw, pitch);
        camera.target = Vector3Add(camera.position, forward);

        const int visibleBubbles = static_cast<int>(std::count_if(bubbles.begin(), bubbles.end(), [&](const Bubble &bubble) {
            return BubbleIsVisible(bubble, camera);
        }));
        if (visibleBubbles >= 3) {
            lowVisibleBubblesSince = 0.0;
        } else if (lowVisibleBubblesSince <= 0.0) {
            lowVisibleBubblesSince = GetTime();
        } else if (GetTime() - lowVisibleBubblesSince > 0.35) {
            for (Bubble &bubble : bubbles) {
                if (!BubbleIsVisible(bubble, camera)) bubble.position = RandomBubblePositionInView(rng, camera);
            }
            lowVisibleBubblesSince = 0.0;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const int hit = ShootBubble(camera, bubbles);
            if (hit >= 0) {
                ++stats.hits;
                bubbles[static_cast<size_t>(hit)].position = RandomBubblePositionInView(rng, camera);
            } else {
                ++stats.misses;
            }
        }

        BeginDrawing();
        BeginMode3D(camera);
        DrawArena(skybox, config, camera);
        for (const Bubble &bubble : bubbles) {
            DrawSphere(bubble.position, bubble.radius, bubble.color);
            DrawSphereWires(bubble.position, bubble.radius * 1.08f, 16, 16, Color{255, 255, 255, 180});
        }
        EndMode3D();
        DrawCrosshair(crosshair, config);
        DrawHud(stats, config, skybox);
        EndDrawing();
    }

    if (crosshair.loaded) UnloadTexture(crosshair.texture);
    UnloadSkybox(skybox);
    CloseWindow();
}
