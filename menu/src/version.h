/* menu/src/version.h -- Aether's one version string (user decision, Task 16). It is "0.1" and it changes ONLY when the
 * user says so: tools/tests/test_version.py fails the gates on any other value. Everything that shows or records the
 * version reads it from here (the diagnostics build string, the startup screen's "FARKUS 0.1" tag, tools/m3_release.sh's
 * manifest, the hardware checklist title). OM_BUILD_ID ("<git short SHA>[-dirty] <commit date>") is passed in by
 * tools/sbuild.sh from the host's git; a build without it says "dev". BSD-3-Clause. */
#ifndef OM_VERSION_H
#define OM_VERSION_H
#define VIBEMENU_VERSION "0.1"
#ifndef OM_BUILD_ID
#define OM_BUILD_ID "dev"
#endif
#define OM_BUILD_STRING "Aether " VIBEMENU_VERSION " " OM_BUILD_ID
#define OM_VERSION_TAG "FARKUS " VIBEMENU_VERSION
#endif
