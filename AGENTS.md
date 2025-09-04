# Repository Guidelines

## Project Structure & Module Organization
- Core firmware: `wled00/` (Arduino/C++). Web UI assets live in `wled00/data/`.
- User extensions: `usermods/` (optional modules you can enable at build time).
- Headers and libs: `include/`, `lib/`.
- Build scripts and tooling: `pio-scripts/`, `tools/` (e.g., `tools/cdata.js`).
- Config: `platformio.ini` and local overrides in `platformio_override.ini` (see `platformio_override.sample.ini`).
- Other: `boards/`, `images/`, `build_output/`, tests in `test/`.

## Build, Test, and Development Commands
- Build firmware: `pio run -e esp32dev` (choose an env from `platformio.ini`).
- DepartStrip usermod: always build after changes with `pio run -e departstrip_debug` from the top-level WLED directory.
- Flash device: `pio run -e esp32dev -t upload` (connect board via USB).
- Serial monitor: `pio device monitor -b 115200`.
- Build web assets: `npm ci && npm run build` (compiles/minifies `wled00/data/`). For live iteration: `npm run dev`.
- Node tests (tooling): `npm test`.
- PlatformIO tests (on target): `pio test -e esp32dev` (tests in `test/`).

## Coding Style & Naming Conventions
- Indentation: tabs for web files (`.html/.css/.js`); 2 spaces for C/C++ and scripts.
- Braces/spacing: space before conditions, no space between function name and `(`; consistent, readable blocks.
- Naming: UPPER_SNAKE_CASE for macros; camelCase for functions/methods; descriptive local variables.
- When unsure, match nearby code. See `CONTRIBUTING.md` for examples.

## Testing Guidelines
- Firmware: place PlatformIO tests under `test/` and run with `pio test -e <env>` on hardware.
- Tooling/UI: use Node’s built‑in runner via `npm test`.
- Add tests for new modules and regressions; keep them fast and self‑contained.

## Commit & Pull Request Guidelines
- Target branch: `main`. Keep commits focused and in imperative mood (e.g., "Fix overflow in JSON parser").
- PRs must include: clear description, linked issues, and screenshots/GIFs for UI changes.
- Avoid force‑push during review; address feedback with follow‑up commits.
- Ensure builds pass; update docs/comments when behavior changes.

## Security & Configuration Tips
- Do not commit secrets. Use `platformio_override.ini` and `wled00/my_config.h` (generated from `my_config_sample.h`) for local settings.
- Prefer enabling features via `platformio_override.ini` or `-D` flags; document usermod switches in `usermods/*/README.md`.
