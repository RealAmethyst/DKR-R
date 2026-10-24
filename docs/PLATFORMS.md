# DKR-R platform support

The v1.0.9 Release Build set was prepared on 24 October 2026. Windows and Linux
ship together; both Linux architectures have AppImages. Keep the complete
package, including its private helpers and required notices.

| Platform | v1.0.9 delivery | Requirements and coverage |
|---|---|---|
| Windows x64 | `DKR-R-1.0.9-Windows-x64.zip` | Windows 10+, compatible Vulkan driver; extract the entire ZIP. |
| Linux x86_64 | `DKR-R-1.0.9-Linux-x86_64.AppImage` | GLIBC 2.38, GLIBCXX_3.4.32 and a compatible Vulkan driver. |
| Linux ARM64 | `DKR-R-1.0.9-Linux-aarch64.AppImage` | 64-bit Linux/Vulkan, the same ABI requirements; physical GPU acceptance is separate. |
| Steam Deck | `DKR-R-1.0.9-SteamDeck-x86_64.AppImage` | Exact Linux x86_64 payload; SteamOS/controller/gyro acceptance needs the device. |
| Android ARM64 | `DKR-R-1.0.9-Android-arm64.apk` | Android 9+ (API 28), ARM64 OS and compatible Vulkan; versionCode 1090001, non-debuggable Release. |
| macOS | No new package in this build set | Published v1.0.8 includes a notarised ARM64 ZIP. A v1.0.9 package needs an Apple host and current feature qualification. |
| iOS | No new package in this build set | Published v1.0.8 includes an ARM64 IPA. Sideload source targets iOS 18+; v1.0.9 device packaging/qualification remains separate. |

The previous Apple assets are not v1.0.9 binaries and must not be renamed or
included in its update manifest. Source compile-check workflows do not produce
qualified release packages. Current public Rollback integration remains gated
out on Apple; a source port alone does not establish parity with this release.

Android retains application ID `io.github.thatguymcd.dkrr.preview` and the
distributed signing certificate. The historical `.preview` ID is retained for
upgrade continuity; do not change it just to make the package name look stable.
Static checks verify the original signer, APK layout and at least 16 KiB native
load alignment. They do not establish device performance or system-installer
acceptance. See [release validation](../RELEASE-VALIDATION.md).

Both clean US v1.0 and US Rev A/v1.1 ROMs are supported by the native revision
selection. Owning a 64-bit CPU is not enough if the OS or GPU driver lacks the
required features. There is no 32-bit ARM or Android x86 package in this workflow.

These Linux packages were built with a glibc 2.38 baseline. An AppImage does
not replace the system's glibc or Vulkan driver; an older distribution needs
an appropriate source build rather than a renamed package.

## Android and handhelds

The mobile UI uses a Menu drawer, density-aware text and finger scrolling.
**Controls → Touch Screen** offers editable N64-style touch groups, including
position, size, opacity, visibility and left-handed placement. Menu remains
available when racing controls are hidden. Android uses in-process SDL2 input,
not the desktop SDL3 helper.

Install compatible updates over the existing app where possible. Uninstalling
removes Android's private app data, so export saves first. Use file/ZIP imports
when folder picking is unavailable. APK compilation alone does not certify
Adreno, Mali or a particular handheld.

For a black screen, crash or slowdown, record the device, Android version,
build/version code and scene. Reopen the app and use
**About DKR-R → Support summary → Export Android Startup Logs** if available.
Do not keep retrying a diagnostic action that itself crashes.
The [troubleshooting guide](TROUBLESHOOTING.md) explains what to send.

Android's integrated Rollback port does not establish cross-architecture
netplay compatibility: admission still checks architecture and floating-point
mode. Backgrounding Android ends an active Rollback match rather than
leaving other racers waiting on a suspended app.

## Building and qualification

Follow [Building DKR-R](BUILDING.md). The older
[ARM build recipes](ARM-PLATFORM-PREVIEW.md) and
[mobile UI implementation report](ANDROID-MOBILE-UI.md) retain their original
preview results; check current toolchain settings before reproducing them.

A platform is not qualified just because it compiles. Check ROM import, both
revisions, rendering, audio, controller/touch input, save reload, return to the
launcher and background/resume on actual hardware. Online testing comes after
ordinary play works. [Release validation](../RELEASE-VALIDATION.md) distinguishes
automated checks from hardware/gameplay acceptance.

## Linux ABI requirements for these release files

Static inspection of both final Linux architectures finds GLIBC 2.38 and
GLIBCXX_3.4.32 requirements. Install a system libstdc++ providing that symbol
version, along with the normal desktop/audio/font stack and a compatible
Vulkan driver. The AppImages do not replace the host C++ runtime, glibc or
graphics driver. Core host libraries remain excluded in line with the
[AppImage library policy](https://github.com/AppImage/pkg2appimage/blob/master/excludelist);
other required linked libraries are bundled. An older distro needs a
compatible source build, not a renamed AppImage.
