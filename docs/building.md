# Building and installing OpenCPN for iOS yourself

This guide explains how to build the app from source and install it on your own iPhone or
iPad with a free Apple ID. If you received the app through TestFlight, you can use the same
steps to build your own or a modified version. The licence requires that this is possible:
the app is GPLv3, with Qt under LGPLv3 (see [COPYRIGHT.md](../COPYRIGHT.md)).

Measured with: Mac with Apple M4 Pro, macOS 26.5, Xcode 26.6, Qt 6.12.0 (November 2026).
Other versions may work but have not been tried.

## 1. What you need

- A Mac with Apple Silicon and about 45 GB of free space: Qt ~16 GB, sources ~2 GB, build
  folders ~10 GB per target.
- **Xcode** from the App Store, opened once so it installs its components.
  Check: `xcode-select -p` prints `/Applications/Xcode.app/Contents/Developer`.
- **Homebrew** (<https://brew.sh>) and with it:

      brew install cmake ninja gettext xcodegen

- **Qt 6.12.0 for iOS**, from the Qt online installer with a free Qt account. Under
  Qt 6.12.0, select: macOS, iOS Simulator (Apple Silicon), iOS Device and Qt Image Formats.
  Keep the default folder `~/Qt`. For another folder, set `QT_ROOT=/path/to/Qt/6.12.0` for
  all build commands.
- An **Apple ID**. A free account is enough for your own devices (see section 6 for the
  limits).
- For the iPhone or iPad: iOS or iPadOS 18 or newer.

## 2. Getting the source

    git clone https://github.com/xanderburchartz/opencpn-ios.git
    cd opencpn-ios
    scripts/fetch-upstream.sh        # OpenCPN, wxWidgets, plugins, OpenSSL, curl, ... (~2 GB)
    scripts/apply-patches.sh         # apply the iOS changes to that source

`fetch-upstream.sh` fetches every component at a fixed commit, so everyone builds exactly the
same version. `scripts/fetch-upstream.sh --lijst` shows which. Third-party source goes into
`upstream/` and is not stored in git.

## 3. Try the simulator first (recommended)

    scripts/build-deps-ios.sh sim          # OpenSSL and curl
    scripts/build-wxqt-ios.sh sim all      # wxWidgets on Qt
    scripts/build-ocpn-ios.sh sim all      # OpenCPN with plugins

Then install and start it in an iOS simulator. Open the simulator first from Xcode or with
`open -a Simulator`.

    xcrun simctl install booted build/sim/ocpn/opencpn.app
    xcrun simctl launch booted local.opencpnios.opencpn

## 4. Building for your device

First choose your own **bundle ID prefix**. Apple does not allow two developers to use the
same bundle ID. Use something that is yours, for example a reversed domain name or your name:

    export OCPNIOS_BUNDLE_PREFIX=com.yourname.opencpnios

And your **Team ID**: Xcode › Settings › Accounts › your Apple ID › the "(Personal Team)"
team. The ten-character ID is also shown after your name by
`security find-identity -v -p codesigning`, or on the Apple Developer website.

    export OCPNIOS_TEAM_ID=ABCDE12345

Then the Release build for the device:

    scripts/build-deps-ios.sh device
    scripts/build-wxqt-ios.sh device-release all
    scripts/build-ocpn-ios.sh device-release all

Result: `build/device-release/ocpn/opencpn.app`, not yet signed.

## 5. Signing and installing

**Prepare the device** (once):
1. Connect the device to the Mac with a cable and trust the computer on the device.
2. Open Xcode › Window › Devices and Simulators, select the device and wait until Xcode has
   prepared it for development.
3. On the device, turn on **Developer Mode**: Settings › Privacy & Security › Developer
   Mode. The device then restarts.

**Get a provisioning profile** (once per device, and again after 7 days with a free account):

    scripts/make-profile.sh iphone        # or: ipad

This creates an empty Xcode project with your bundle ID, lets Xcode create a development
profile and saves it as `build/device/profielen/iphone.mobileprovision`. If it fails, see
`build/logs/profiel-iphone.log`.

**Sign and install:**

    OCPNIOS_BUILD=device-release scripts/sign-ocpn-ios.sh build/device/profielen/iphone.mobileprovision
    xcrun devicectl list devices                     # note the Identifier of your device
    xcrun devicectl device install app --device <Identifier> build/device-release/signed/opencpn.app

The device must be unlocked. Installing also works without a cable when the Mac and the
device are on the same network and the device has been paired over the cable before.

On first start, iOS asks whether OpenCPN may use the local network (for NMEA and AIS data
from your instruments) and your location (built-in GPS).

## 6. Limits of a free Apple ID

- An app installed with a free account **works for 7 days**. After that it no longer
  starts: run `make-profile.sh` again, sign and install. Your charts and settings are kept as
  long as you do not delete the app.
- At most **3 of your own apps** per device at the same time.
- A paid developer account does not have these limits and can also use TestFlight.

## 7. Making changes

- The iOS-specific code is in `app/` (own code) and in `patches/` (changes to third-party
  source). The OpenCPN changes are a series of small commits on top of `Release_5.14.2`
  (`patches/OpenCPN/0001-*.patch` and following, applied with `git am`). To change OpenCPN,
  commit in `upstream/OpenCPN` (a new small commit per topic) and regenerate the series:

      rm patches/OpenCPN/*.patch
      git -C upstream/OpenCPN format-patch --no-signature -o "$PWD/patches/OpenCPN" Release_5.14.2

  For the other projects regenerate the single patch, e.g.
  `git -C upstream/radar_pi diff > patches/radar_pi/0001-ios.patch`.

- Rebuilding after a change: `scripts/build-ocpn-ios.sh device-release build` (only what has
  changed). After a change to `app/cmake/ios-plugins.cmake`, or in a plugin with fixups:
  `... device-release all`.

Comments in the patches are in English; the script messages and the code in `app/` are
mostly in Dutch.

## 8. Troubleshooting

| message | cause and fix |
|---|---|
| `geen qt-cmake in ...` | Qt is not in `~/Qt/6.12.0` or the iOS components are missing; set `QT_ROOT` or install iOS Device/Simulator in the Qt installer. |
| `upstream/... ontbreekt` | `scripts/fetch-upstream.sh` (and then `scripts/apply-patches.sh`) has not been run. |
| `geen Apple Development-identiteit gevonden` | Sign in to Xcode with your Apple ID (Settings › Accounts) and let Xcode create a certificate; `make-profile.sh` also does this. |
| `maximum number of installed apps using a free developer profile` | Free account: remove another of your own apps from the device. |
| `unable to locate a device` when installing | Device locked, on another network or not paired; unlock it or connect it with the cable. |
| App no longer starts after a week | Free profile expired, see section 6. |

## 9. How long it takes

Measured on a Mac with M4 Pro, first build. Later builds are mostly incremental.

| step | time |
|---|---|
| `fetch-upstream.sh` | ~2 min, depending on the connection |
| `build-deps-ios.sh` (per target) | ~3 min |
| `build-wxqt-ios.sh device-release all` | ~1.5 min |
| `build-ocpn-ios.sh device-release all` | ~4.5 min |
