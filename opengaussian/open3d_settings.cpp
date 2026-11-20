// SPDX-License-Identifier: GPL-3.0-or-later
//
// The language shared by every OPEN3D app (see open3d_settings.h).
//
// This file is a copy. Its original is common/settings/open3d_settings.cpp in the OPEN3D root: change it
// there, never here. It reads JSON by itself, without a library: it checks the whole file as RFC 8259
// says and keeps the value of every other key as it was written.

#include "open3d_settings.h"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace open3d_settings
{
namespace
{

constexpr std::string_view kKey = "language";
// Deeper than this, a file is not read: as serde_json does, and so that it cannot exhaust the stack.
constexpr int kMaxDepth = 128;

// One key of the object in settings.json.
struct Member
{
    std::string key;                 // decoded, in UTF-8
    std::string raw_value;           // as it was written
    std::optional<std::string> text; // the decoded value, when it is a string
};

// Whether `text` is well-formed UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
bool valid_utf8(std::string_view text)
{
    std::size_t at = 0;
    while (at < text.size())
    {
        const auto byte = static_cast<unsigned char>(text[at]);
        std::size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        if (byte < 0x80)
        {
            ++at;
            continue;
        }
        if (byte >= 0xC2 && byte <= 0xDF)
            length = 2;
        else if (byte >= 0xE0 && byte <= 0xEF)
        {
            length = 3;
            if (byte == 0xE0)
                low = 0xA0;
            if (byte == 0xED)
                high = 0x9F;
        }
        else if (byte >= 0xF0 && byte <= 0xF4)
        {
            length = 4;
            if (byte == 0xF0)
                low = 0x90;
            if (byte == 0xF4)
                high = 0x8F;
        }
        else
            return false;
        if (at + length > text.size())
            return false;
        for (std::size_t i = 1; i < length; ++i)
        {
            const auto next = static_cast<unsigned char>(text[at + i]);
            if (next < (i == 1 ? low : 0x80) || next > (i == 1 ? high : 0xBF))
                return false;
        }
        at += length;
    }
    return true;
}

void append_utf8(std::string& out, std::uint32_t point)
{
    if (point < 0x80)
        out += static_cast<char>(point);
    else if (point < 0x800)
    {
        out += static_cast<char>(0xC0 | (point >> 6));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
    else if (point < 0x10000)
    {
        out += static_cast<char>(0xE0 | (point >> 12));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (point >> 18));
        out += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
}

// Whether a number that is out of the range of a double is too large (rather than too small): the
// power of ten of its first significant digit is above zero. Too large makes the file unreadable, as
// in the Rust and Python copies; too small is zero.
bool too_large(std::string_view number)
{
    std::size_t at = number.size() > 0 && number[0] == '-' ? 1 : 0;
    long long power = 0;
    bool found = false;
    long long digits = 0;
    for (; at < number.size() && number[at] >= '0' && number[at] <= '9'; ++at, ++digits)
        if (!found && number[at] != '0')
        {
            found = true;
            power = -digits;
        }
    if (found)
        power += digits - 1;
    if (at < number.size() && number[at] == '.')
        for (long long place = 1; ++at < number.size() && number[at] >= '0' && number[at] <= '9'; ++place)
            if (!found && number[at] != '0')
            {
                found = true;
                power = -place;
            }
    if (!found)
        return false;
    long long exponent = 0;
    bool negative = false;
    if (at < number.size() && (number[at] == 'e' || number[at] == 'E'))
    {
        ++at;
        if (at < number.size() && (number[at] == '+' || number[at] == '-'))
            negative = number[at++] == '-';
        for (; at < number.size(); ++at)
            if (exponent < 1000000000)
                exponent = exponent * 10 + (number[at] - '0');
    }
    return power + (negative ? -exponent : exponent) > 0;
}

// Reads a whole settings.json, checking it as RFC 8259 says.
class Reader
{
  public:
    explicit Reader(std::string_view text) : text_(text)
    {
    }

    // The members of the object the text is, or nothing when it is not one.
    std::optional<std::vector<Member>> object_members()
    {
        if (!valid_utf8(text_))
            return std::nullopt;
        if (text_.substr(0, 3) == "\xEF\xBB\xBF")
            at_ = 3;
        skip_space();
        std::vector<Member> members;
        if (peek() != '{' || !object(&members))
            return std::nullopt;
        skip_space();
        if (at_ != text_.size())
            return std::nullopt;
        return members;
    }

  private:
    std::string_view text_;
    std::size_t at_ = 0;
    int depth_ = 0;

    char peek() const
    {
        return at_ < text_.size() ? text_[at_] : '\0';
    }

    bool digit() const
    {
        return peek() >= '0' && peek() <= '9';
    }

    void skip_space()
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')
            ++at_;
    }

    bool hex4(std::uint32_t* value)
    {
        if (at_ + 4 > text_.size())
            return false;
        *value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[at_++];
            std::uint32_t digit_value = 0;
            if (c >= '0' && c <= '9')
                digit_value = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit_value = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                digit_value = static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return false;
            *value = *value * 16 + digit_value;
        }
        return true;
    }

    // A string, at its opening quote; its decoded text goes to `decoded` when there is one.
    bool string(std::string* decoded)
    {
        ++at_;
        while (at_ < text_.size())
        {
            const auto c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"')
                return true;
            if (c < 0x20)
                return false;
            if (c != '\\')
            {
                if (decoded)
                    *decoded += static_cast<char>(c);
                continue;
            }
            if (at_ >= text_.size())
                return false;
            const char escape = text_[at_++];
            char plain = 0;
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                plain = escape;
                break;
            case 'b':
                plain = '\b';
                break;
            case 'f':
                plain = '\f';
                break;
            case 'n':
                plain = '\n';
                break;
            case 'r':
                plain = '\r';
                break;
            case 't':
                plain = '\t';
                break;
            case 'u': {
                std::uint32_t point = 0;
                if (!hex4(&point))
                    return false;
                if (point >= 0xDC00 && point <= 0xDFFF)
                    return false;
                if (point >= 0xD800 && point <= 0xDBFF)
                {
                    std::uint32_t low = 0;
                    if (text_.substr(at_, 2) != "\\u")
                        return false;
                    at_ += 2;
                    if (!hex4(&low) || low < 0xDC00 || low > 0xDFFF)
                        return false;
                    point = 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00);
                }
                if (decoded)
                    append_utf8(*decoded, point);
                continue;
            }
            default:
                return false;
            }
            if (decoded)
                *decoded += plain;
        }
        return false;
    }

    bool number()
    {
        const std::size_t start = at_;
        if (peek() == '-')
            ++at_;
        if (peek() == '0')
            ++at_;
        else if (digit())
            while (digit())
                ++at_;
        else
            return false;
        if (peek() == '.')
        {
            ++at_;
            if (!digit())
                return false;
            while (digit())
                ++at_;
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++at_;
            if (peek() == '+' || peek() == '-')
                ++at_;
            if (!digit())
                return false;
            while (digit())
                ++at_;
        }
        const std::string_view token = text_.substr(start, at_ - start);
        double value = 0;
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        if (result.ec == std::errc::result_out_of_range)
            return !too_large(token);
        return result.ec == std::errc() && result.ptr == token.data() + token.size();
    }

    bool word(std::string_view expected)
    {
        if (text_.substr(at_, expected.size()) != expected)
            return false;
        at_ += expected.size();
        return true;
    }

    bool value(std::optional<std::string>* text = nullptr)
    {
        switch (peek())
        {
        case '{':
            return object(nullptr);
        case '[':
            return array();
        case '"':
            if (text)
            {
                text->emplace();
                return string(&**text);
            }
            return string(nullptr);
        case 't':
            return word("true");
        case 'f':
            return word("false");
        case 'n':
            return word("null");
        default:
            return number();
        }
    }

    bool array()
    {
        if (++depth_ > kMaxDepth)
            return false;
        ++at_;
        skip_space();
        if (peek() != ']')
            for (;;)
            {
                if (!value())
                    return false;
                skip_space();
                if (peek() != ',')
                    break;
                ++at_;
                skip_space();
            }
        if (peek() != ']')
            return false;
        ++at_;
        --depth_;
        return true;
    }

    // An object, at its opening brace. Its members go to `members` when there is one: a key that comes
    // again keeps its first place and takes its last value.
    bool object(std::vector<Member>* members)
    {
        if (++depth_ > kMaxDepth)
            return false;
        ++at_;
        skip_space();
        if (peek() != '}')
            for (;;)
            {
                if (peek() != '"')
                    return false;
                Member member;
                if (!string(&member.key))
                    return false;
                skip_space();
                if (peek() != ':')
                    return false;
                ++at_;
                skip_space();
                const std::size_t value_start = at_;
                if (!value(&member.text))
                    return false;
                member.raw_value = std::string(text_.substr(value_start, at_ - value_start));
                if (members)
                {
                    auto same = members->begin();
                    while (same != members->end() && same->key != member.key)
                        ++same;
                    if (same == members->end())
                        members->push_back(std::move(member));
                    else
                        *same = std::move(member);
                }
                skip_space();
                if (peek() != ',')
                    break;
                ++at_;
                skip_space();
            }
        if (peek() != '}')
            return false;
        ++at_;
        --depth_;
        return true;
    }
};

