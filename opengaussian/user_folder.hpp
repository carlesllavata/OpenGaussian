// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: the folder OpenGaussian keeps everything in (D-044, its exception for OpenGaussian):
// %LOCALAPPDATA%\Open3D\OpenGaussian, app_data_dir("OpenGaussian") of open3d_settings.h, in place of upstream's
// %USERPROFILE%\.opengaussian. Only the starting point moves: config, data, logs, plugins and the rest stay
// inside it as upstream lays them out. Upstream builds that starting point in a few places (UserPaths::resolve,
// the logger, the session breadcrumb, the model cache of preprocessing); each passes its old folder to
// userFolder. LFS_HOME still wins, and a folder it names is not copied.
//
// The first time, the old folder is copied into the new one, and left as it was (D-048, D-049), without what
// is made again when needed (D-049, Details of 2026-11-20): carryOverSkipping, kSkipped. Built into lfs_logger,
// the lowest library that needs it; tellUserFolder is in the visualizer (user_folder_notice.cpp).

#pragma once

#include "core/export.hpp"
#include "open3d_settings.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace opengaussian {

    // The app's name, as the launcher shows it: its folder inside the Open3D one.
    inline constexpr const char* kAppName = "OpenGaussian";

    // What is not copied from the old folder, by its path inside it ('*' stands for any one name): the Python
    // environment, each plugin's own one and the caches. They are made again when needed, and a copied Python
    // environment would stop working. `onnx` holds the downloaded model weights, which preprocessing checks and
    // downloads again when they are missing.
    inline constexpr std::array<std::string_view, 4> kSkipped = {"venv", "plugins/*/.venv", "cache", "onnx"};

    // carry_over (open3d_settings.h) without the paths `skipped` names, in kSkipped's form: it copies, puts the
    // copy in place, tells what happened and never fails the program the same way. What is skipped is neither
    // copied nor named among what was left out.
    [[nodiscard]] LFS_LOGGER_API open3d_settings::CarryOver
    carryOverSkipping(const std::filesystem::path& old_dir, const std::filesystem::path& new_dir,
                      std::span<const std::string_view> skipped = kSkipped);

    // What carrying the old folder over came to, and the folder it leaves the run with.
    struct CarriedOver {
        std::filesystem::path old_dir;
        // The Open3D folder; empty when there is none (neither OPEN3D_DATA_DIR nor LOCALAPPDATA).
        std::filesystem::path new_dir;
        open3d_settings::CarryOver result;
        // new_dir, or old_dir when the copy failed or there is no new_dir.
        std::filesystem::path folder;
    };

    // Carries `old_dir` over into `open3d` and says which folder to use: `open3d`, or `old_dir` when the copy
    // failed (so that this run does not make `open3d` and the next start tries again) or there is no `open3d`.
    [[nodiscard]] LFS_LOGGER_API CarriedOver carryUserFolder(const std::filesystem::path& old_dir,
                                                             const std::optional<std::filesystem::path>& open3d);

    // The folder OpenGaussian keeps everything in, in place of upstream's `old_dir` (%USERPROFILE%\.opengaussian):
    // the one LFS_HOME names, as UserPaths::resolve takes it; otherwise the first call carries `old_dir` over
    // into app_data_dir("OpenGaussian") (carryUserFolder) and its choice holds for the whole run. Every caller
    // passes the same old folder.
    [[nodiscard]] LFS_LOGGER_API std::filesystem::path userFolder(const std::filesystem::path& old_dir);

    // Whether `root` is the folder userFolder chose without LFS_HOME, upstream's default one.
    [[nodiscard]] LFS_LOGGER_API bool isUserFolder(const std::filesystem::path& root);

    // What userFolder's carrying over came to in this run; Nothing when it has not chosen.
    [[nodiscard]] LFS_LOGGER_API CarriedOver carriedOver();

    // Writes to the log what carrying the old folder over came to, and says in a toast what went wrong: the
    // folder that could not be copied, or the files left out. Called once the interface can show it.
    void tellUserFolder();

} // namespace opengaussian
