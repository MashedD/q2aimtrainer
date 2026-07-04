# q2aimtrainer

Stationary 3D Quake II-style aim trainer. Built with the same core stack as `q2manager`: C++23, CMake, and raylib.

## Features

- Fullscreen 3D stationary aim trainer
- Center-screen raycast shooting
- Multiple random 3D bubbles active at once
- Quake-style mouse constants: `m_yaw 0.022`, `m_pitch 0.022`
- Default `fov 90`
- JSON config beside the executable for FOV and mouse tuning
- HUD with hits, misses, accuracy, time, and hits/min

## Controls

- Mouse: aim
- Left click: shoot
- `R`: reset session
- `-` / `=`: decrease/increase sensitivity
- `F11`: toggle fullscreen
- `Esc`: quit

## Config

Edit `q2aimtrainer.json` beside the executable, then restart the trainer:

```json
{
  "fov": 90.0,
  "sensitivity": 3.0,
  "m_yaw": 0.022,
  "m_pitch": 0.022
}
```

`./build-lin.sh` copies the default config to `dist/q2aimtrainer.json` only if it does not already exist, so your local tuning is preserved across rebuilds.

## Build

```sh
./build-lin.sh
```

Or manually:

```sh
cmake -S . -B build
cmake --build build --config Release
```
