// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: the folder OpenGaussian keeps everything in, and carrying the old one over (see user_folder.hpp).
// carryOverSkipping keeps the rules of carry_over (open3d_settings.cpp), which copies a folder whole: it walks
// itself only the folders that hold something skipped, and hands every other folder to carry_over.

#include "user_folder.hpp"

#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#include <fstream>
#include <iterator>
#endif

namespace opengaussian {

    namespace {

        namespace fs = std::filesystem;
        using open3d_settings::CarryOver;

        // userFolder's choice, made once per run.
        std::once_flag g_choice_once;
        std::atomic<bool> g_chosen{false};
        // Made on first use and never destroyed: code that runs while the program starts or ends may ask for the
        // folder too.
        [[nodiscard]] CarriedOver& carriedState() {
            static CarriedOver* const state = new CarriedOver();
            return *state;
        }

        // A path as UTF-8, for what is said.
        [[nodiscard]] std::string text(const fs::path& path) {
            const auto bytes = path.u8string();
            return std::string(bytes.begin(), bytes.end());
        }

        [[nodiscard]] std::string couldntWrite(const fs::path& path, const std::string& why) {
            return "couldn't write " + text(path) + " (" + why + ")";
        }

        // `path` in the form Windows reads past MAX_PATH (\\?\C:\... or \\?\UNC\server\share\...), which upstream's
        // own files use too (core/native_fs.hpp): the new folder's name is longer than the old one's, and the copy is
        // made in folders with longer names still.
        [[nodiscard]] fs::path extended(const fs::path& path) {
#ifdef _WIN32
            std::error_code failure;
            const std::wstring full = fs::absolute(path, failure).lexically_normal().native();
            if (failure || full.starts_with(L"\\\\?\\"))
                return path;
            if (full.starts_with(L"\\\\"))
                return fs::path(L"\\\\?\\UNC\\" + full.substr(2));
            if (full.size() >= 3 && full[1] == L':' && full[2] == L'\\')
                return fs::path(L"\\\\?\\" + full);
#endif
            return path;
        }

        // `said` with the paths in it as people write them, without the prefix of extended.
        [[nodiscard]] std::string plain(std::string said) {
            for (const std::string_view prefix : {std::string_view("\\\\?\\UNC\\"), std::string_view("\\\\?\\")}) {
                const std::string to = prefix.size() > 4 ? "\\\\" : "";
                for (std::size_t at = said.find(prefix); at != std::string::npos; at = said.find(prefix, at + to.size()))
                    said.replace(at, prefix.size(), to);
            }
            return said;
        }

        // An environment variable as a path; nothing when it is not set or is empty, as lfs::core::environment.
        [[nodiscard]] std::optional<fs::path> environmentPath(const char* const name) {
#ifdef _WIN32
            const std::wstring wide(name, name + std::char_traits<char>::length(name));
            std::wstring value(256, L'\0');
            for (;;) {
                const DWORD length = GetEnvironmentVariableW(wide.c_str(), value.data(), static_cast<DWORD>(value.size()));
                if (length == 0)
                    return std::nullopt;
                if (length < value.size()) {
                    value.resize(length);
                    return fs::path(value);
                }
                value.resize(length);
            }
#else
            const char* const value = std::getenv(name);
            if (value == nullptr || *value == '\0')
                return std::nullopt;
            return fs::path(value);
#endif
        }

        // Whether something has the name `path`: a folder, a file or a link. When that cannot be told, it counts
        // as there, so that it is never touched.
        [[nodiscard]] bool taken(const fs::path& path) {
            std::error_code failure;
            return fs::symlink_status(path, failure).type() != fs::file_type::not_found;
        }

        // Whether `path` is `folder` or inside it, comparing their absolute forms part by part.
        [[nodiscard]] bool inside(const fs::path& path, const fs::path& folder) {
            std::error_code failure;
            const fs::path whole = fs::absolute(path, failure).lexically_normal();
            const fs::path base = fs::absolute(folder, failure).lexically_normal();
            auto part = whole.begin();
            for (auto base_part = base.begin(); base_part != base.end(); ++base_part) {
                if (base_part->empty())
                    continue;
                if (part == whole.end() || *part != *base_part)
                    return false;
                ++part;
            }
            return true;
        }

