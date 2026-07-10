# q2aimtrainer

Stationary 3D Quake II-style aim trainer. Built with the same core stack as `q2manager`: C++23, CMake, and raylib.

## Features

- Fullscreen 3D stationary aim trainer
- Center-screen raycast shooting
- Multiple random 3D bubbles active at once
- Quake II-style mouse constants: `m_yaw 0.022`, `m_pitch 0.022`
- Default `fov 90`
- JSON config beside the executable for FOV and mouse tuning
- Themes: `cyber` and neon-green `matrix`
- PNG crosshair with configurable scale and RGBA tint
- Hit sound on successful bubble clicks
- Quake II-style six-face TGA skybox support
- Quake II-style ground friction, acceleration, air movement, and jumping
- HUD with hits, misses, accuracy, time, and hits/min

## Controls

- Mouse: aim
- `WASD`: move
- Left click: shoot
- Right click: jump
- `R`: reset session
- `-` / `=`: decrease/increase sensitivity
- `F11`: toggle fullscreen
- `Esc`: quit

## Config

Edit `q2aimtrainer.json` beside the executable, then restart the trainer:

```json
{
  "theme": "cyber",
  "fov": 90.0,
  "sensitivity": 3.0,
  "m_yaw": 0.022,
  "m_pitch": 0.022,
  "crosshair": "assets/ch9.png",
  "crosshair_scale": 1.0,
  "crosshair_color": [110, 250, 255, 255],
  "hit_sound": "assets/marker.wav",
  "hit_sound_volume": 1.0,
  "skybox": "assets/space1",
  "skybox_size": 96.0,
  "skybox_tint": [255, 255, 255, 255]
}
```

`theme` supports `cyber` and `matrix`. The theme changes HUD, floor/grid, fallback crosshair, and bubble colors. Explicit `crosshair_color` and `skybox_tint` still override those specific colors.

`crosshair` supports `.png` files. Relative paths are resolved from the executable directory, so `assets/ch9.png` points to `dist/assets/ch9.png` when running the dist build. `crosshair_color` is `[red, green, blue, alpha]`, with each value from `0` to `255`.

`hit_sound` supports raylib audio formats including `.wav`. Relative paths resolve from the executable directory, so `assets/marker.wav` points to `dist/assets/marker.wav`. Set `hit_sound` to an empty string to disable it.

`skybox` is a Quake-style skybox prefix. `"assets/space1"` loads `space1ft.tga`, `space1rt.tga`, `space1bk.tga`, `space1lf.tga`, `space1up.tga`, and `space1dn.tga` from `dist/assets`. Set it to an empty string to disable the skybox.

`./build-lin.sh` copies the default config to `dist/q2aimtrainer.json` only if it does not already exist, so your local tuning is preserved across rebuilds.

## Linux Desktop Entry

Use `q2aimtrainer.desktop.example` as a template. Replace `/ABSOLUTE/PATH/TO/QUAKE2` with the directory containing the `q2aimtrainer` binary and `assets/`, then copy it to:

```text
~/.local/share/applications/q2aimtrainer.desktop
```

## Build

```sh
./build-lin.sh
```

Or manually:

```sh
cmake -S . -B build
cmake --build build --config Release
```
