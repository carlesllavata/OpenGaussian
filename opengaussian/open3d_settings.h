// SPDX-License-Identifier: GPL-3.0-or-later
//
// The language shared by every OPEN3D app: one preference, in settings.json (D-050 to D-058).
//
// This file is a copy. Its original is common/settings/open3d_settings.h in the OPEN3D root, which also
// holds the contract it keeps (common/settings.md): change it there, never here. Plain C++17 with the
// Windows API; open3d_settings.cpp goes with it.
//
// The file is %APPDATA%\Open3D\settings.json, or settings.json in the folder that OPEN3D_SETTINGS_DIR
// names. It is a JSON object whose "language" is "en" or "es". Reading never fails and never writes: no
// file, no key, a file that cannot be read and any other value all mean "no preference yet", and then
// the language of Windows decides (Spanish if it is Spanish, otherwise English). Writing keeps every
// other key, and happens only when a person changes the language.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace open3d_settings
{

// A language every OPEN3D app can be shown in.
enum class Language
{
    English,
    Spanish,
};

// The environment variable that, when set, names the folder in place of %APPDATA%\Open3D.
inline constexpr const char* kDirVariable = "OPEN3D_SETTINGS_DIR";
// The file's name in that folder.
inline constexpr const char* kFileName = "settings.json";

// The code the file holds: "en" or "es".
std::string_view code(Language language);
// The language of a code; nothing for anything but "en" and "es".
std::optional<Language> from_code(std::string_view code);

// The folder of the shared settings: the one OPEN3D_SETTINGS_DIR names, otherwise %APPDATA%\Open3D.
// Nothing when neither variable is set.
std::optional<std::filesystem::path> settings_dir();
// settings_dir from the values of OPEN3D_SETTINGS_DIR and APPDATA. An empty value counts as not set.
std::optional<std::filesystem::path> settings_dir_from(const std::optional<std::filesystem::path>& variable,
                                                       const std::optional<std::filesystem::path>& appdata);

// The language to show: the preference in `dir`, or the language of Windows when there is none.
Language language(const std::optional<std::filesystem::path>& dir);
// The preference kept in `dir`; nothing when there is none yet. It never fails and never writes.
std::optional<Language> read_language(const std::filesystem::path& dir);
// The preference in the bytes of a settings.json.
std::optional<Language> language_in(std::string_view content);

// Keeps `language` in `dir` as the shared preference; nothing takes the preference out, and then the
// language of Windows decides. Every other key of the file stays. The folder is created if it is
// missing, and the file is written whole into a new file beside it, which then takes its place. Call it
// only when a person changes the language. Returns false when it could not, with what failed in `error`.
bool write_language(const std::filesystem::path& dir, std::optional<Language> language, std::string* error = nullptr);
// What a settings.json holds after write_language: every key of `old` (nothing for no file) and the
// language, or no "language" key for nothing. An `old` that is not a JSON object is not kept.
std::string with_language(std::optional<std::string_view> old, std::optional<Language> language);

// The language of Windows (D-052): Spanish when the user's interface language is Spanish, of any
// country; otherwise English.
Language windows_language();
// windows_language for a Windows language identifier (a LANGID): Spanish when its primary language is
// Spanish.
Language language_for_windows(std::uint16_t langid);

} // namespace open3d_settings
