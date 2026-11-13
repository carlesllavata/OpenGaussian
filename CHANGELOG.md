# Changelog

All notable changes to OpenGaussian, newest first. OpenGaussian is LichtFeld Studio under another name, part of
the Open3D suite; changes that come from LichtFeld Studio are summarised per merge.

## 0.1.0 – 2026-11-13

First version of OpenGaussian, based on LichtFeld Studio 0.5.5 in development (commit `9b6dce25b`).

- The program is now called OpenGaussian: `OpenGaussian.exe`, window title, icon, logo on the start screen,
  About window and the version shown in the file properties.
- The About window shows the OpenGaussian version together with the LichtFeld Studio version it is based on,
  links to the OpenGaussian repository and credits the LichtFeld Studio authors.
- Settings, plugins, logs and caches are kept in `%USERPROFILE%\.opengaussian`, separate from an installed
  LichtFeld Studio.
- The interface comes in English (default) and Spanish.
- No accounts: the LichtFeld Portal account, the online Gallery (publishing, sync and transfers) and the
  in-app bug report form are switched off. *Help > Report a bug* opens OpenGaussian's GitHub issues.
- The start screen no longer shows LichtFeld Studio's community, donation and sponsor links.
- JPEG photos are decoded on the GPU without having to install the CUDA Toolkit.
- Ships as a portable zip, `opengaussian-<version>-windows-x64.zip`, with the licences of every third-party
  component in `THIRD-PARTY-LICENSES.md`.