        // Whether the name `name` is `wanted`, plain ASCII, without telling case apart, as Windows does.
        [[nodiscard]] bool isName(const fs::path& name, const std::string_view wanted) {
            const auto& native = name.native();
            if (native.size() != wanted.size())
                return false;
            for (std::size_t i = 0; i < native.size(); ++i) {
                const auto unit = static_cast<std::make_unsigned_t<fs::path::value_type>>(native[i]);
                const auto expected = static_cast<unsigned char>(wanted[i]);
                const auto lower = [](const unsigned long c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
                if (unit > 127 || lower(unit) != lower(expected))
                    return false;
            }
            return true;
        }

        // How something inside the old folder stands to what is skipped.
        enum class Fit {
            // Nothing in it is skipped: it is copied whole.
            Whole,
            // It is skipped.
            Skipped,
            // Something inside it is skipped: its contents are walked one by one.
            Holds,
        };

        // How `inside`, a path inside the old folder, stands to the paths `skipped` names.
        [[nodiscard]] Fit fitOf(const fs::path& inside, const std::span<const std::string_view> skipped) {
            Fit fit = Fit::Whole;
            for (const std::string_view pattern : skipped) {
                std::size_t start = 0;
                bool same = true;
                for (const fs::path& part : inside) {
                    if (start > pattern.size()) {
                        same = false;
                        break;
                    }
                    const std::size_t slash = pattern.find('/', start);
                    const std::size_t end = slash == std::string_view::npos ? pattern.size() : slash;
                    const std::string_view wanted = pattern.substr(start, end - start);
                    if (wanted != "*" && !isName(part, wanted)) {
                        same = false;
                        break;
                    }
                    start = end + 1;
                }
                if (!same)
                    continue;
                if (start > pattern.size())
                    return Fit::Skipped;
                fit = Fit::Holds;
            }
            return fit;
        }

        // A new, empty folder of its own in `parent`, named after `name`, to copy into: named as carry_over names
        // its own, so that nothing reads it.
        [[nodiscard]] bool newFolder(const fs::path& parent, const fs::path& name, fs::path& folder, std::string& error) {
#ifdef _WIN32
            const std::string stamp = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
#else
            const std::string stamp = "0";
#endif
            for (int attempt = 0; attempt < 100; ++attempt) {
                fs::path candidate = parent / name;
                candidate += ".carry-" + stamp + "-" + std::to_string(attempt) + ".tmp";
                std::error_code failure;
                if (fs::create_directory(candidate, failure)) {
                    folder = candidate;
                    return true;
                }
                if (failure) {
                    error = "couldn't create a folder in " + text(parent) + " (" + failure.message() + ")";
                    return false;
                }
            }
            error = "couldn't create a folder in " + text(parent) + " (every name tried is taken)";
            return false;
        }

        // Gives the finished copy its name, `new_dir`. While nothing else has that name it tries a few times: another
        // program, such as an antivirus, may hold the copy for a moment.
        [[nodiscard]] bool putInPlace(const fs::path& temporary, const fs::path& new_dir, std::string& error) {
            std::string last;
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (taken(new_dir)) {
                    error = text(new_dir) + " appeared while the copy was made";
                    return false;
                }
#ifdef _WIN32
                if (attempt > 0)
                    Sleep(50);
                // Without MOVEFILE_REPLACE_EXISTING: nothing that has the name new_dir is ever replaced.
                if (MoveFileExW(temporary.c_str(), new_dir.c_str(), MOVEFILE_WRITE_THROUGH))
                    return true;
                last = std::system_category().message(static_cast<int>(GetLastError()));
#else
                std::error_code failure;
                fs::rename(temporary, new_dir, failure);
                if (!failure)
                    return true;
                last = failure.message();
#endif
            }
            error = "couldn't rename " + text(temporary) + " to " + text(new_dir) + " (" + last + ")";
            return false;
        }

        // What copying one file came to.
        enum class Step {
            Copied,
            // The file cannot be read: it is left out.
            LeftOut,
            // Something cannot be written: the copy stops.
            Stop,
        };

