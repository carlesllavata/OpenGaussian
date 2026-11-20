// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: how OpenGaussian takes the language every Open3D app shares (see shared_language.hpp).

#include "shared_language.hpp"

#include "open3d_settings.h"

#include "core/error_bus.hpp"
#include "core/event_bridge/localization_manager.hpp"
#include "core/logger.hpp"
#include "preferences.hpp"

#include <optional>

namespace opengaussian {

    namespace {

        // The text of the toast, in opengaussian/locales/<code>.json.
        constexpr const char* kNotSharedKey = "opengaussian.language_not_shared";

        // The only language carried over. Upstream saves English when its start screen is closed without a
        // choice and when its preferences are reset, so a saved English cannot be told apart from its default.
        constexpr const char* kCarried = "es";

        [[nodiscard]] std::string codeOf(const open3d_settings::Language language) {
            return std::string(open3d_settings::code(language));
        }

        // Says in a toast that the shared file could not be written. The program goes on.
        void tellNotShared(const std::string& why) {
            LOG_WARN("The language could not be saved for the other Open3D apps: {}", why);
            lfs::ErrorBus::instance().publish(lfs::ErrorNotification{
                .error = lfs::make_error(lfs::ErrorInit{
                    .code = lfs::ErrorCode::Unavailable,
                    .domain = lfs::ErrorDomain::Core,
                    .severity = lfs::Severity::Warning,
                    .user_message = LOCF(kNotSharedKey, why),
                    .detail = why,
                    .detection = LFS_SOURCE_SITE_CURRENT(),
                }),
                .surface = lfs::ErrorSurface::Toast,
                .actions = {},
                .operation_id = lfs::OperationId::generate(),
            });
        }

        // Writes the shared preference; what failed goes to `error`.
        [[nodiscard]] bool write(const open3d_settings::Language language, std::string& error) {
            const auto dir = open3d_settings::settings_dir();
            if (!dir) {
                error = "neither OPEN3D_SETTINGS_DIR nor APPDATA is set";
                return false;
            }
            return open3d_settings::write_language(*dir, language, &error);
        }

        // Whether a Spanish saved in OpenGaussian before is still to be carried over.
        [[nodiscard]] bool toCarry(lfs::vis::UserPreferences& preferences) {
            return preferences.language() == kCarried && !preferences.languageShared();
        }

        // From now on the shared file is the only source: a Spanish saved in OpenGaussian before is never
        // carried over again, so a preference taken out later (Auto in the launcher) is not put back.
        void rememberShared(lfs::vis::UserPreferences& preferences) {
            if (!preferences.languageShared())
                preferences.setLanguageShared();
        }

    } // namespace

    std::string sharedLanguage() {
        const auto dir = open3d_settings::settings_dir();
        const auto language = dir ? open3d_settings::read_language(*dir) : std::nullopt;
        return language ? codeOf(*language) : std::string{};
    }

    std::string takeStartLanguage() {
        auto& preferences = lfs::vis::UserPreferences::instance();
        if (const std::string shared = sharedLanguage(); !shared.empty()) {
            rememberShared(preferences);
            LOG_INFO("Language {}: the one every Open3D app shares", shared);
            return shared;
        }
        if (toCarry(preferences)) {
            std::string error;
            if (write(open3d_settings::Language::Spanish, error)) {
                preferences.setLanguageShared();
                LOG_INFO("Language es: chosen in OpenGaussian before, now shared by every Open3D app");
            } else {
                // The toast in the language that stays shown.
                lfs::event::LocalizationManager::getInstance().setLanguage(kCarried);
                tellNotShared(error);
            }
            return kCarried;
        }
        const std::string windows = codeOf(open3d_settings::windows_language());
        LOG_INFO("Language {}: the language of Windows, as no Open3D app has a preference yet", windows);
        return windows;
    }

    void shareLanguage(const std::string& code) {
        const auto language = open3d_settings::from_code(code);
        if (!language)
            return;
        std::string error;
        if (!write(*language, error)) {
            tellNotShared(error);
            return;
        }
        rememberShared(lfs::vis::UserPreferences::instance());
    }

} // namespace opengaussian
