// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: tests of user_folder.cpp on its own, with fake old folders and OPEN3D_DATA_DIR and
// OPEN3D_SETTINGS_DIR in a temporary folder; it never reads, copies or writes the folders of this computer.
// Nothing builds it with the program. From the repo, in an x64 Native Tools Command Prompt:
//
//   cl /nologo /std:c++20 /EHsc /W4 /permissive- /utf-8 /DLFS_STATIC_BUILD /Isrc\core\include
//      opengaussian\test_user_folder.cpp opengaussian\user_folder.cpp opengaussian\open3d_settings.cpp
//      /Fo:<build folder>\ /Fe:<build folder>\test_user_folder.exe
//   <build folder>\test_user_folder.exe <a folder that does not exist yet>
//
// It prints each test and ends with 0 when all of them pass.

#include "user_folder.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;
using open3d_settings::CarryOver;
using Kind = CarryOver::Kind;

namespace {

    int g_failures = 0;

    // Says what a carry over came to, when a test does not expect it.
    void show(const CarryOver& done) {
        std::printf("  (kind %d, %zu files, %zu left out, reason: %s)\n", static_cast<int>(done.kind), done.files,
                    done.left_out.size(), done.reason.c_str());
        for (const auto& [path, why] : done.left_out)
            std::printf("    left out %s: %s\n", path.generic_string().c_str(), why.c_str());
    }

#define CHECK(condition)                                                 \
    do {                                                                 \
        if (!(condition)) {                                              \
            std::printf("  FAILED line %d: %s\n", __LINE__, #condition); \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

    fs::path g_root;

    // `path` in the form Windows reads past MAX_PATH, for the test's own reads and writes: the code tested is
    // given plain paths.
    fs::path longForm(const fs::path& path) {
        const std::wstring full = fs::absolute(path).lexically_normal().native();
        return full.starts_with(L"\\\\") ? path : fs::path(L"\\\\?\\" + full);
    }

    // A new, empty folder of the test's own.
    fs::path scratch(const char* name) {
        const fs::path dir = g_root / name;
        fs::create_directories(dir);
        return dir;
    }

    void write(const fs::path& path, const std::string& bytes) {
        fs::create_directories(longForm(path.parent_path()));
        std::ofstream(longForm(path), std::ios::binary) << bytes;
    }

    std::string read(const fs::path& path) {
        std::ifstream input(longForm(path), std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void setVariable(const wchar_t* name, const fs::path& value) {
        SetEnvironmentVariableW(name, value.empty() ? nullptr : value.c_str());
    }

    // Every folder and file inside `dir`, by its path inside it: a file's bytes and time, "<folder>" for a
    // folder, "<other>" for anything else (links are not followed).
    std::map<std::string, std::string> listing(const fs::path& plain_dir) {
        const fs::path dir = longForm(plain_dir);
        std::map<std::string, std::string> found;
        for (auto entry = fs::recursive_directory_iterator(dir); entry != fs::recursive_directory_iterator(); ++entry) {
            const std::string inside = entry->path().lexically_relative(dir).generic_string();
            const auto kind = entry->symlink_status().type();
            if (kind == fs::file_type::directory)
                found[inside] = "<folder>";
            else if (kind == fs::file_type::regular)
                found[inside] = read(entry->path()) + "@" +
                                std::to_string(fs::last_write_time(entry->path()).time_since_epoch().count());
            else {
                found[inside] = "<other>";
                entry.disable_recursion_pending();
            }
        }
        return found;
    }

    // An old folder as OpenGaussian 0.1.0 leaves it, with what is skipped and what is not.
    fs::path fakeOldFolder(const char* name) {
        const fs::path old = scratch(name) / ".opengaussian";
        write(old / "config" / "preferences.json", "{\"language\": \"es\"}\n");
        write(old / "config" / "keymaps" / "default.json", "{}");
        write(old / "data" / "presets" / "p.json", "[1]");
        fs::create_directories(old / "data" / "empty");
        write(old / "data" / "migrations" / "legacy-user-settings-v1.done", "legacy settings migration complete\n");
        write(old / "logs" / "lichtfeld.log", "a log\n");
        write(old / "recovery" / "untitled.licht", "recovery");
        write(old / "projects" / "a" / "project.licht", "project");
        write(old / "tmp" / "scratch.bin", "scratch");
        write(old / "top.txt", "at the top");
        write(old / "plugins" / "readme.txt", "plugins");
        write(old / "plugins" / "foo" / "pyproject.toml", "[tool.lichtfeld]");
        write(old / "plugins" / "foo" / "__init__.py", "");
        write(old / "plugins" / "foo" / "__pycache__" / "x.pyc", "bytecode");
        write(old / "plugins" / "foo" / "src" / "mod.py", "x = 1");
        write(old / "plugins" / "foo" / ".venv" / "pyvenv.cfg", "home = C:\\Python");
        write(old / "plugins" / "Bar" / ".VENV" / "pyvenv.cfg", "home = C:\\Python");
        write(old / "plugins" / "Bar" / "keep.txt", "kept");
        write(old / "venv" / "Scripts" / "python.exe", "python");
        write(old / "cache" / "pipeline_cache.bin", "cache");
        write(old / "onnx" / "moge.onnx", "weights");
        // Paths longer than MAX_PATH, more so in the new folder: inside a folder copied whole and at a level walked.
        write(old / "projects" / std::string(100, 'p') / std::string(100, 'q') / "deep.bin", "deep");
        write(old / "plugins" / "foo" / (std::string(200, 'r') + ".txt"), "long name");
        // A time far from now, to see it kept.
        fs::last_write_time(old / "config" / "preferences.json",
                            fs::last_write_time(old / "config" / "preferences.json") - std::chrono::hours(24 * 365));
        return old;
    }

    // What the copy of fakeOldFolder must hold: everything but what is skipped.
    std::map<std::string, std::string> withoutSkipped(std::map<std::string, std::string> all) {
        for (const char* skipped : {"venv", "cache", "onnx", "plugins/foo/.venv", "plugins/Bar/.VENV"}) {
            const std::string prefix = std::string(skipped) + "/";
            for (auto it = all.begin(); it != all.end();)
                it = it->first == skipped || it->first.starts_with(prefix) ? all.erase(it) : std::next(it);
        }
        return all;
    }

    void theOldFolderIsCopiedWithoutWhatIsMadeAgain() {
        std::puts("the old folder is copied without what is made again, once, and left as it was");
        const fs::path old = fakeOldFolder("copy");
        const fs::path new_dir = scratch("copy-open3d") / "OpenGaussian";
        const auto before = listing(old);

        const CarryOver done = opengaussian::carryOverSkipping(old, new_dir);
        if (done.kind != Kind::Copied || done.files != 17 || !done.left_out.empty())
            show(done);
        CHECK(done.kind == Kind::Copied);
        CHECK(done.files == 17);
        CHECK(done.left_out.empty());
        CHECK(listing(new_dir) == withoutSkipped(before));
        CHECK(listing(old) == before);
        // Nothing is left beside the new folder.
        CHECK(std::distance(fs::directory_iterator(new_dir.parent_path()), fs::directory_iterator()) == 1);

        // From then on nothing is copied, and the new folder stays as it is.
        write(new_dir / "config" / "preferences.json", "{}");
        write(old / "config" / "added.json", "later");
        const auto after_first = listing(new_dir);
        CHECK(opengaussian::carryOverSkipping(old, new_dir).kind == Kind::Nothing);
        CHECK(listing(new_dir) == after_first);
    }

    void anExistingOrMissingFolderMeansNothing() {
        std::puts("an existing new folder, even empty, or no old folder means nothing to do");
        const fs::path old = fakeOldFolder("nothing");
        const fs::path empty = scratch("nothing-open3d") / "OpenGaussian";
        fs::create_directories(empty);
        CHECK(opengaussian::carryOverSkipping(old, empty).kind == Kind::Nothing);
        CHECK(fs::is_empty(empty));

        const fs::path missing_new = scratch("nothing-open3d-2") / "OpenGaussian";
        CHECK(opengaussian::carryOverSkipping(scratch("nothing") / "missing", missing_new).kind == Kind::Nothing);
        CHECK(!fs::exists(missing_new));
        const opengaussian::CarriedOver carried = opengaussian::carryUserFolder(scratch("nothing") / "missing", missing_new);
        CHECK(carried.folder == missing_new);
        CHECK(!fs::exists(missing_new));

        // Without an Open3D folder, the old one stays in use and nothing is copied.
        const opengaussian::CarriedOver none = opengaussian::carryUserFolder(old, std::nullopt);
        CHECK(none.folder == old);
        CHECK(none.result.kind == Kind::Nothing);
    }

    void aFailedCopyKeepsTheOldFolderInUse() {
        std::puts("a copy that fails leaves nothing behind, and the old folder in use");
        const fs::path old = fakeOldFolder("fail");
        const auto before = listing(old);
        // The folder the new one goes in is a file.
        const fs::path blocked = scratch("fail-open3d") / "Open3D";
        write(blocked, "a file");
        const opengaussian::CarriedOver carried = opengaussian::carryUserFolder(old, blocked / "OpenGaussian");
        CHECK(carried.result.kind == Kind::Failed);
        CHECK(!carried.result.reason.empty());
        // Its paths are said as people write them.
        CHECK(carried.result.reason.find("\\\\?\\") == std::string::npos);
        CHECK(carried.result.reason.find(blocked.string()) != std::string::npos);
        CHECK(carried.folder == old);
        CHECK(read(blocked) == "a file");
        CHECK(listing(old) == before);

        // A new folder inside the old one is refused, and nothing is made.
        const CarryOver inside = opengaussian::carryOverSkipping(old, old / "data" / "OpenGaussian");
        CHECK(inside.kind == Kind::Failed);
        CHECK(!fs::exists(old / "data" / "OpenGaussian"));
        CHECK(listing(old) == before);
        CHECK(std::distance(fs::directory_iterator(blocked.parent_path()), fs::directory_iterator()) == 1);
    }

    void aFileInUseIsLeftOutAndNamed() {
        std::puts("a file held by another program is left out and named; the rest is copied");
        const fs::path old = fakeOldFolder("held");
        const fs::path new_dir = scratch("held-open3d") / "OpenGaussian";
        // Held with no sharing at all, at a level walked here and inside a folder copied whole.
        std::vector<HANDLE> held;
        for (const fs::path& file : {old / "top.txt", old / "plugins" / "foo" / "pyproject.toml", old / "data" / "presets" / "p.json"})
            held.push_back(CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        const CarryOver done = opengaussian::carryOverSkipping(old, new_dir);
        for (const HANDLE handle : held)
            CloseHandle(handle);
        CHECK(done.kind == Kind::Copied);
        CHECK(done.files == 14);
        CHECK(done.left_out.size() == 3);
        std::map<std::string, std::string> left_out;
        for (const auto& [path, why] : done.left_out)
            left_out[path.generic_string()] = why;
        CHECK(left_out.contains("top.txt"));
        CHECK(left_out.contains("plugins/foo/pyproject.toml"));
        CHECK(left_out.contains("data/presets/p.json"));
        CHECK(!left_out["top.txt"].empty());
        CHECK(!fs::exists(new_dir / "top.txt"));
        CHECK(read(new_dir / "plugins" / "foo" / "src" / "mod.py") == "x = 1");
    }

    void linksAreNotFollowed() {
        std::puts("a junction in the old folder is neither followed nor copied, and is named");
        const fs::path old = fakeOldFolder("link");
        const fs::path elsewhere = scratch("link-elsewhere");
        write(elsewhere / "secret.txt", "not ours");
        const std::wstring line = L"cmd /d /c mklink /J \"" + (old / "plugins" / "linked").wstring() + L"\" \"" +
                                  elsewhere.wstring() + L"\" >nul";
        CHECK(_wsystem(line.c_str()) == 0);
        const fs::path new_dir = scratch("link-open3d") / "OpenGaussian";
        const CarryOver done = opengaussian::carryOverSkipping(old, new_dir);
        CHECK(done.kind == Kind::Copied);
        CHECK(done.left_out.size() == 1);
        CHECK(!done.left_out.empty() && done.left_out[0].first.generic_string() == "plugins/linked");
        CHECK(!fs::exists(new_dir / "plugins" / "linked"));
        CHECK(read(elsewhere / "secret.txt") == "not ours");
    }

    // userFolder chooses once per run, so this runs last.
    void userFolderChoosesOnceAndLfsHomeWins() {
        std::puts("userFolder carries over into OPEN3D_DATA_DIR once, and LFS_HOME wins without a copy");
        const fs::path old = fakeOldFolder("choice");
        const fs::path data = scratch("choice-open3d");
        setVariable(L"OPEN3D_DATA_DIR", data);
        setVariable(L"LFS_HOME", {});
        CHECK(!opengaussian::isUserFolder(data / "OpenGaussian"));
        CHECK(opengaussian::carriedOver().result.kind == Kind::Nothing);

        CHECK(opengaussian::userFolder(old) == data / "OpenGaussian");
        CHECK(read(data / "OpenGaussian" / "config" / "preferences.json") == "{\"language\": \"es\"}\n");
        CHECK(!fs::exists(data / "OpenGaussian" / "venv"));
        CHECK(opengaussian::isUserFolder(data / "OpenGaussian"));
        CHECK(!opengaussian::isUserFolder(old));
        const opengaussian::CarriedOver carried = opengaussian::carriedOver();
        CHECK(carried.result.kind == Kind::Copied);
        CHECK(carried.old_dir == old);
        CHECK(carried.new_dir == data / "OpenGaussian");

        // The choice holds for the run.
        CHECK(opengaussian::userFolder(scratch("choice") / "other") == data / "OpenGaussian");

        const fs::path home = scratch("choice-lfs-home");
        setVariable(L"LFS_HOME", home);
        CHECK(opengaussian::userFolder(old) == home);
        CHECK(fs::is_empty(home));
        CHECK(!opengaussian::isUserFolder(home));
        setVariable(L"LFS_HOME", {});
        CHECK(opengaussian::userFolder(old) == data / "OpenGaussian");
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::puts("usage: test_user_folder <a folder that does not exist yet>");
        return 2;
    }
    g_root = fs::absolute(argv[1]);
    if (fs::exists(g_root)) {
        std::puts("test_user_folder: the folder must not exist yet");
        return 2;
    }
    fs::create_directories(g_root);
    // Every Open3D folder in the test's own, whatever this computer has.
    setVariable(L"OPEN3D_SETTINGS_DIR", g_root / "settings");
    setVariable(L"OPEN3D_DATA_DIR", g_root / "data");
    setVariable(L"LFS_HOME", {});

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        theOldFolderIsCopiedWithoutWhatIsMadeAgain();
        anExistingOrMissingFolderMeansNothing();
        aFailedCopyKeepsTheOldFolderInUse();
        aFileInUseIsLeftOutAndNamed();
        linksAreNotFollowed();
        userFolderChoosesOnceAndLfsHomeWins();
    } catch (const std::exception& exception) {
        std::printf("  FAILED: %s\n", exception.what());
        ++g_failures;
    }

    if (g_failures != 0) {
        std::printf("%d failed\n", g_failures);
        return 1;
    }
    std::puts("all passed");
    return 0;
}