// A key as a JSON string.
std::string json_string(const std::string& key)
{
    std::string text = "\"";
    for (const char c : key)
    {
        if (c == '"' || c == '\\')
            text += std::string("\\") + c;
        else if (static_cast<unsigned char>(c) < 0x20)
        {
            const char* digits = "0123456789abcdef";
            text += std::string("\\u00") + digits[(c >> 4) & 0xF] + digits[c & 0xF];
        }
        else
            text += c;
    }
    return text + "\"";
}

std::string display(const std::filesystem::path& path)
{
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr);
    return text;
#else
    return path.string();
#endif
}

bool fail(std::string* error, std::string text)
{
    if (error)
        *error = std::move(text);
    return false;
}

std::optional<std::filesystem::path> environment(const char* name)
{
#ifdef _WIN32
    const std::wstring wide_name(name, name + std::char_traits<char>::length(name));
    const DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (size <= 1)
        return std::nullopt;
    std::wstring value(size, L'\0');
    const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
    if (length == 0 || length >= size)
        return std::nullopt;
    value.resize(length);
    return std::filesystem::path(value);
#else
    const char* value = std::getenv(name);
    if (!value || !*value)
        return std::nullopt;
    return std::filesystem::path(value);
#endif
}

// Writes `data` into a new file in the folder of `path`, which then takes the place of `path`: another
// app never reads half a file.
bool replace(const std::filesystem::path& path, const std::string& data, std::string* error)
{
#ifdef _WIN32
    const std::wstring stamp = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path temporary = path.wstring() + L"." + stamp + L"-" + std::to_wstring(attempt) + L".tmp";
        const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD last_error = GetLastError();
            if (last_error == ERROR_FILE_EXISTS)
                continue;
            return fail(error, "couldn't create a file in " + display(path.parent_path()) + " (" +
                                   std::system_category().message(static_cast<int>(last_error)) + ")");
        }
        DWORD written = 0;
        const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                        written == data.size() && FlushFileBuffers(file);
        DWORD last_error = ok ? 0 : GetLastError();
        CloseHandle(file);
        if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (ok)
            last_error = GetLastError();
        DeleteFileW(temporary.c_str());
        return fail(error, "couldn't write " + display(path) + " (" +
                               std::system_category().message(static_cast<int>(last_error)) + ")");
    }
    return fail(error, "couldn't create a file in " + display(path.parent_path()) + " (every name tried is taken)");