        // Copies the file `from` into the new file `to`, with the time it was last changed, as carry_over does: LeftOut
        // when `from` cannot be read, with why in `why`, and then `to` is not left behind; Stop when `to` cannot be
        // written. `from` is opened as other programs may still use it.
        [[nodiscard]] Step copyFile(const fs::path& from, const fs::path& to, std::string& why) {
#ifdef _WIN32
            const auto message = [](const DWORD code) { return std::system_category().message(static_cast<int>(code)); };
            const HANDLE source = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (source == INVALID_HANDLE_VALUE) {
                why = message(GetLastError());
                return Step::LeftOut;
            }
            FILETIME changed{};
            const bool has_time = GetFileTime(source, nullptr, nullptr, &changed) != 0;
            const HANDLE target = CreateFileW(to.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (target == INVALID_HANDLE_VALUE) {
                why = couldntWrite(to, message(GetLastError()));
                CloseHandle(source);
                return Step::Stop;
            }
            std::vector<char> buffer(1 << 16);
            Step step = Step::Copied;
            for (;;) {
                DWORD read = 0;
                if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                    why = message(GetLastError());
                    step = Step::LeftOut;
                    break;
                }
                if (read == 0)
                    break;
                DWORD written = 0;
                if (!WriteFile(target, buffer.data(), read, &written, nullptr) || written != read) {
                    why = couldntWrite(to, message(GetLastError()));
                    step = Step::Stop;
                    break;
                }
            }
            if (step == Step::Copied) {
                if (has_time)
                    SetFileTime(target, nullptr, nullptr, &changed);
                if (!FlushFileBuffers(target)) {
                    why = couldntWrite(to, message(GetLastError()));
                    step = Step::Stop;
                }
            }
            CloseHandle(target);
            CloseHandle(source);
            if (step == Step::LeftOut)
                DeleteFileW(to.c_str());
            return step;
#else
            std::ifstream input(from, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (!input && !input.eof()) {
                why = "it cannot be read";
                return Step::LeftOut;
            }
            if (taken(to)) {
                why = couldntWrite(to, "it is there already");
                return Step::Stop;
            }
            std::ofstream output(to, std::ios::binary);
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!output.flush()) {
                why = couldntWrite(to, "the write failed");
                return Step::Stop;
            }
            output.close();
            std::error_code failure;
            fs::last_write_time(to, fs::last_write_time(from, failure), failure);
            return Step::Copied;
#endif
        }

        // What the copy has come to so far: the files copied, and what was left out, by its path inside the old
        // folder, with why.
        struct Copying {
            std::size_t files = 0;
            std::vector<std::pair<fs::path, std::string>> left_out;
        };

        bool copyFolder(const fs::path& from, const fs::path& to, const fs::path& inside_old,
                        std::span<const std::string_view> skipped, Copying& copying, std::string& error);

        // Copies the folder `from`, whose path inside the old folder is `inside_old`, into `to`, which is not there yet.
        // False when the copy must stop, with why in `error`.
        [[nodiscard]] bool copySubfolder(const fs::path& from, const fs::path& to, const fs::path& inside_old, const Fit fit,
                                         const std::span<const std::string_view> skipped, Copying& copying, std::string& error) {
            if (fit == Fit::Holds) {
                std::error_code made;
                if (!fs::create_directory(to, made)) {
                    error = couldntWrite(to, made ? made.message() : std::string("it is there already"));
                    return false;
                }
                return copyFolder(from, to, inside_old, skipped, copying, error);
            }
            // A folder that cannot be read is left out, as carry_over leaves out one inside the folder it copies.
            {
                std::error_code failure;
                const fs::directory_iterator probe(from, failure);
                if (failure) {
                    copying.left_out.emplace_back(inside_old, failure.message());
                    return true;
                }
            }
            const CarryOver done = open3d_settings::carry_over(from, to);
            if (done.kind == CarryOver::Kind::Failed) {
                error = done.reason;
                return false;
            }
            copying.files += done.files;
            for (const auto& [path, why] : done.left_out)
                copying.left_out.emplace_back(inside_old / path, why);
            return true;
        }

        // Copies what the folder `from` holds into the empty folder `to`, without what `skipped` names; `inside_old`
        // is the path of `from` inside the old folder. False when the copy must stop (the old folder itself cannot be
        // read, or something cannot be written), with why in `error`.
        bool copyFolder(const fs::path& from, const fs::path& to, const fs::path& inside_old,
                        const std::span<const std::string_view> skipped, Copying& copying, std::string& error) {
            std::error_code failure;
            fs::directory_iterator entry(from, failure);
            if (failure) {
                if (inside_old.empty()) {
                    error = "couldn't read " + text(from) + " (" + failure.message() + ")";
                    return false;
                }
                copying.left_out.emplace_back(inside_old, failure.message());
                return true;
            }
            for (const fs::directory_iterator end; entry != end;) {
                const fs::path name = entry->path().filename();
                const fs::path path = inside_old / name;
                const fs::path target = to / name;
                const Fit fit = fitOf(path, skipped);
                std::error_code kind_failure;
                const fs::file_type kind = entry->symlink_status(kind_failure).type();
                if (fit == Fit::Skipped) {
                    // Made again when needed: neither copied nor named.
                } else if (kind_failure) {
                    copying.left_out.emplace_back(path, kind_failure.message());
                } else if (kind == fs::file_type::directory) {
                    if (!copySubfolder(entry->path(), target, path, fit, skipped, copying, error))
                        return false;
                } else if (kind == fs::file_type::regular) {
                    std::string why;
                    switch (copyFile(entry->path(), target, why)) {
                    case Step::Copied:
                        ++copying.files;
                        break;
                    case Step::LeftOut:
                        copying.left_out.emplace_back(path, why);
                        break;
                    case Step::Stop:
                        error = why;
                        return false;
                    }
                } else {
                    copying.left_out.emplace_back(path, "not a plain file or folder, so not copied");
                }
                entry.increment(failure);
                if (failure) {
                    copying.left_out.emplace_back(inside_old, failure.message());
                    break;
                }
            }
            return true;
        }

