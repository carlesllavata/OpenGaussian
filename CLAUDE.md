# OpenGaussian

LichtFeld Studio under another name: it trains, views, edits and exports 3D Gaussian Splatting scenes.
A fork in C++ and CUDA, built and packaged for Windows; almost every file is upstream's.

This file holds facts about this repo only, each one checkable in the code. Update it in the same commit
as a change that makes it old. It is our file: the root `README.md`, `AGENTS.md`, `CONTRIBUTING.md` and
`.mcp.json` are upstream's and still talk about LichtFeld.

## Layout

- `opengaussian/`: everything of ours. `CMakeLists.txt` includes `opengaussian/OpenGaussian.cmake`: the
  product's constants and, once upstream's targets exist, the exe's name, the icon, nvJPEG and the branding.
- `opengaussian/ApplyBranding.cmake`: our logos over upstream's file names, and only the languages of
  `OG_LOCALES`, with the product's name replaced and the overrides of `opengaussian/locales/`.
- `opengaussian/package.ps1`: the packager. `src/python/lfs_plugins/opengaussian.py`: our switches.
- `.github/README.md` (the one GitHub shows) and `CHANGELOG.md` are ours. The rest is upstream's, with
  small edits marked "OpenGaussian": `git grep -n OpenGaussian -- CMakeLists.txt cmake src` lists them.

## Build and check

- Build (tools and their versions in `.github/README.md`; `VCPKG_ROOT` and `VCPKG_BUILD_TYPE=release` set):
  `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_PORTABLE=ON -DBUILD_PYTHON_STUBS=OFF`
  `-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`, then `cmake --build build`.
- There is no check command of our own, and no tests are built: upstream's are in `tests/` and need
  `-DBUILD_TESTS=ON`, which `BUILD_PORTABLE` forces off. The only checks are the packager's.
- `.github/workflows` is upstream's: every job that starts by itself is limited to upstream's repository.

## Version and release

- The version is `OG_VERSION` in `opengaussian/OpenGaussian.cmake`. It is written again by hand in
  `.github/README.md` and `CHANGELOG.md`; nothing checks that they agree.
- `CHANGELOG.md` has a section per version, headed `## X.Y.Z – YYYY-MM-DD` (not the form of the other
  Open3D repos), and no `## In development` section.
- `powershell -ExecutionPolicy Bypass -File opengaussian\package.ps1 -BuildDir build -OutDir dist` refuses
  uncommitted changes and an exe that is not `OG_VERSION`, runs `cmake --install` into a staging folder,
  checks it (exe version, nvJPEG, licences, only the `en` and `es` languages) and writes
  `opengaussian-<version>-windows-x64.zip` and `SHA256SUMS.txt`.
- The exe stays in `bin\` inside the zip, not at its root: it finds its resources, Python and extensions
  in the folders next to `bin` (`src/core/include/core/executable_path.hpp`).

## Rules of this code

- Upstream's files, copyright lines and licence are kept (GPL-3.0-or-later with `LICENSE-EXCEPTION.txt`).
- Upstream's names and version numbers are never changed: the `lfs_` prefixes, the `lichtfeld` Python
  module, the plugin API, the CMake project and its version, `share\LichtFeld-Studio` in the package.
- The code keeps upstream's style and is never reformatted. The settings files the other Open3D repos
  share (`.editorconfig`, the common `.gitattributes`, `ruff.toml`) are left out on purpose; upstream's
  `.gitignore` and `.gitattributes` are not edited (this file is tracked although `.gitignore` lists it).
- Accounts, the online Gallery and the portal bug report are off through `PORTAL_ENABLED = False` (our
  switches); their code stays in the tree, and their interface parts are hidden in the `.rml` files.

## Traps

- Upstream's `.gitignore` has `*.png`, so `opengaussian/logo.png` (shown by our README) and the logos in
  `opengaussian/assets/` are not in Git; a clone without them builds with upstream's logos and no error.
- Upstream's tags are kept as `upstream/<tag>` (refspec `+refs/tags/*:refs/tags/upstream/*`, and
  `tagopt = --no-tags` on both remotes); `cmake/GenerateGitVersion.cmake` reads upstream's version from
  `upstream/v*`. `git push --tags` would publish them: our remote holds only our own `vX.Y.Z` tags.
- Upstream writes its user folder (`.lichtfeld`) and window titles as literals, replaced here one by one
  (`OG_USER_DIR_NAME`, `".opengaussian"`): a merge can bring new ones; `src/python/runner.cpp` has one left.
- Upstream's `build_lichtfeld.ps1` clones or updates vcpkg in the folder above the repo.
