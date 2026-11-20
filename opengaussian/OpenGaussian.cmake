# SPDX-License-Identifier: GPL-3.0-or-later
#
# OpenGaussian branding layer on top of LichtFeld Studio.
#
# Included once from the top-level CMakeLists.txt, right after WindowsVersionResource.cmake and before any
# target exists. It defines the product constants, replaces the Windows version resource with an OpenGaussian
# one, and defers the rest (executable name, icon, the shared language, the user folder, branded runtime
# resources) to the end of the configure step, when every upstream target exists. Keeping all of this here keeps
# the diff against upstream small.

include_guard(GLOBAL)

set(OG_PRODUCT_NAME "OpenGaussian")
set(OG_VERSION "0.1.0")
set(OG_EXE_NAME "OpenGaussian")
set(OG_USER_DIR_NAME ".opengaussian")
set(OG_REPO_URL "https://github.com/carlesllavata/OpenGaussian")
set(OG_UPSTREAM_NAME "LichtFeld Studio")
set(OG_UPSTREAM_REPO_URL "https://github.com/MrNeRF/LichtFeld-Studio")
# UI languages shipped with OpenGaussian; the others stay in the source tree to keep merges clean.
set(OG_LOCALES en es)
set(OG_DIR "${CMAKE_CURRENT_LIST_DIR}")

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$" _og_version_ok "${OG_VERSION}")
if(NOT _og_version_ok)
    message(FATAL_ERROR "OG_VERSION must be X.Y.Z, got '${OG_VERSION}'")
