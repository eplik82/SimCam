// =============================================================================
//  Püsivara versioon
//  GitHub Actions paneb selle release'i sildist (v1.2.3 → "1.2.3"),
//  kohalikul ehitusel tools/version.py (git describe) või "0.0.0-dev".
//  FOTA võrdleb seda GitHubi viimase release'i versiooniga.
// =============================================================================
#pragma once

#ifndef SIMCAM_VERSION
#define SIMCAM_VERSION "0.0.0-dev"
#endif

#define SIMCAM_GITHUB_REPO   "eplik82/SimCam"          // FOTA allikas
#define SIMCAM_FW_ASSET      "simcam-firmware.bin"     // release'i fail