#else
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file.flush())
            return fail(error, "couldn't write " + display(temporary));
    }
    std::error_code failure;
    std::filesystem::rename(temporary, path, failure);
    if (!failure)
        return true;
    std::filesystem::remove(temporary, failure);
    return fail(error, "couldn't write " + display(path));
#endif
}

// The bytes of a file: nothing when there is no file, an error when it cannot be read.
bool read_file(const std::filesystem::path& path, std::optional<std::string>* content)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::error_code failure;
        *content = std::nullopt;
        return !std::filesystem::exists(path, failure) && !failure;
    }
    std::ostringstream bytes;
    bytes << file.rdbuf();
    if (file.bad())
        return false;
    *content = bytes.str();
    return true;
}

namespace fs = std::filesystem;

// Whether something has the name `path`: a folder, a file or a link. When that cannot be told, it counts
// as there, so that it is never touched.
bool taken(const fs::path& path)
{
    std::error_code failure;
    return fs::symlink_status(path, failure).type() != fs::file_type::not_found;
}

// Whether `path` is `folder` or inside it, comparing their absolute forms part by part.
bool inside(const fs::path& path, const fs::path& folder)
{
    std::error_code failure;
    const fs::path whole = fs::absolute(path, failure).lexically_normal();
    const fs::path base = fs::absolute(folder, failure).lexically_normal();
    auto part = whole.begin();
    for (auto base_part = base.begin(); base_part != base.end(); ++base_part)
    {
        if (base_part->empty())
            continue;
        if (part == whole.end() || *part != *base_part)
            return false;
        ++part;
    }
    return true;
}

