# Changelog

Changes to the unofficial iOS port of OpenCPN, per TestFlight build. All builds so far are
OpenCPN 5.14.2. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

## [Build 9] - 2026-11-26

### Fixed
- Scrolling with two fingers on a trackpad or with a mouse wheel (iPad with a trackpad,
  or the iPad app on a Mac) did nothing. Lists and settings pages now follow the fingers
  and the chart pans.
- Pinching on a trackpad now zooms the chart.
- The first tap right after closing a window (for example on the toolbar after Done) was
  often ignored. Only a tap on the spot where the window was closed is held back now.
- Number and position fields (for example latitude and longitude of a mark) got the letter
  keyboard with word suggestions. They now get the numbers keyboard, without suggestions.
- After closing the font picker the keyboard came up without a tap on a field.
- Tapping a field low on a settings page pushed the whole window up, with the header off
  screen and a black area above the keyboard. The page itself now scrolls the field into view.
- During startup the desktop status line showed for a few seconds.
- After an app update the alarm sounds (AIS, DSC, SART, anchor) and the basemap pointed to
  the previous installation.
- On a Mac ("Designed for iPad") an empty grey strip showed above the chart.
- Opening Options > Toolbar a second time could crash the app.
- A square text box "Course Up / GNSS (0 satellites)" could appear on the chart, also in
  the middle of the screen after rotating, and could not be closed.
- The compass icon became very small when the chart was turned.
- After rotating the device the toolbar, zoom buttons and status box stood for a moment
  at their old place; they now move along at once.
- On iPhone the chart scale next to the zoom buttons disappeared after rotating to
  landscape and back. It now shows on every screen.
- On a settings page that shows extra rows after a choice (own ship at real scale) the new
  rows ran over the rows below and the page did not scroll.

### Changed
- The compass and GPS status icons at the top right are at least 44 pt, large enough to tap.
- In Options the back button names the previous page whenever the name fits, also on iPad.
- About OpenCPN no longer shows file paths. "Send" next to the log file opens the share sheet.
- The status bar is in iOS style and follows the day, dusk and night colours. Options >
  User Interface > Status Bar chooses between a bar at the bottom and a floating box on the
  chart, and which values it shows: SOG, COG, heading, position, distance and bearing to the
  next waypoint. SOG and COG by default.
- The chart bar only shows when more than one chart is available at the current position.
- New toolbar in iOS style with OpenCPN's own icons, 44 pt buttons, no collapse button.
  What does not fit goes behind "More". Swipe it to the left to hide it; a tab at the edge
  brings it back. Options > Toolbar switches each button and each plugin button on or off,
  and chooses between always showing the toolbar and hiding it automatically (back with a
  tap on the chart). MOB is always there. Print and Help are off by default.
- The zoom buttons at the bottom right and the compass and GPS box at the top right are in
  the same iOS style: zoom in and out, follow ship and the chart display options, and the
  chart scale next to them. The compass box shows a north arrow with the orientation in a
  word (North, Course, Head; tap to change) and the GPS status with a word: "No fix", "GPS"
  when the satellite data is old, or the number of satellites, in red, grey, yellow or
  green.
- Messages such as "No tide data" look like an iOS alert: rounded, text centred, full-width
  buttons, no frame.
- Progress windows (updating the chart database, preparing charts, downloads) look like an
  iOS alert too: title on top, the file name instead of the full path, a thin progress bar,
  no elapsed and remaining time.
- Options work like the iOS Settings app: a change counts at once, there is no Apply and
  no Cancel, Done closes. Other windows lost their Apply button; Done applies.
- Settings and other sheets have rounded corners and leave a strip of the chart visible.
  On iPad they are a centred sheet, also in landscape, instead of filling the screen.
- The notification bell sits in the compass box at the top right and only shows when
  there are notifications, in the colour of the most serious one. The list of
  notifications is an iOS sheet with Done; each notification shows its time and an OK.
- The status box with SOG and COG is a floating box by default, in the bottom left corner
  and clear of the toolbar. Drag it anywhere; it keeps its place after rotating and
  restarting. The bar at the bottom is still a choice in Options.
- The Navico autopilot panel floats again, next to the toolbar, and can be dragged.
  Panels at the bottom of the screen have rounded corners.
- The toolbar's auto-hide is the same setting as "Hide toolbar after ... seconds" in
  Options > User Interface; on iOS the default is 5 seconds.
- Buttons on the chart and the toolbar tab have names for VoiceOver.

## [Build 8] - 2026-11-25

### Added
- First build that can be tested outside the internal group, through a public TestFlight link
  once Apple has reviewed it.

### Changed
- Screenshots and the diagnostic log are off by default. They are only for the developer's own
  tests.

## [Build 7] - 2026-11-25

### Added
- Radar through a mayara-server on the boat's network (mayara_pi plugin), in its own window or as
  an overlay on the chart.
- Data sources that use UDP broadcast or multicast now work. iOS requires a special permission for
  this, which the app now has.

## [Build 6] - 2026-11-24

### Fixed
- Crash when the app went to the background and the device was rotated.
- After rotating back to portrait, the zoom bar stood away from the edge of the screen.

## [Build 5] - 2026-11-24

### Changed
- In landscape, the toolbar and zoom bar sit against the edge of the screen and keep clear of the
  camera island only on the side where it is.

### Fixed
- The status box with the bell, compass and GPS icons ended up partly off screen after rotating.
- The follow options in the MMSI editor overlapped. They are now listed one below the other.

## [Build 4] - 2026-11-24

### Changed
- The connection editor shows all fields at once; the "Advanced" section is gone. Sentence filters
  have their own page.
- Settings that apply to all connections are grouped, with output to a GPS or autopilot in its own
  group.
- Source priorities: tap Edit to reorder sources; otherwise the list scrolls.
- Option pages with switches, values and long labels are laid out for a narrow screen.
- OpenGL options, the MMSI list, tides and chart folders fit the screen without sideways scrolling.

### Fixed
- Fields in the UDP connection editor could not be reached by scrolling.

## [Build 3] - 2026-11-24

### Changed
- The connection list works like the iOS Wi-Fi list: tap to edit, swipe left to delete, a switch to
  turn a connection on or off, and a status line ("No data", "Receiving", "Unreachable").
- The connection editor puts TCP, UDP, GPSD and Signal K in one row, with the usual OpenCPN ports.
- Source priorities show each source on two lines.

### Fixed
- Crash when closing the source priorities.
- On the iPad, the keyboard popped up when opening the source priorities.

## [Build 2] - 2026-11-24

### Added
- First TestFlight build. OpenCPN 5.14.2 on iPhone and iPad, adapted for touch and small screens:
  vector charts (S-57 ENC and CM93), a world basemap, charts from the chart downloader or the Files
  app, the device's GPS, and NMEA 0183, AIS, NMEA 2000 through a gateway, and Signal K from the
  boat's network.
- Built-in plugins, all off until turned on: dashboard, GRIB weather, WMM, chart downloader,
  weather routing, watchdog, VDR, polar, tactics, radar, Raymarine and Navico autopilot, pypilot
  and autopilot route. The o-charts plugin is included, but encrypted o-charts charts don't work on
  iOS yet.

## [Build 1] - 2026-11-24

Not released. Apple's processing rejected it because of a graphics function that iOS does not
allow apps to use. Fixed in build 2.
