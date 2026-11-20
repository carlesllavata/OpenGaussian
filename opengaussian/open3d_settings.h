// SPDX-License-Identifier: GPL-3.0-or-later
//
// What every OPEN3D app shares: the language, one preference in settings.json (D-050 to D-058), and the
// folders each app keeps its own files in (D-042 to D-049).
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
//
// An app keeps its settings in %APPDATA%\Open3D\<app> (app_settings_dir) and everything else it writes in
// %LOCALAPPDATA%\Open3D\<app> (app_data_dir), or in <app> inside the folders that OPEN3D_SETTINGS_DIR and
// OPEN3D_DATA_DIR name. At start, before it reads its settings, it copies the folders it used before into
// the new ones with carry_over: once, whole, and leaving the old ones as they were.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
// The environment variable that, when set, names the folder in place of %LOCALAPPDATA%\Open3D.
inline constexpr const char* kDataDirVariable = "OPEN3D_DATA_DIR";
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

// The folder of the other files of every app (caches, logs, recovery files, databases): the one
// OPEN3D_DATA_DIR names, otherwise %LOCALAPPDATA%\Open3D. Nothing when neither variable is set.
std::optional<std::filesystem::path> data_dir();
// data_dir from the values of OPEN3D_DATA_DIR and LOCALAPPDATA. An empty value counts as not set.
std::optional<std::filesystem::path> data_dir_from(const std::optional<std::filesystem::path>& variable,
                                                   const std::optional<std::filesystem::path>& localappdata);

// The folder of an app's own settings (D-042): `app` in settings_dir, such as
// %APPDATA%\Open3D\OpenGaussian. Nothing when there is no settings_dir.
std::optional<std::filesystem::path> app_settings_dir(const std::filesystem::path& app);
// The folder of everything else an app writes (D-044): `app` in data_dir, such as
// %LOCALAPPDATA%\Open3D\OpenGaussian. Nothing when there is no data_dir.
std::optional<std::filesystem::path> app_data_dir(const std::filesystem::path& app);

// What carry_over did.
struct CarryOver
{
    enum class Kind
    {
        // Nothing to do: the new folder is there already, or there is no old folder.
        Nothing,
        // The old folder was copied into the new one.
        Copied,
        // Nothing was put in place and the new folder is still missing, so the next start tries again.
        Failed,
    };
    Kind kind = Kind::Nothing;
    // When it copied: how many files, and what was left out (a file that could not be read, such as one
    // in use, or a link), each with its path inside the old folder and why.
    std::size_t files = 0;
    std::vector<std::pair<std::filesystem::path, std::string>> left_out;
    // When it failed: why.
    std::string reason;
};

// Copies an app's old folder into its new one, the first time it starts with the new folders (D-048,
// D-049). Only when nothing has the name `new_dir` and `old_dir` is a folder: everything in `old_dir` is
// copied into a new folder beside `new_dir`, which takes the name `new_dir` once the copy is complete, so
// a copy cut half way is never taken for a finished one. An existing `new_dir` is never touched, and
// `old_dir` is never changed, moved or deleted. Links inside `old_dir` are not followed. A file that
// cannot be read is left out and said; the rest is still copied. Call it at start, before the app reads
// its settings. It never throws for a folder or a file.
CarryOver carry_over(const std::filesystem::path& old_dir, const std::filesystem::path& new_dir);

} // namespace open3d_settings