std::string couldnt_write(const fs::path& path, const std::string& why)
{
    return "couldn't write " + display(path) + " (" + why + ")";
}

// A new, empty folder of its own in `dir`, named after `name`, for carry_over to copy into.
bool new_folder(const fs::path& dir, const fs::path& name, fs::path* folder, std::string* error)
{
#ifdef _WIN32
    const std::string stamp = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
#else
    const std::string stamp = "0";
#endif
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        fs::path candidate = dir / name;
        candidate += ".carry-" + stamp + "-" + std::to_string(attempt) + ".tmp";
        std::error_code failure;
        if (fs::create_directory(candidate, failure))
        {
            *folder = candidate;
            return true;
        }
        if (failure)
            return fail(error, "couldn't create a folder in " + display(dir) + " (" + failure.message() + ")");
    }
    return fail(error, "couldn't create a folder in " + display(dir) + " (every name tried is taken)");
}

// Gives the finished copy its name, `new_dir`. While nothing else has that name it tries a few times:
// another program, such as an antivirus, may hold the copy for a moment.
bool put_in_place(const fs::path& temporary, const fs::path& new_dir, std::string* error)
{
    std::string last;
    for (int attempt = 0; attempt < 10; ++attempt)
    {
        if (taken(new_dir))
            return fail(error, display(new_dir) + " appeared while the copy was made");
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
    return fail(error, "couldn't rename " + display(temporary) + " to " + display(new_dir) + " (" + last + ")");
}

// What copying one file came to.
enum class Step
{
    Copied,
    // The file cannot be read: it is left out.
    LeftOut,
    // Something cannot be written: the copy stops.
    Stop,
};

// Copies the file `from` into the new file `to`, with its contents and the time it was last changed.
// LeftOut when `from` cannot be read, with why in `why`; then `to` is not left behind. Stop when `to`
// cannot be written. `from` is opened as other programs may still use it.
Step copy_one_file(const fs::path& from, const fs::path& to, std::string* why)
{
#ifdef _WIN32
    const auto message = [](DWORD code) { return std::system_category().message(static_cast<int>(code)); };
    const HANDLE source = CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (source == INVALID_HANDLE_VALUE)
    {
        *why = message(GetLastError());
        return Step::LeftOut;
    }
    FILETIME changed{};
    const bool has_time = GetFileTime(source, nullptr, nullptr, &changed) != 0;
    const HANDLE target = CreateFileW(to.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (target == INVALID_HANDLE_VALUE)
    {
        *why = couldnt_write(to, message(GetLastError()));
        CloseHandle(source);
        return Step::Stop;
    }
    std::vector<char> buffer(1 << 16);
    Step step = Step::Copied;
    for (;;)
    {
        DWORD read = 0;
        if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            *why = message(GetLastError());
            step = Step::LeftOut;
            break;
        }
        if (read == 0)
            break;
        DWORD written = 0;
        if (!WriteFile(target, buffer.data(), read, &written, nullptr) || written != read)
        {
            *why = couldnt_write(to, message(GetLastError()));
            step = Step::Stop;
            break;
        }
    }
    if (step == Step::Copied)
    {
        if (has_time)
            SetFileTime(target, nullptr, nullptr, &changed);
        if (!FlushFileBuffers(target))
        {
            *why = couldnt_write(to, message(GetLastError()));
            step = Step::Stop;
        }
    }
    CloseHandle(target);
    CloseHandle(source);
    if (step == Step::LeftOut)
        DeleteFileW(to.c_str());
    return step;
#else
    std::optional<std::string> content;
    if (!read_file(from, &content) || !content)
    {
        *why = "it cannot be read";
        return Step::LeftOut;
    }
    std::error_code failure;
    if (taken(to))
    {
        *why = couldnt_write(to, "it is there already");
        return Step::Stop;
    }
    std::ofstream file(to, std::ios::binary);
    file.write(content->data(), static_cast<std::streamsize>(content->size()));
    if (!file.flush())
    {
        *why = couldnt_write(to, "the write failed");
        return Step::Stop;
    }
    file.close();
    fs::last_write_time(to, fs::last_write_time(from, failure), failure);
    return Step::Copied;
#endif
}

// The files carry_over has copied so far, and what it left out.
struct Copying
{
    std::size_t files = 0;
    std::vector<std::pair<fs::path, std::string>> left_out;
};

// Copies what the folder `from` holds into the empty folder `to`; `inside_old` is the path of `from`
// inside the old folder. False when the copy must stop (the old folder itself cannot be read, or
// something cannot be written), with why in `error`.
bool copy_tree(const fs::path& from, const fs::path& to, const fs::path& inside_old, Copying* copying, std::string* error)
{
    std::error_code failure;
    fs::directory_iterator entry(from, failure);
    if (failure)
    {
        if (inside_old.empty())
            return fail(error, "couldn't read " + display(from) + " (" + failure.message() + ")");
        copying->left_out.emplace_back(inside_old, failure.message());
        return true;
    }
    const fs::directory_iterator end;
    while (entry != end)
    {
        const fs::path name = entry->path().filename();
        const fs::path path = inside_old / name;
        const fs::path target = to / name;
        std::error_code kind_failure;
        const fs::file_type kind = entry->symlink_status(kind_failure).type();
        if (kind_failure)
            copying->left_out.emplace_back(path, kind_failure.message());
        else if (kind == fs::file_type::directory)
        {
            std::error_code made;
            if (!fs::create_directory(target, made))
                return fail(error, couldnt_write(target, made ? made.message() : std::string("it is there already")));
            if (!copy_tree(entry->path(), target, path, copying, error))
                return false;
        }
        else if (kind == fs::file_type::regular)
        {
            std::string why;
            switch (copy_one_file(entry->path(), target, &why))
            {
            case Step::Copied:
                ++copying->files;
                break;
            case Step::LeftOut:
                copying->left_out.emplace_back(path, why);
                break;
            case Step::Stop:
                return fail(error, why);
            }
        }
        else
            copying->left_out.emplace_back(path, "not a plain file or folder, so not copied");
        entry.increment(failure);
        if (failure)
        {
            copying->left_out.emplace_back(inside_old, failure.message());
            break;
        }
    }
    return true;
}

} // namespace

