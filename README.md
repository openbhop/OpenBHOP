# OpenBHOP

OpenBHOP is an open source game engine which faithfully re-creates bunnyhopping and surf physics from Counter-Strike: Source.  The goal is to be exactly the same, so that a map completion in OpenBHOP is identical to a map completion in Counter-Strike: Source.

## Why?

* We're no longer confined to technology that was developed 20 years ago, and deprecated 10 years ago.
* Being open source means we, the movement community, own it.  We can fix it, publish it, and make it better.
* We're no longer a by-product of another game, every update is tailored for the movement mechanics that we know and love.
* We're hyper-focused on bunnyhop.  Surf, RJ, trikz, and other gamemodes are easily doable, but the primary focus here is bunnyhop and we don't want to get distracted.

## Movement TODO

- [x]  BBox tracing
- [x]  Walk
- [x]  Duck
- [x]  Bunnyhop
- [x]  Surf
- [x]  Acceleration
- [x]  Friction
- [ ]  Water movement
- [ ]  Ladder movement
- [x]  Noclip movement
- [x]  Slope boosting
- [x]  Slope & step walking
- [x]  Stamina (optional)
- [ ]  Push, teleport, gravity triggers

## Platform TODO

- [x]  Windows
- [x]  Web
- [ ]  Linux
- [ ]  Android
- [ ]  Mac

## Input TODO

- [x] Keyboard/mouse
- [ ] Controller
- [ ] Tablet and phone

## Movement authenticity

When all movement mechanics are complete, we'll do a side-by-side comparison to CS:S.  Run the same inputs and compare the outputs, to prove that movement in OpenBHOP is authentic for speedrunning.

## BSP maps

BSP support is in early stages.  OpenBHOP has a proprietary map format (.cmap) for early development & testing, it's very similar to BSP and sets the foundation for a seamless transition.

## Technical overview

- **SDL3** for input, windowing, and GPU support.
- We use a custom build of RmlUi for user interface, [located here](https://github.com/openbhop/Interface).
- Physics uses brush/plane geometric data allowing for clean bbox tracing.  This method was originally found in Quake, then adopted by GoldSrc, then carried over into Source Engine, and is key to authentic movement physics.
- **Lightweight:** minimal dependencies and small build size.  As of writing this, a complete OpenBHOP build is less than 5mb.
- **Portable:** No testing has been done outside of Windows, but supporting other platforms should be trivial.  Cross-platform will become a focus later on, with contributions for it welcome any time.

## Build Instructions

### Prerequisites

* **CMake** (3.20+)
* **C++ Compiler** (supporting C++17)
* **DXC (DirectX Shader Compiler)**: Must be installed and available in your system `PATH`.

### Desktop build Steps

```
# 1. Clone recursively
git clone --recursive https://github.com/openbhop/OpenBHOP.git
cd OpenBHOP

# 2. Generate project files
cmake -B build

# 3. Build the project
cmake --build build --config Release

# 4. Double click build/release/bh.exe
```

### Web build steps, requires an emsdk-enabled shell

```
# 1. Clone recursively
git clone --recursive https://github.com/openbhop/OpenBHOP.git
cd OpenBHOP

# 2. Generate project files
emcmake cmake -S . -B build-web -G Ninja

# 3. Build the project
cmake --build build-web

# 4. To run
cd build-web
python3 -m http.server 8000

# visit http://localhost:8000 in a browser
```
## Contributing

OpenBHOP is a community project with a shared passion for bunnyhop, we welcome contributions from anybody.

- AI may be used for boilerplate, but logic is audited by humans.
