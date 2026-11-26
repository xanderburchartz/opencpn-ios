// Schermafbeeldingen voor het nakijken van de layout op een echt toestel.
//
// Op een iPhone of iPad kan Claude zelf geen schermafbeelding maken; alleen bestanden uit
// de app ophalen (`xcrun devicectl device copy from`, scripts/fetch-shots.sh). Xander
// (10 nov 2026): "bouw 3 maar, want dan hebben we een structurele oplossing".
//
// Wanneer: ~0,7 s nadat een venster (blad, melding, paneel) zichtbaar wordt, en ~0,8 s na
// een losgelaten tik (dan staat het resultaat van de tik in beeld). Wat: het hele
// app-scherm, ook de kaart (OpenGL), via UIKit. Waar: Documents/log/schermafbeeldingen/ in de
// app-container, JPEG op schermmaat in punten, hooguit kMaxShots bestanden (oudste weg).
// Niets gaat het toestel uit; ophalen gebeurt alleen met de kabel/het netwerk van de Mac.
// Alleen voor eigen tests, standaard uit (besluit Xander 25 nov 2026): aan zolang de map
// Documents/log/schermafbeeldingen bestaat en er geen bestand OFF in staat, of met env
// OCPNIOS_DIAG=1 (ocpnios_TestDiagnostics in startup_diag.cpp). De app maakt die map niet zelf.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <QWidget>

bool ocpnios_TestDiagnostics();

#if __has_feature(objc_arc)
#error "screen_capture.mm gaat uit van handmatig geheugenbeheer (geen ARC)"
#endif

namespace {

constexpr int kMaxShots = 40;

// In Documents/log/schermafbeeldingen: zichtbaar in de Bestanden-app onder "OpenCPN › log",
// los van kaarten (Xander, 13 nov 2026). Eerder Library/Caches/ocpnios-shots.
NSString* ShotDir() {
  NSArray* paths =
      NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
  return [[[paths firstObject] stringByAppendingPathComponent:@"log"]
      stringByAppendingPathComponent:@"schermafbeeldingen"];
}

void Prune(NSString* dir) {
  NSFileManager* fm = [NSFileManager defaultManager];
  NSArray* names = [[fm contentsOfDirectoryAtPath:dir error:nil]
      sortedArrayUsingSelector:@selector(compare:)];
  NSMutableArray* jpgs = [NSMutableArray array];
  for (NSString* n in names)
    if ([n hasSuffix:@".jpg"]) [jpgs addObject:n];
  while ((int)jpgs.count > kMaxShots) {
    [fm removeItemAtPath:[dir stringByAppendingPathComponent:jpgs[0]] error:nil];
    [jpgs removeObjectAtIndex:0];
  }
}

void Capture(const QString& reason) {
  @autoreleasepool {
    NSString* dir = ShotDir();
    NSFileManager* fm = [NSFileManager defaultManager];
    // Elke keer kijken, zodat weghalen van de map (of OFF erin) meteen werkt.
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:dir isDirectory:&isDir] || !isDir) return;
    if ([fm fileExistsAtPath:[dir stringByAppendingPathComponent:@"OFF"]]) return;

    UIWindowScene* scene = nil;
    for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
      if ([sc isKindOfClass:[UIWindowScene class]] &&
          (!scene || sc.activationState == UISceneActivationStateForegroundActive))
        scene = (UIWindowScene*)sc;
    if (!scene || scene.activationState != UISceneActivationStateForegroundActive) return;

    const CGRect bounds = scene.coordinateSpace.bounds;
    UIGraphicsImageRendererFormat* fmt = [UIGraphicsImageRendererFormat defaultFormat];
    fmt.scale = 1.0;  // punten: genoeg om de layout te beoordelen, klein bestand
    UIGraphicsImageRenderer* r =
        [[[UIGraphicsImageRenderer alloc] initWithBounds:bounds format:fmt] autorelease];
    NSArray* windows = [scene.windows sortedArrayUsingComparator:^(UIWindow* a, UIWindow* b) {
      return a.windowLevel < b.windowLevel ? NSOrderedAscending
             : a.windowLevel > b.windowLevel ? NSOrderedDescending
                                              : NSOrderedSame;
    }];
    UIImage* img = [r imageWithActions:^(UIGraphicsImageRendererContext*) {
      for (UIWindow* w in windows) {
        if (w.hidden || w.alpha <= 0) continue;
        [w drawViewHierarchyInRect:w.frame afterScreenUpdates:YES];
      }
    }];
    NSData* jpg = UIImageJPEGRepresentation(img, 0.7);
    if (!jpg) return;

