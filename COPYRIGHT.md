# Copyright and licences

OpenCPN for iOS is an unofficial port of [OpenCPN](https://opencpn.org) to iPhone and iPad.
The app as a whole is distributed under the **GNU General Public License, version 3**
(see [LICENSE](LICENSE)). The reasons are explained in
[docs/licenties-verspreiding.md](docs/licenties-verspreiding.md), which is in Dutch. In short:

- All GPL code in the app is licensed "version 2 or later" or "version 3 or later".
- OpenSSL (Apache 2.0) and Qt (LGPLv3) are compatible with GPLv3 but not with GPLv2, so the
  combined work is GPLv3.

The code in this repository (`app/`, `scripts/`, `cmake/`, `spikes/` and the patches in
`patches/`) is GPL-2.0-or-later, the same as OpenCPN itself.

## Components

Third-party source is not stored in this repository. `scripts/fetch-upstream.sh` downloads
each component at a fixed commit (`scripts/fetch-upstream.sh --lijst` prints the list), and
`scripts/apply-patches.sh` applies the iOS changes from `patches/`.

| Component | Version | Licence |
|---|---|---|
| OpenCPN | 5.14.2 | GPL-2.0-or-later; bundled data files under the terms listed in OpenCPN's `data/license.txt` |
| wxWidgets (wxQt port) | 3.3.4 | wxWindows Library Licence 3.1 (LGPL-2.0-or-later with an exception for binary distribution) |
| Qt (Core, Gui, Widgets, OpenGLWidgets, PrintSupport, Svg, image formats, iOS platform plugin) | 6.12.0 | LGPL-3.0, statically linked; see "Qt and static linking" below |
| OpenSSL | 3.5.9 | Apache-2.0 |
| curl | 8.23.0 | curl licence (MIT-style) |
| shapelib | 1.6.1 | MIT or LGPL-2.0-or-later (dual licence) |
| RapidJSON | 1.1.0 | MIT |
| Plugins: dashboard, GRIB, WMM, chart downloader (part of OpenCPN) | 5.14.2 | GPL-2.0-or-later |
| radar_pi | 5.7.2 | GPL-2.0-or-later |
| vdr_pi, polar_pi, tactics_pi, raymarine_autopilot_pi | see `fetch-upstream.sh` | GPL-2.0-or-later |
| weather_routing_pi, watchdog_pi, pypilot_pi, autopilot_route_pi | see `fetch-upstream.sh` | mix of GPL-2.0-or-later and GPL-3.0-or-later files |
| navico_autopilot_pi | see `fetch-upstream.sh` | GPL-3.0-or-later |
| o-charts_pi (plugin source only) | 2.2.5.0 | GPL-2.0-or-later |

The licence text of each component is in its own source tree after `fetch-upstream.sh` has
run (for example `upstream/wxWidgets/docs/licence.txt` and `upstream/openssl/LICENSE.txt`).

**o-charts:** this app does **not** include `oexserverd`, the closed-source decryption
service of o-charts. Only the GPL plugin source is built, with a stub in place of the
service. Encrypted o-charts charts therefore cannot be displayed. The app shows a message
saying so.

## Qt and static linking

iOS does not allow apps to load their own dynamic libraries, so Qt is linked statically.
LGPLv3 then requires that users can modify Qt, relink the app and run the result. This
repository provides for that:

- the complete source of the app, including all patches and build scripts;
- fixed commits for every third-party component;
- step-by-step build and installation instructions:
  [docs/zelf-bouwen.en.md](docs/zelf-bouwen.en.md). An iPhone or iPad can be used with a free
  Apple ID.

## Not affiliated

This port is not an official OpenCPN release. The OpenCPN project and the plugin authors
are not responsible for it. Report problems with the iOS app here, not to them.
