#include "raylib.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "raymath.h"

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
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr float kDefaultFov = 90.0f;
constexpr float kMYaw = 0.022f;
constexpr float kMPitch = 0.022f;
constexpr float kDefaultSensitivity = 3.0f;
constexpr int kBubbleCount = 8;
constexpr float kBubbleRadius = 0.42f;
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
};

static json ConfigJson(const Config &config) {
    return json{
        {"fov", config.fov},
        {"sensitivity", config.sensitivity},
        {"m_yaw", config.mYaw},
        {"m_pitch", config.mPitch},
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
    } catch (const std::exception &e) {
        std::cerr << "Failed to read " << path << ": " << e.what() << '\n';
    }

    config.fov = std::clamp(config.fov, 45.0f, 140.0f);
    config.sensitivity = std::clamp(config.sensitivity, 0.1f, 20.0f);
    config.mYaw = std::clamp(config.mYaw, 0.001f, 0.2f);
    config.mPitch = std::clamp(config.mPitch, 0.001f, 0.2f);
    return config;
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
    const float margin = 90.0f;
    std::uniform_real_distribution<float> xDist(margin, std::max(margin, static_cast<float>(GetScreenWidth()) - margin));
    std::uniform_real_distribution<float> yDist(margin, std::max(margin, static_cast<float>(GetScreenHeight()) - margin));
    std::uniform_real_distribution<float> distanceDist(8.0f, 24.0f);

    const Ray ray = GetScreenToWorldRay({xDist(rng), yDist(rng)}, camera);
    return Vector3Add(camera.position, Vector3Scale(ray.direction, distanceDist(rng)));
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
    const Vector3 cameraForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    const Vector3 toBubble = Vector3Normalize(Vector3Subtract(bubble.position, camera.position));
    if (Vector3DotProduct(cameraForward, toBubble) <= 0.0f) return false;

    const Vector2 screen = GetWorldToScreen(bubble.position, camera);
    const float margin = bubble.radius * 80.0f;
    return screen.x >= -margin && screen.x <= static_cast<float>(GetScreenWidth()) + margin &&
           screen.y >= -margin && screen.y <= static_cast<float>(GetScreenHeight()) + margin;
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

static void DrawArena() {
    ClearBackground(Color{5, 7, 11, 255});
    DrawPlane({0.0f, -0.02f, 12.0f}, {64.0f, 64.0f}, Color{15, 18, 24, 255});
    DrawGrid(64, 1.0f);

    for (int i = -4; i <= 4; ++i) {
        const float x = static_cast<float>(i) * 6.0f;
        DrawCubeWires({x, 3.0f, 24.0f}, 0.05f, 6.0f, 0.05f, Color{40, 130, 150, 120});
        DrawCubeWires({x, 0.05f, 24.0f}, 0.05f, 0.05f, 24.0f, Color{40, 130, 150, 90});
    }
}

static void DrawCrosshair() {
    const int cx = GetScreenWidth() / 2;
    const int cy = GetScreenHeight() / 2;
    const Color cyan = Color{110, 250, 255, 255};
    DrawLine(cx - 14, cy, cx - 5, cy, cyan);
    DrawLine(cx + 5, cy, cx + 14, cy, cyan);
    DrawLine(cx, cy - 14, cx, cy - 5, cyan);
    DrawLine(cx, cy + 5, cx, cy + 14, cyan);
    DrawCircleLines(cx, cy, 3.0f, Color{255, 255, 255, 210});
}

static void DrawHud(const Stats &stats, const Config &config) {
    const double elapsed = std::max(0.001, GetTime() - stats.startedAt);
    const int shots = stats.hits + stats.misses;
    const float accuracy = shots > 0 ? 100.0f * static_cast<float>(stats.hits) / static_cast<float>(shots) : 100.0f;
    const float hpm = static_cast<float>(stats.hits) * 60.0f / static_cast<float>(elapsed);

    DrawRectangle(18, 16, 330, 136, Color{4, 8, 12, 190});
    DrawRectangleLinesEx({18.0f, 16.0f, 330.0f, 136.0f}, 2.0f, Color{80, 220, 255, 255});

    char line[160]{};
    std::snprintf(line, sizeof(line), "hits %d   misses %d   acc %.1f%%", stats.hits, stats.misses, accuracy);
    DrawText(line, 34, 32, 20, Color{220, 245, 255, 255});
    std::snprintf(line, sizeof(line), "hits/min %.1f   time %.1fs", hpm, elapsed);
    DrawText(line, 34, 58, 20, Color{170, 220, 230, 255});
    std::snprintf(line, sizeof(line), "fov %.0f   sens %.2f", config.fov, config.sensitivity);
    DrawText(line, 34, 84, 20, Color{170, 220, 230, 255});
    std::snprintf(line, sizeof(line), "m_yaw %.3f   m_pitch %.3f", config.mYaw, config.mPitch);
    DrawText(line, 34, 110, 20, Color{170, 220, 230, 255});

    DrawText("LMB shoot   R reset   -/= sens   F11 fullscreen   Esc quit", 18, GetScreenHeight() - 32, 18, Color{170, 220, 230, 230});
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "q2aimtrainer");
    ToggleFullscreenWindow();
    DisableCursor();
    SetTargetFPS(240);

    Config config = LoadConfig();

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

    double noVisibleBubblesSince = 0.0;

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

        const bool anyVisible = std::any_of(bubbles.begin(), bubbles.end(), [&](const Bubble &bubble) {
            return BubbleIsVisible(bubble, camera);
        });
        if (anyVisible) {
            noVisibleBubblesSince = 0.0;
        } else if (noVisibleBubblesSince <= 0.0) {
            noVisibleBubblesSince = GetTime();
        } else if (GetTime() - noVisibleBubblesSince > 0.75) {
            ResetBubbles(bubbles, rng, camera);
            noVisibleBubblesSince = 0.0;
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
        DrawArena();
        for (const Bubble &bubble : bubbles) {
            DrawSphere(bubble.position, bubble.radius, bubble.color);
            DrawSphereWires(bubble.position, bubble.radius * 1.08f, 16, 16, Color{255, 255, 255, 180});
        }
        EndMode3D();
        DrawCrosshair();
        DrawHud(stats, config);
        EndDrawing();
    }

    CloseWindow();
}
