// Sensoren van het toestel als NMEA 0183 voor OpenCPN's verbindingstype "Ingebouwde GPS".
//
// OpenCPN heeft daarvoor een driver (CommDriverN0183AndroidInt) die op Android via Java
// zinnen krijgt. Op iOS levert dit bestand dezelfde twee functies (androidStartGPS/
// androidStopGPS) en dezelfde events, met:
//   - Core Location: positie, SOG, COG     -> $GPRMC, $GPGGA
//   - kompas (alleen als ingesteld)         -> $HCHDM, $HCHDT
//   - barometer (CMAltimeter)               -> $IIXDR,P,<bar>,B,Barometer
// Het kompas meet de stand van het toestel, niet van de boot. Alleen zinvol als het toestel
// vast in de lengterichting zit; daarom standaard uit: config [Settings] IOSInternalHeading=1.
// Op de achtergrond blijft de GPS lopen zolang de verbinding aan staat (UIBackgroundModes
// "location" in Info.plist); iOS toont dan de blauwe locatie-aanduiding.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/config.h>

#include <cmath>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "model/comm_drv_n0183_android_int.h"

#import <CoreLocation/CoreLocation.h>
#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

static wxEvtHandler* s_consumer = nullptr;

static void SendSentence(const std::string& body) {
  if (!s_consumer) return;
  unsigned char cs = 0;
  for (char c : body) cs ^= (unsigned char)c;
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
  std::string s = "$" + body + tail;
  auto buf = std::make_shared<std::vector<unsigned char>>(s.begin(), s.end());
  CommDriverN0183AndroidIntEvent ev(wxEVT_COMMDRIVER_N0183_ANDROID_INT, 0);
  ev.SetPayload(buf);
  s_consumer->AddPendingEvent(ev);
}

static std::string LatLon(double deg, bool lat) {
  char hemi = lat ? (deg >= 0 ? 'N' : 'S') : (deg >= 0 ? 'E' : 'W');
  deg = std::fabs(deg);
  int d = (int)deg;
  double m = (deg - d) * 60.0;
  char buf[32];
  if (lat)
    snprintf(buf, sizeof(buf), "%02d%07.4f,%c", d, m, hemi);
  else
    snprintf(buf, sizeof(buf), "%03d%07.4f,%c", d, m, hemi);
  return buf;
}

static void SendFix(CLLocation* loc) {
  if (!loc || loc.horizontalAccuracy < 0) return;
  time_t t = (time_t)loc.timestamp.timeIntervalSince1970;
  struct tm u;
  gmtime_r(&t, &u);
  double frac = loc.timestamp.timeIntervalSince1970 - (double)t;
  char hms[16], dmy[8];
  snprintf(hms, sizeof(hms), "%02d%02d%05.2f", u.tm_hour, u.tm_min, u.tm_sec + frac);
  snprintf(dmy, sizeof(dmy), "%02d%02d%02d", u.tm_mday, u.tm_mon + 1, u.tm_year % 100);

  std::string pos = LatLon(loc.coordinate.latitude, true) + "," +
                    LatLon(loc.coordinate.longitude, false);
  char sog[16] = "", cog[16] = "";
  if (loc.speed >= 0) snprintf(sog, sizeof(sog), "%.1f", loc.speed * 3600.0 / 1852.0);
  if (loc.course >= 0) snprintf(cog, sizeof(cog), "%.1f", loc.course);

  SendSentence(std::string("GPRMC,") + hms + ",A," + pos + "," + sog + "," + cog + "," +
               dmy + ",,,A");
  // GGA: aantal satellieten geeft iOS niet; HDOP benaderd als nauwkeurigheid/5 m.
  char rest[64];
  snprintf(rest, sizeof(rest), ",1,,%.1f,%.1f,M,,M,,", loc.horizontalAccuracy / 5.0,
           loc.verticalAccuracy >= 0 ? loc.altitude : 0.0);
  SendSentence(std::string("GPGGA,") + hms + "," + pos + rest);
}

@interface OcpnIosGps : NSObject <CLLocationManagerDelegate>
@property(nonatomic, strong) CLLocationManager* manager;
@property(nonatomic, strong) CMAltimeter* altimeter;
@property(nonatomic, strong) CLLocation* last;
@property(nonatomic, strong) NSDate* lastSent;
@property(nonatomic, strong) NSTimer* timer;
@property(nonatomic) BOOL heading;
@end

