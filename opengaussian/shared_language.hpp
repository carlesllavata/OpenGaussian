// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGaussian: the language is the one every Open3D app shares, in settings.json (open3d_settings.h, a
// copy of a common file of the Open3D suite). Upstream's language preference goes through here:
// lfs::vis::loadLanguagePreference and saveLanguagePreference (src/visualizer/preferences.cpp) read and
// write the shared file, so the start screen (startup_overlay.cpp) offers a language only while there is no
// shared preference, and the start of the GUI (gui_manager.cpp) takes its language from takeStartLanguage.
// Upstream's own "language" in preferences.json is only read to carry it over; "language_shared" there
// says that is settled.

#pragma once

#include <string>

namespace opengaussian {

    // The language to show at start, as a code ("en" or "es"): the shared preference; with none, a
    // Spanish chosen in OpenGaussian before is written there once (language_shared in preferences.json
    // remembers it); otherwise the language of Windows. A carry-over that fails is said in a toast and
    // tried again at the next start. Nothing else is written.
    [[nodiscard]] std::string takeStartLanguage();

    // The shared preference ("en" or "es"), or empty when there is none yet. It never writes.
    [[nodiscard]] std::string sharedLanguage();

    // A person chose `code` in OpenGaussian: keep it as the language of every Open3D app (they take it
    // the next time they start). A write that fails is said in a toast; any code but "en" and "es" is
    // not shared.
    void shareLanguage(const std::string& code);

} // namespace opengaussian
