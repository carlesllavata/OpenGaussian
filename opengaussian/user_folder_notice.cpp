// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: what carrying the old folder over came to, said once the interface can show it (see
// user_folder.hpp). Built into the visualizer, whose start (gui_manager.cpp) calls it.

#include "user_folder.hpp"

#include "core/error_bus.hpp"
#include "core/event_bridge/localization_manager.hpp"
#include "core/logger.hpp"

#include <string>

namespace opengaussian {

    namespace {

        // The texts of the toasts, in opengaussian/locales/<code>.json.
        constexpr const char* kFolderNotCopiedKey = "opengaussian.folder_not_copied";
        constexpr const char* kFilesNotCopiedKey = "opengaussian.files_not_copied";

        // How many of the files left out a toast names; the log names them all.
        constexpr std::size_t kNamed = 3;

        [[nodiscard]] std::string text(const std::filesystem::path& path) {
            const auto bytes = path.u8string();
            return std::string(bytes.begin(), bytes.end());
        }

        // Says `message` in a toast. The program goes on.
        void toast(const std::string& message, const std::string& detail) {
            lfs::ErrorBus::instance().publish(lfs::ErrorNotification{
                .error = lfs::make_error(lfs::ErrorInit{
                    .code = lfs::ErrorCode::Unavailable,
                    .domain = lfs::ErrorDomain::Core,
                    .severity = lfs::Severity::Warning,
                    .user_message = message,
                    .detail = detail,
                    .detection = LFS_SOURCE_SITE_CURRENT(),
                }),
                .surface = lfs::ErrorSurface::Toast,
                .actions = {},
                .operation_id = lfs::OperationId::generate(),
            });
        }

    } // namespace

    void tellUserFolder() {
        const CarriedOver carried = carriedOver();
        const open3d_settings::CarryOver& result = carried.result;
        const std::string old_dir = text(carried.old_dir);
        const std::string new_dir = text(carried.new_dir);
        if (result.kind == open3d_settings::CarryOver::Kind::Nothing)
            return;
        if (result.kind == open3d_settings::CarryOver::Kind::Failed) {
            LOG_WARN("{} could not be copied into {}, so it is used this time and the next start tries again: {}",
                     old_dir, new_dir, result.reason);
            toast(LOCF(kFolderNotCopiedKey, old_dir, new_dir, result.reason), result.reason);
            return;
        }
        LOG_INFO("Copied {} files of {} into {}, OpenGaussian's folder from now on; the old one is left as it was, and "
                 "the Python environments and the caches are made again when needed",
                 result.files, old_dir, new_dir);
        if (result.left_out.empty())
            return;
        std::string named;
        for (std::size_t i = 0; i < result.left_out.size(); ++i) {
            const auto& [path, why] = result.left_out[i];
            const std::string file = text(carried.old_dir / path);
            LOG_WARN("Not copied into {}: {} ({})", new_dir, file, why);
            if (i < kNamed)
                named += (i == 0 ? "" : ", ") + file;
        }
        if (result.left_out.size() > kNamed)
            named += ", ...";
        const std::size_t count = result.left_out.size();
        toast(LOCF(kFilesNotCopiedKey, count, new_dir, named), named);
    }

} // namespace opengaussian
