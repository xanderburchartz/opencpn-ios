# o-charts on iOS: what the decryption library needs to provide

Status 13 Nov 2026. Technical note for the conversation with o-charts. Based on the open
source of o-charts_pi 2.2.5.0 (GPL-2.0, github.com/bdbcat/o-charts_pi, commit `cc4d4e75`).
The closed part (oexserverd, libsgl) has not been examined or analysed; everything below
follows from what the open plugin asks of oexserverd and what it expects back.

## Starting point
- The o-charts_pi plugin is built into this iOS app (statically, like every plugin on iOS)
  and loads; its screens work.
- At o-charts, decryption and licence checking live in the program `oexserverd` plus the
  library `libsgl`. On iOS an app may not start a program of its own, so that cannot run
  there.
- For that reason the coupling with oexserverd is a single C interface in this app:
  [`app/ios/ocharts/ocharts_ios_backend.h`](../app/ios/ocharts/ocharts_ios_backend.h).
  o-charts would supply the implementation as a static library or xcframework; it is linked
  in with the CMake option `OCPNIOS_OCHARTS_BACKEND=<path>`. Downloading code afterwards is
  not allowed on iOS, so the library ships inside the app bundle.
- Without that library the app links a stub
  ([`ocharts_backend_stub.cpp`](../app/ios/ocharts/ocharts_backend_stub.cpp)). The plugin
  then reports "o-charts decryption is not available on iOS" and makes no connection to
  the o-charts shop.

## What the plugin currently asks of oexserverd
The plugin uses two paths. In the iOS app both go unchanged to the interface: the same
arguments, the same messages, the same answers. Only the transport differs.

### 1. Command-line calls (`ocios_exec`)
Synchronous; the plugin reads the output lines.

| argument | purpose | where in the plugin | expected output |
|---|---|---|---|
| `-a` | server version | `fpr.cpp` GetServerVersionString, `validator.cpp` | one line, longer than 2 characters |
| `-s` | dongle present | `fpr.cpp` IsDongleAvailable | `1` or `0` |
| `-t` | dongle serial number | `fpr.cpp` GetDongleSN | number |
| `-v <key>` | expiry date of a key | `fpr.cpp` getExpDate | one line |
| `-g <dir>` | create the fingerprint (FPR) of this device | `fpr.cpp` getFPR | line `FPR:<path>`; `ERROR` on failure |
| `-k <dir>` | same, for the SGLock dongle | `fpr.cpp` getFPR | same |
| `-y '<text>'` | extra information with `-g`/`-k` | `fpr.cpp` getFPR | |

There is no USB dongle (SGLock) on iOS; `-s` should return `0`. `-m` (TPM) and `-b` (TPM
off) are used by the plugin on Linux only. On Android the plugin creates the fingerprint
with `-y <WVID> -k` and reads it from stdout (`ochartShop.cpp` doUploadXFPR, prefix
`oc04R_`); on iOS the plugin currently follows the macOS/Linux path with a file.

### 2. Server requests (`ocios_request`, `ocios_stream_read`, `ocios_stream_close`)
On macOS/Linux the plugin starts `oexserverd` as a background process (on iOS:
`ocios_backend_start`) and sends requests over the FIFO `/tmp/OCPN_PIPEX`. Each request
is one struct; the answer comes back as a byte stream.

| struct (header) | command | purpose |
|---|---|---|
| `fifo_msg` (`Osenc.h`) | `CMD_TEST_AVAIL` (1) | server available; answer `OK` |
| | `CMD_READ_ESENC` (0) | read an encrypted vector chart (eSENC/oesu) |
| | `CMD_READ_ESENC_HDR` (3) | only its header |
| | `CMD_GET_HK` (6) | `getHK()` in `oernc_inStream.cpp`; meaning of the answer not examined |
| | `CMD_EXIT` (2) | stop the server |
| `rnc_fifo_msg` (`oernc_inStream.h`) | `CMD_OPEN_RNC` (4) / `CMD_OPEN_RNC_FULL` (5) | raster chart (oernc), header or full |

`ocios_request` receives the struct byte for byte and returns a stream with exactly what
oexserverd would write into the private FIFO. The library does not need to open the
`fifo_name` field. The in-app transport is in
[`ocharts_ios_pipe.cpp`](../app/ios/ocharts/ocharts_ios_pipe.cpp).