endif()
set(OG_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(OG_VERSION_MINOR "${CMAKE_MATCH_2}")
set(OG_VERSION_PATCH "${CMAKE_MATCH_3}")

add_compile_definitions(
    OG_PRODUCT_NAME="${OG_PRODUCT_NAME}"
    OG_VERSION="${OG_VERSION}"
    OG_USER_DIR_NAME="${OG_USER_DIR_NAME}"
    OG_REPO_URL="${OG_REPO_URL}"
    OG_UPSTREAM_NAME="${OG_UPSTREAM_NAME}"
    OG_UPSTREAM_REPO_URL="${OG_UPSTREAM_REPO_URL}"
)

message(STATUS "OpenGaussian ${OG_VERSION} (based on ${OG_UPSTREAM_NAME} ${PROJECT_VERSION})")

# Replaces upstream's function of the same name (cmake/WindowsVersionResource.cmake): same behaviour, but the
# file properties carry the OpenGaussian name and version, which the Open3D launcher reads.
function(lfs_add_windows_version_resource target description)
    if(NOT WIN32)
        return()
    endif()

    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        set(OG_WINDOWS_FILE_TYPE 0x1L) # VFT_APP
    elseif(target_type STREQUAL "SHARED_LIBRARY" OR target_type STREQUAL "MODULE_LIBRARY")
        set(OG_WINDOWS_FILE_TYPE 0x2L) # VFT_DLL, including Python extension modules
    else()
        message(FATAL_ERROR "Windows version resources require an executable or DLL: ${target}")
    endif()

    string(REPLACE "${OG_UPSTREAM_NAME}" "${OG_PRODUCT_NAME}" OG_WINDOWS_FILE_DESCRIPTION "${description}")
    set(OG_WINDOWS_INTERNAL_NAME "$<TARGET_FILE_BASE_NAME:${target}>")
    set(OG_WINDOWS_ORIGINAL_FILENAME "$<TARGET_FILE_NAME:${target}>")

    set(resource_dir "${CMAKE_CURRENT_BINARY_DIR}/windows-resources")
    configure_file("${OG_DIR}/windows_version.rc.in" "${resource_dir}/${target}.rc.in" @ONLY)
    set(resource "${resource_dir}/$<CONFIG>/${target}.version.rc")
    file(GENERATE OUTPUT "${resource}" INPUT "${resource_dir}/${target}.rc.in" TARGET ${target})
    target_sources(${target} PRIVATE "${resource}")
endfunction()

function(og_finalize)
    set(app "${PROJECT_NAME}")
    if(NOT TARGET ${app})
        message(FATAL_ERROR "OpenGaussian: executable target '${app}' not found")
    endif()

    set_target_properties(${app} PROPERTIES OUTPUT_NAME "${OG_EXE_NAME}")

    # The language every Open3D app shares: open3d_settings.h and .cpp (copies of common files of the Open3D
    # suite, in their own style) and shared_language.cpp, through which upstream's language preference
    # (src/visualizer/preferences.cpp) reads and writes it. They build into the visualizer, which calls them.
    if(NOT TARGET lfs_visualizer)
        message(FATAL_ERROR "OpenGaussian: target 'lfs_visualizer' not found")
    endif()
    target_sources(lfs_visualizer PRIVATE "${OG_DIR}/open3d_settings.cpp" "${OG_DIR}/shared_language.cpp"
                                          "${OG_DIR}/user_folder_notice.cpp")
    target_include_directories(lfs_visualizer PRIVATE "${CMAKE_SOURCE_DIR}")

    # The folder OpenGaussian keeps everything in, %LOCALAPPDATA%\Open3D\OpenGaussian, with the old one carried over
    # (user_folder.hpp). It builds into the logger, the lowest library whose code builds that folder, with its own
    # copy of open3d_settings.cpp; lfs_core and lfs_preprocessing include it too.
    foreach(og_target lfs_logger lfs_core lfs_preprocessing)
        if(NOT TARGET ${og_target})
            message(FATAL_ERROR "OpenGaussian: target '${og_target}' not found")
        endif()
        target_include_directories(${og_target} PRIVATE "${CMAKE_SOURCE_DIR}")
    endforeach()
    target_sources(lfs_logger PRIVATE "${OG_DIR}/open3d_settings.cpp" "${OG_DIR}/user_folder.cpp")

    if(WIN32)
        # Swap upstream's icon resource for ours.
        get_target_property(sources ${app} SOURCES)
        list(TRANSFORM sources REPLACE "resources/lichtfeld-studio\\.rc$" "${OG_DIR}/resources/opengaussian.rc")
        set_property(TARGET ${app} PROPERTY SOURCES ${sources})
    endif()

    # nvImageCodec decodes JPEG on the GPU only if nvjpeg64_*.dll sits next to the exe. Upstream copies cudart and
    # curand but not nvJPEG, so without a CUDA Toolkit on PATH decoding falls back to the CPU. NVIDIA's CUDA EULA
    # lists nvjpeg.dll as redistributable (see tools/license_texts/NVIDIA-CUDA-Toolkit-redistribution.txt).
    if(WIN32 AND CUDAToolkit_BIN_DIR)
        file(GLOB og_nvjpeg_dlls "${CUDAToolkit_BIN_DIR}/nvjpeg64_*.dll" "${CUDAToolkit_BIN_DIR}/x64/nvjpeg64_*.dll")
        if(og_nvjpeg_dlls)
            add_custom_command(TARGET ${app} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${og_nvjpeg_dlls} "$<TARGET_FILE_DIR:${app}>"
                COMMENT "Copying nvJPEG next to OpenGaussian.exe"
                VERBATIM
            )
        else()
            message(WARNING "OpenGaussian: nvjpeg64_*.dll not found in ${CUDAToolkit_BIN_DIR}; JPEG decoding will use the CPU")
        endif()
    endif()

    # Branded runtime resources: our logos over upstream's file names, and only the shipped languages with the
    # product name replaced. Runs after every upstream step that copies resources, on every build (it is cheap).
    # Lists are passed with "|" separators: a ";" would split the argument in a custom command.
    string(JOIN "|" og_locales_joined ${OG_LOCALES})
    set(branding_args
        "-DOG_DIR=${OG_DIR}"
        "-DOG_LOCALE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src/visualizer/gui/resources/locales"
        "-DOG_LOCALES=${og_locales_joined}"
        "-DOG_PRODUCT_NAME=${OG_PRODUCT_NAME}"
        "-DOG_UPSTREAM_NAME=${OG_UPSTREAM_NAME}"
    )
    add_custom_target(og_branding ALL
        COMMAND ${CMAKE_COMMAND} ${branding_args}
                "-DOG_RESOURCE_DIRS=$<TARGET_FILE_DIR:${app}>/resources|${CMAKE_BINARY_DIR}/src/visualizer/resources"
                -P "${OG_DIR}/ApplyBranding.cmake"
        COMMENT "Applying OpenGaussian branding to runtime resources"
        VERBATIM
    )
    add_dependencies(og_branding ${app})
    foreach(dep copy_locale_files copy_visualizer_resources)
        if(TARGET ${dep})
            add_dependencies(og_branding ${dep})
        endif()
    endforeach()

    # Same for `cmake --install` (portable packages). This install rule is registered last, so it runs after
    # upstream's own resource install rules.
    install(CODE "
        set(_og_dirs \"\")
        foreach(_og_candidate \"\${CMAKE_INSTALL_PREFIX}/resources\" \"\${CMAKE_INSTALL_PREFIX}/share/LichtFeld-Studio\")
            if(IS_DIRECTORY \"\${_og_candidate}\")
                string(APPEND _og_dirs \"\${_og_candidate}|\")
            endif()
        endforeach()
        execute_process(COMMAND \"${CMAKE_COMMAND}\"
            \"-DOG_DIR=${OG_DIR}\"
            \"-DOG_LOCALE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src/visualizer/gui/resources/locales\"
            \"-DOG_LOCALES=${og_locales_joined}\"
            \"-DOG_PRODUCT_NAME=${OG_PRODUCT_NAME}\"
            \"-DOG_UPSTREAM_NAME=${OG_UPSTREAM_NAME}\"
            \"-DOG_RESOURCE_DIRS=\${_og_dirs}\"
            -P \"${OG_DIR}/ApplyBranding.cmake\"
            COMMAND_ERROR_IS_FATAL ANY)
    ")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL og_finalize)