std::string_view code(Language language)
{
    return language == Language::Spanish ? "es" : "en";
}

std::optional<Language> from_code(std::string_view text)
{
    if (text == "en")
        return Language::English;
    if (text == "es")
        return Language::Spanish;
    return std::nullopt;
}

std::optional<std::filesystem::path> settings_dir()
{
    return settings_dir_from(environment(kDirVariable), environment("APPDATA"));
}

std::optional<std::filesystem::path> settings_dir_from(const std::optional<std::filesystem::path>& variable,
                                                       const std::optional<std::filesystem::path>& appdata)
{
    if (variable && !variable->empty())
        return *variable;
    if (appdata && !appdata->empty())
        return *appdata / "Open3D";
    return std::nullopt;
}

Language language(const std::optional<std::filesystem::path>& dir)
{
    if (dir)
        if (const auto found = read_language(*dir))
            return *found;
    return windows_language();
}

std::optional<Language> read_language(const std::filesystem::path& dir)
{
    std::optional<std::string> content;
    if (!read_file(dir / kFileName, &content) || !content)
        return std::nullopt;
    return language_in(*content);
}

std::optional<Language> language_in(std::string_view content)
{
    const auto members = Reader(content).object_members();
    if (!members)
        return std::nullopt;
    for (const Member& member : *members)
        if (member.key == kKey && member.text)
            return from_code(*member.text);
    return std::nullopt;
}