        // carryOverSkipping, for paths in extended's form.
        [[nodiscard]] CarryOver carryOverExtended(const fs::path& old_dir, const fs::path& new_dir, const std::span<const std::string_view> skipped) {
            CarryOver done;
            fs::path temporary;
            try {
                std::error_code failure;
                if (taken(new_dir) || !fs::is_directory(old_dir, failure))
                    return done;
                done.kind = CarryOver::Kind::Failed;
                if (inside(new_dir, old_dir)) {
                    done.reason = text(new_dir) + " is inside " + text(old_dir);
                    return done;
                }
                const fs::path name = new_dir.filename();
                if (name.empty()) {
                    done.reason = text(new_dir) + " cannot be a folder of its own";
                    return done;
                }
                fs::path parent = new_dir.parent_path();
                if (parent.empty())
                    parent = ".";
                fs::create_directories(parent, failure);
                if (failure || !fs::is_directory(parent, failure)) {
                    done.reason = "couldn't create a folder in " + text(parent) + (failure ? " (" + failure.message() + ")" : std::string());
                    return done;
                }
                if (!newFolder(parent, name, temporary, done.reason))
                    return done;
                Copying copying;
                if (copyFolder(old_dir, temporary, fs::path(), skipped, copying, done.reason) &&
                    putInPlace(temporary, new_dir, done.reason)) {
                    done.kind = CarryOver::Kind::Copied;
                    done.files = copying.files;
                    done.left_out = std::move(copying.left_out);
                    done.reason.clear();
                    return done;
                }
            } catch (const std::exception& exception) {
                done.kind = CarryOver::Kind::Failed;
                done.reason = exception.what();
            }
            std::error_code failure;
            if (!temporary.empty())
                fs::remove_all(temporary, failure);
            // Another start of OpenGaussian may have put its own copy in place meanwhile.
            if (taken(new_dir))
                return CarryOver{};
            return done;
        }

    } // namespace

    CarryOver carryOverSkipping(const fs::path& old_dir, const fs::path& new_dir, const std::span<const std::string_view> skipped) {
        CarryOver done = carryOverExtended(extended(old_dir), extended(new_dir), skipped);
        done.reason = plain(std::move(done.reason));
        for (auto& [path, why] : done.left_out)
            why = plain(std::move(why));
        return done;
    }

    CarriedOver carryUserFolder(const fs::path& old_dir, const std::optional<fs::path>& open3d) {
        CarriedOver carried;
        carried.old_dir = old_dir;
        carried.folder = old_dir;
        if (!open3d)
            return carried;
        carried.new_dir = *open3d;
        carried.result = carryOverSkipping(old_dir, *open3d);
        if (carried.result.kind != CarryOver::Kind::Failed)
            carried.folder = *open3d;
        return carried;
    }

    fs::path userFolder(const fs::path& old_dir) {
        if (const auto home = environmentPath("LFS_HOME"))
            return *home;
        CarriedOver& carried = carriedState();
        std::call_once(g_choice_once, [&old_dir, &carried] {
            try {
                carried = carryUserFolder(old_dir, open3d_settings::app_data_dir(kAppName));
            } catch (const std::exception& exception) {
                carried = CarriedOver{.old_dir = old_dir, .new_dir = {}, .result = {}, .folder = old_dir};
                carried.result.kind = CarryOver::Kind::Failed;
                carried.result.reason = exception.what();
            }
            // Before the log is open: the interface says it again (tellUserFolder).
            if (carried.result.kind == CarryOver::Kind::Failed)
                std::fprintf(stderr, "OpenGaussian: %s could not be copied into %s, so it is used this time: %s\n",
                             text(carried.old_dir).c_str(), text(carried.new_dir).c_str(), carried.result.reason.c_str());
            g_chosen.store(true, std::memory_order_release);
        });
        return carried.folder;
    }

    bool isUserFolder(const fs::path& root) {
        return g_chosen.load(std::memory_order_acquire) && root == carriedState().folder;
    }

    CarriedOver carriedOver() {
        if (!g_chosen.load(std::memory_order_acquire))
            return {};
        return carriedState();
    }

} // namespace opengaussian