    NSDateFormatter* df = [[[NSDateFormatter alloc] init] autorelease];
    df.dateFormat = @"yyyyMMdd-HHmmss-SSS";
    NSString* stamp = [df stringFromDate:[NSDate date]];
    QString safe = reason.left(40);
    for (QChar& c : safe)
      if (!c.isLetterOrNumber()) c = QLatin1Char('_');
    NSString* name = [NSString stringWithFormat:@"%@-%@.jpg", stamp,
                                                safe.toNSString()];
    [jpg writeToFile:[dir stringByAppendingPathComponent:name] atomically:YES];
    Prune(dir);
  }
}

class CaptureFilter : public QObject {
 public:
  explicit CaptureFilter(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    QObject::connect(&m_timer, &QTimer::timeout, this, [this]() { Capture(m_reason); });
  }

  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (!obj->isWidgetType()) return false;
    auto* w = static_cast<QWidget*>(obj);
    if (ev->type() == QEvent::Show && w->isWindow()) {
      Schedule(w->windowTitle().isEmpty() ? QStringLiteral("venster") : w->windowTitle(),
               700);
    } else if (ev->type() == QEvent::MouseButtonRelease) {
      // Elke losgelaten tik, ook op een knop in een venster. Wordt hij aan meer widgets
      // aangeboden, dan start de timer gewoon opnieuw: één afbeelding per tik.
      const QString t = w->window() ? w->window()->windowTitle() : QString();
      Schedule(t.isEmpty() ? QStringLiteral("tik") : QStringLiteral("tik_") + t, 800);
    }
    return false;
  }

 private:
  void Schedule(const QString& reason, int ms) {
    m_reason = reason;
    m_timer.start(ms);  // opnieuw starten: bij snel achter elkaar alleen de laatste stand
  }
  QTimer m_timer;
  QString m_reason;
};

void InstallCapture() {
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) return;
  if (!ocpnios_TestDiagnostics()) return;  // bij het starten gekozen
  app->installEventFilter(new CaptureFilter(app));
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(InstallCapture)

// Eenmalig opruimen na de overstap naar Documents/log (13 nov 2026): pdf's van de afdrukknop
// uit de hoofdmap naar "Afdrukken", oude logbestanden naar "log", oude schermafbeeldingen in
// Library/Caches weg. Alleen verplaatsen, niets van de gebruiker weggooien.
void ocpnios_TidyDocuments() {
  @autoreleasepool {
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* docs =
        [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    NSString* lib =
        [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) firstObject];
    NSString* caches =
        [NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES) firstObject];
    if (!docs) return;

    NSString* prints = [docs stringByAppendingPathComponent:@"Afdrukken"];
    for (NSString* n in [fm contentsOfDirectoryAtPath:docs error:nil]) {
      if (![n hasPrefix:@"OpenCPN-"] || ![n.pathExtension isEqualToString:@"pdf"]) continue;
      [fm createDirectoryAtPath:prints withIntermediateDirectories:YES attributes:nil error:nil];
      [fm moveItemAtPath:[docs stringByAppendingPathComponent:n]
                  toPath:[prints stringByAppendingPathComponent:n]
                   error:nil];
    }

    NSString* logDir = [docs stringByAppendingPathComponent:@"log"];
    NSString* oldLogs = [lib stringByAppendingPathComponent:@"Application Support/opencpn"];
    for (NSString* n in @[ @"opencpn.log", @"opencpn.log.log", @"ocpnios-diag.log",
                           @"fit.log", @"scroll.log" ]) {
      NSString* from = [oldLogs stringByAppendingPathComponent:n];
      if (![fm fileExistsAtPath:from]) continue;
      [fm createDirectoryAtPath:logDir withIntermediateDirectories:YES attributes:nil error:nil];
      NSString* to = [logDir stringByAppendingPathComponent:
                                 [@"oud-" stringByAppendingString:n]];
      [fm removeItemAtPath:to error:nil];
      [fm moveItemAtPath:from toPath:to error:nil];
    }

    if (caches)
      [fm removeItemAtPath:[caches stringByAppendingPathComponent:@"ocpnios-shots"] error:nil];
  }
}