### Requirements for the library
- C linkage (`extern "C"`), arm64 for device and simulator (xcframework), iOS 18 or
  later, without its own copy of wxWidgets or Qt (both are already in the app).
- Thread-safe: OpenCPN also opens charts in worker threads; several streams at once.
- No processes of its own, no dlopen of code outside the bundle.
- `ocios_backend_abi_version()` returns `OCIOS_BACKEND_ABI` (currently 1).

## Device identification (the open question)
On the desktop, oexserverd with libsgl creates a fingerprint of the hardware; `fpr.cpp`
only calls `-g`/`-k` and uploads the file. On Android the app itself passes data to
oexserverd: a UUID and the Widevine ID (`-z`, `-y`, see `o-charts_pi.cpp` Init and
validate_SENC_server), because Android shields fixed hardware IDs.

iOS gives apps no fixed hardware ID (no serial number, IMEI or MAC address). What is
available (according to Apple's documentation; not tested in this app):

| mechanism | stays the same on | changes on |
|---|---|---|
| `identifierForVendor` | app update | removal of all apps from the same developer |
| own UUID in the Keychain (this device only) | reinstall | erasing the device, or restoring it to a different device |
| DeviceCheck (2 bits per device, held by Apple) | | not examined |
| App Attest (key in the Secure Enclave, signed by Apple) | | reinstall (new key) |

Widevine does not exist on iOS. The question for o-charts is therefore which
identification they accept for a per-device licence, and how a user continues after a new
device or a reinstall. That determines what `-g` does on iOS; the interface does not need
to change for it, the library can call the iOS APIs itself.

## Other open points
- **Licence:** a closed library in an app with GPL code (OpenCPN, this plugin) is a legal
  question for o-charts and the OpenCPN team together. On the desktop, oexserverd is a
  separate program next to the GPL plugin; on iOS everything is in one bundle.
- **Traffic with the shop:** as long as there is no library, the app blocks every
  connection from the plugin to o-charts.org (`libs/wxcurl/src/base.cpp`, patch). No
  login has taken place and no device has been registered.
- **Raster charts (oernc)** follow the same path; not tested separately.

## What is ready on the app side
- `patches/o-charts_pi/0001-ios.patch`: iOS branches in the plugin (transport, GL ES,
  wx 3.3 adjustments, message, blocking of shop traffic) and the Dutch translation of the
  message.
- `app/cmake/ios-ocharts.cmake`: build of the plugin and choice between stub and library.
  On by default since 12 Nov 2026; off with `-DOCPNIOS_WITH_OCHARTS=OFF`. With a library:
  `-DOCPNIOS_OCHARTS_BACKEND=<path>`.
- `app/ios/ocharts/`: interface, stub, glue on the plugin side.

## What has been tested (simulator, iPhone 17 Pro Max, 11 and 12 Nov 2026)
| item | result |
|---|---|
| loading and enabling the plugin, plugin list | loads; listed with icon and Dutch text |
| start with the stub | `-s` goes to the stub, "No Dongle detected", plugin keeps working |
| shop panel (Options › Charts › o-charts) | opens; "Refresh chart list" shows the message, no network traffic in the log |
| Preferences › create fingerprint | shows the message (at first wrongly "Fingerprint file created", fixed) |
| USB dongle button | hidden on iOS (no dongle possible) |
| fake `.oesu` in a chart folder | plugin refuses before decryption ("RInstallKey not found"), no crash |
| GRIB, WeatherRouting alongside o-charts | work |
| restarts with o-charts on and off | no crash |

Not tested: opening a real o-charts chart (needs a real key and the library), the blocking
in `libs/wxcurl` (not reached from the current screens, the button blocks earlier), iPad,
night mode. Device: not covered by this note.

## Questions for o-charts
1. Would you supply the decryption as an in-app library for iOS, against this interface
   (or a proposal of your own)?
2. Which device identification do you accept on iOS, and what happens after a new device
   or a reinstall?
3. How do you see the combination of a closed library with the GPL code in one app bundle
   (together with the OpenCPN team)?
4. May we test against your test environment before anything goes to testers?