std::string with_language(std::optional<std::string_view> old, std::optional<Language> language)
{
    std::vector<Member> members;
    if (old)
        members = Reader(*old).object_members().value_or(std::vector<Member>{});
    auto found = members.begin();
    while (found != members.end() && found->key != kKey)
        ++found;
    if (!language)
    {
        if (found != members.end())
            members.erase(found);
    }
    else
    {
        const std::string value = "\"" + std::string(code(*language)) + "\"";
        if (found != members.end())
            found->raw_value = value;
        else
            members.push_back(Member{std::string(kKey), value, std::nullopt});
    }
    if (members.empty())
        return "{}\n";
    std::string text = "{\n";
    for (std::size_t i = 0; i < members.size(); ++i)
        text += "  " + json_string(members[i].key) + ": " + members[i].raw_value + (i + 1 < members.size() ? ",\n" : "\n");
    return text + "}\n";
}

bool write_language(const std::filesystem::path& dir, std::optional<Language> language, std::string* error)
{
    const std::filesystem::path path = dir / kFileName;
    std::optional<std::string> old;
    // Its other keys could not be kept.
    if (!read_file(path, &old))
        return fail(error, "couldn't read " + display(path));
    std::error_code failure;
    std::filesystem::create_directories(dir, failure);
    if (failure || !std::filesystem::is_directory(dir, failure))
        return fail(error, "couldn't create " + display(dir) + (failure ? " (" + failure.message() + ")" : std::string()));
    return replace(path, with_language(old, language), error);
}

Language windows_language()
{
#ifdef _WIN32
    return language_for_windows(GetUserDefaultUILanguage());
#else
    return Language::English;
#endif
}

Language language_for_windows(std::uint16_t langid)
{
    constexpr std::uint16_t kPrimaryLanguage = 0x03FF;
    constexpr std::uint16_t kLangSpanish = 0x0A;
    return (langid & kPrimaryLanguage) == kLangSpanish ? Language::Spanish : Language::English;
}

std::optional<std::filesystem::path> data_dir()
{
    return data_dir_from(environment(kDataDirVariable), environment("LOCALAPPDATA"));
}

std::optional<std::filesystem::path> data_dir_from(const std::optional<std::filesystem::path>& variable,
                                                   const std::optional<std::filesystem::path>& localappdata)
{
    // The same rule as settings_dir_from.
    return settings_dir_from(variable, localappdata);
}

std::optional<std::filesystem::path> app_settings_dir(const std::filesystem::path& app)
{
    if (const auto dir = settings_dir())
        return *dir / app;
    return std::nullopt;
}

std::optional<std::filesystem::path> app_data_dir(const std::filesystem::path& app)
{
    if (const auto dir = data_dir())
        return *dir / app;
    return std::nullopt;
}

CarryOver carry_over(const std::filesystem::path& old_dir, const std::filesystem::path& new_dir)
{
    CarryOver done;
    std::error_code failure;
    if (taken(new_dir) || !std::filesystem::is_directory(old_dir, failure))
        return done;
    done.kind = CarryOver::Kind::Failed;
    if (inside(new_dir, old_dir))
    {
        done.reason = display(new_dir) + " is inside " + display(old_dir);
        return done;
    }
    const std::filesystem::path name = new_dir.filename();
    if (name.empty())
    {
        done.reason = display(new_dir) + " cannot be a folder of its own";
        return done;
    }
    std::filesystem::path parent = new_dir.parent_path();
    if (parent.empty())
        parent = ".";
    std::filesystem::create_directories(parent, failure);
    if (failure || !std::filesystem::is_directory(parent, failure))
    {
        done.reason = "couldn't create a folder in " + display(parent) + (failure ? " (" + failure.message() + ")" : std::string());
        return done;
    }
    std::filesystem::path temporary;
    if (!new_folder(parent, name, &temporary, &done.reason))
        return done;
    Copying copying;
    if (copy_tree(old_dir, temporary, std::filesystem::path(), &copying, &done.reason) &&
        put_in_place(temporary, new_dir, &done.reason))
    {
        done.kind = CarryOver::Kind::Copied;
        done.files = copying.files;
        done.left_out = std::move(copying.left_out);
        done.reason.clear();
        return done;
    }
    std::filesystem::remove_all(temporary, failure);
    // Another start of the same app may have put its own copy in place meanwhile.
    if (taken(new_dir))
        return CarryOver{};
    return done;
}

} // namespace open3d_settings