@implementation OcpnIosGps
- (void)start {
  long h = 0;
  if (wxConfigBase* c = wxConfigBase::Get(false))
    c->Read("/Settings/IOSInternalHeading", &h, 0L);
  self.heading = h != 0;

  self.manager = [[CLLocationManager alloc] init];
  self.manager.delegate = self;
  self.manager.desiredAccuracy = kCLLocationAccuracyBest;
  self.manager.distanceFilter = kCLDistanceFilterNone;
  self.manager.activityType = CLActivityTypeOtherNavigation;
  self.manager.pausesLocationUpdatesAutomatically = NO;
  [self.manager requestWhenInUseAuthorization];
  // Alleen toegestaan met UIBackgroundModes "location" in Info.plist (app/CMakeLists.txt).
  NSArray* modes = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"UIBackgroundModes"];
  if ([modes containsObject:@"location"]) {
    self.manager.allowsBackgroundLocationUpdates = YES;
    self.manager.showsBackgroundLocationIndicator = YES;
  }
  [self.manager startUpdatingLocation];
  if (self.heading && [CLLocationManager headingAvailable]) {
    self.manager.headingFilter = 1.0;
    [self.manager startUpdatingHeading];
  }

  if ([CMAltimeter isRelativeAltitudeAvailable]) {
    self.altimeter = [[CMAltimeter alloc] init];
    [self.altimeter startRelativeAltitudeUpdatesToQueue:[NSOperationQueue mainQueue]
                                            withHandler:^(CMAltitudeData* d, NSError* e) {
      if (!d) return;
      double bar = d.pressure.doubleValue / 100.0;  // kPa -> bar
      char b[64];
      snprintf(b, sizeof(b), "IIXDR,P,%.5f,B,Barometer", bar);
      SendSentence(b);
    }];
  }

  // Stilliggend levert iOS soms minder vaak een positie; laatste fix elke seconde
  // herhalen zolang hij jonger is dan 30 s (met zijn eigen tijdstip).
  self.timer = [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer* t) {
    if (!self.last) return;
    if (self.lastSent && -[self.lastSent timeIntervalSinceNow] < 1.5) return;
    if (-[self.last.timestamp timeIntervalSinceNow] > 30) return;
    SendFix(self.last);
    self.lastSent = [NSDate date];
  }];
  wxLogMessage("iOS interne GPS gestart (kompas %s, barometer %s)", self.heading ? "aan" : "uit",
               self.altimeter ? "aan" : "niet beschikbaar");
}

- (void)stop {
  [self.timer invalidate];
  self.timer = nil;
  [self.manager stopUpdatingLocation];
  [self.manager stopUpdatingHeading];
  self.manager.delegate = nil;
  self.manager = nil;
  [self.altimeter stopRelativeAltitudeUpdates];
  self.altimeter = nil;
}

- (void)locationManager:(CLLocationManager*)m didUpdateLocations:(NSArray<CLLocation*>*)locs {
  CLLocation* loc = locs.lastObject;
  if (!loc) return;
  self.last = loc;
  SendFix(loc);
  self.lastSent = [NSDate date];
}

- (void)locationManager:(CLLocationManager*)m didUpdateHeading:(CLHeading*)h {
  if (!self.heading || h.headingAccuracy < 0) return;
  char b[48];
  snprintf(b, sizeof(b), "HCHDM,%.1f,M", h.magneticHeading);
  SendSentence(b);
  if (h.trueHeading >= 0) {
    snprintf(b, sizeof(b), "HCHDT,%.1f,T", h.trueHeading);
    SendSentence(b);
  }
}

- (void)locationManager:(CLLocationManager*)m didFailWithError:(NSError*)e {
  wxLogMessage("iOS interne GPS: %s", e.localizedDescription.UTF8String);
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)m {
  wxLogMessage("iOS interne GPS: toestemming %d", (int)m.authorizationStatus);
}
@end

static OcpnIosGps* s_gps = nil;

bool androidStartGPS(wxEvtHandler* consumer) {
  s_consumer = consumer;
  if (!s_gps) s_gps = [[OcpnIosGps alloc] init];
  [s_gps start];
  return true;
}

bool androidStopGPS() {
  [s_gps stop];
  s_gps = nil;
  s_consumer = nullptr;
  return true;
}
