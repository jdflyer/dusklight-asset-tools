# Dusklight Mod Template

A standalone starting point for [Dusklight](https://github.com/TwilitRealm/dusklight) mods.
Fork it, rename a few things, and you have a working mod with CI that builds native libraries
for every supported platform and combines them into a single distributable `.dusk` bundle.

See the [Dusklight modding documentation](https://github.com/TwilitRealm/dusklight/blob/main/docs/modding.md)
for the full mod API: services, hooking game functions, asset overlays, and more.

## Quick start

1. **Fork or copy this repository.**
2. Edit `mod.json`: set your mod's `id` (reverse-DNS style, e.g. `com.example.my_mod`),
   `name`, `author`, and `description`.
3. Rename the target in `CMakeLists.txt` (`add_mod(my_mod ...)`) — this names the `.dusk` file.
4. Write your mod in `src/mod.cpp`.
5. Build:

   ```sh
   cmake -B build
   cmake --build build
   ```

   The first configure downloads the Dusklight sources (a few hundred MB) into `dusklight/`
   and, on platforms that need one, a small link stub. Both are cached afterward.

The result is `build/mods/<name>.dusk`. Copy it into the game's user mods folder to try it:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

During development, rebuild and click **Reload** in the in-game mod manager to pick up changes.

## Updating to a new Dusklight version

Change the `DUSKLIGHT_VERSION` line in `CMakeLists.txt` to the new release tag (or commit SHA)
and reconfigure. The pinned version is fetched into `dusklight/` automatically.

The `dusklight/` directory is a real checkout of the game's source code — use it to browse
game headers, find functions to hook, and read the mod SDK itself.

## Features

`add_mod`'s `FEATURES` argument declares what your mod touches:

- `game`: call into and hook game code. Requires linking against the game's export surface:
  on Windows, macOS, Android, and iOS a small version-independent link stub is downloaded
  automatically (Linux needs nothing). Mods that only use services can remove this for wider
  compatibility across Dusklight versions.
- `webgpu`: use the WebGPU API (`webgpu/webgpu.h`); required for `GfxService`.

Optional content directories, all packaged automatically when passed to `add_mod`:

- `res/`: resources bundled with your mod (`RES_DIR`), loaded via `ResourceService`.
  `res/icon.png` and `res/banner.png` are picked up as mod-manager artwork.
- `overlay/`: game file overrides (`OVERLAY_DIR`).
- `textures/`: texture replacements (`TEXTURES_DIR`).

## Cross-platform bundle

The included GitHub Actions workflow builds the mod for
`linux-x86_64`, `linux-aarch64`, `macos-arm64`, `macos-x86_64`, `windows-amd64`,
`windows-arm64`, and `android-aarch64`, then merges the per-platform builds into a single
`.dusk` containing all native libraries (`tools/merge_mod.py`).

Pushing a tag attaches the combined bundle to a GitHub release.

## For Dusklight developers

Point the build at an existing checkout instead of fetching one:

```sh
cmake -B build -DDUSKLIGHT_DIR=~/path/to/dusklight
```

`DUSK_GAME_EXE` can likewise be set to a locally built game binary (macOS) or import library
(Windows) instead of the downloaded stub.
