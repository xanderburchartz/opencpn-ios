// iOS-documentkiezer voor wxFileDialog (wxFD_OPEN).
//
// QIOSFileDialog presenteert zich op de rootViewController. Stond daar al iets op (een
// blad of actieblad), dan verscheen er niets en bleef QFileDialog::exec in zijn eigen lus
// hangen: WeatherRouting > Bestand > Openen liep vast (12 nov 2026). Deze kiezer toont
// zich op de bovenste controller en wacht in de iOS-runloop.
// Aangeroepen vanuit wxFileDialog::ShowModal (wxWidgets-patch, src/qt/filedlg.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <QDir>
#include <QStandardPaths>
#include <QString>

@interface OcpniosPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) BOOL done;
@property(nonatomic, strong) NSURL* picked;
@end

@implementation OcpniosPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
  self.picked = urls.firstObject;
  self.done = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
  self.picked = nil;
  self.done = YES;
}
@end

namespace {
UIViewController* TopController() {
  // Het Qt-hoofdvenster, niet het toetsenbordvenster (UITextEffectsWindow): dat kan key
  // zijn, en een kiezer daarop verscheen niet (12 nov 2026).
  UIWindow* main = nil;
  for (UIScene* sc in UIApplication.sharedApplication.connectedScenes) {
    if (![sc isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* w in ((UIWindowScene*)sc).windows) {
      if (w.windowLevel != UIWindowLevelNormal || !w.rootViewController) continue;
      if ([NSStringFromClass([w class]) hasPrefix:@"UITextEffects"]) continue;
      if (!main || w.isKeyWindow) main = w;
    }
  }
  UIViewController* vc = main.rootViewController;
  while (vc.presentedViewController && !vc.presentedViewController.isBeingDismissed)
    vc = vc.presentedViewController;
  return vc;
}
}  // namespace

static bool PickImpl(const QString& title, const QString& dir, QString* out, bool folder);

bool ocpnios_pick_open_file(const QString& title, const QString& dir, QString* out) {
  return PickImpl(title, dir, out, false);
}

// Map kiezen (wxDirDialog), bijv. Opties › Kaarten › Kaartbestanden › Voeg toe (13 nov 2026).
bool ocpnios_pick_folder(const QString& title, const QString& dir, QString* out) {
  return PickImpl(title, dir, out, true);
}

static bool PickImpl(const QString& title, const QString& dir, QString* out, bool folder) {
  UIViewController* top = TopController();
  if (!top || !out) return false;

  UIDocumentPickerViewController* picker =
      [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ folder ? UTTypeFolder : UTTypeItem ]
                                                                  asCopy:NO];
  picker.allowsMultipleSelection = NO;
  // Begin in de map van de dialoog, anders in Documents van de app.
  QString start = dir;
  if (start.isEmpty() || !QDir(start).exists())
    start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  if (!start.isEmpty()) picker.directoryURL = [NSURL fileURLWithPath:start.toNSString()];
  if (!title.isEmpty()) picker.title = title.toNSString();

  OcpniosPickerDelegate* del = [[OcpniosPickerDelegate alloc] init];
  picker.delegate = del;

  // De documentkiezer presenteert zich vertraagd en heeft daarvoor de hoofdwachtrij van
  // iOS nodig (XPC naar de bestandsdienst). In een QEventLoop loopt die niet: de kiezer
  // verscheen nooit, ook de kiezer van Qt zelf niet (12 nov 2026). Daarom wachten in de
  // iOS-runloop; Qt-gebeurtenissen lopen daarin mee.
  [top presentViewController:picker animated:YES completion:nil];
  NSDate* giveUp = [NSDate dateWithTimeIntervalSinceNow:5.0];
  while (!del.done) {
    @autoreleasepool {
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
    }
    // Niet in beeld gekomen (bijv. een ander blad sluit nog): opnieuw proberen, en
    // na 5 s opgeven in plaats van eindeloos te wachten.
    if (!picker.presentingViewController && !del.done) {
      if ([giveUp timeIntervalSinceNow] < 0) break;
      UIViewController* t = TopController();
      if (t && !t.isBeingDismissed && !t.presentedViewController)
        [t presentViewController:picker animated:YES completion:nil];
    }
  }

  NSURL* url = del.picked;
  if (!url) return false;
  // Bestand buiten de eigen container (iCloud, andere app): toegang openhouden; de
  // plugin leest het bestand direct na terugkeer.
  [url startAccessingSecurityScopedResource];
  *out = QString::fromNSString(url.path);
  return !out->isEmpty();
}
