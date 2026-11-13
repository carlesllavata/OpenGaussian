# SPDX-License-Identifier: GPL-3.0-or-later
"""OpenGaussian switches for upstream (LichtFeld Studio) features.

OpenGaussian is part of the Open3D suite, whose apps have no accounts or logins. Upstream's LichtFeld Portal
account, its online Gallery and the portal bug report are therefore switched off here; the code stays in the
tree so upstream merges stay clean. Every place that reads these switches is marked with "OpenGaussian".
"""

# LichtFeld Portal account, online Gallery (publish, sync, transfers) and portal bug reports.
PORTAL_ENABLED = False

# Help > Report a bug opens this page instead of the portal form.
ISSUES_URL = "https://github.com/carlesllavata/OpenGaussian/issues"
