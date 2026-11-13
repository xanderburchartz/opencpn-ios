// Deelvenster van iOS (UIActivityViewController) voor een bestand, bv. de PDF die
// OpenCPN op iOS bij afdrukken in Documents schrijft (wxQt-patch printqt.cpp).
// Vanuit het deelvenster kan de gebruiker afdrukken (AirPrint), mailen of bewaren.
// Keuze Xander (8 nov 2026): na afdrukken het deelvenster tonen.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include "share_sheet.h"

namespace {

UIViewController* TopViewController() {
  UIWindow* key = nil;
  for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* w in ((UIWindowScene*)scene).windows) {
      if (w.isKeyWindow) { key = w; break; }
    }
    if (key) break;
  }
  UIViewController* vc = key.rootViewController;
  while (vc.presentedViewController) vc = vc.presentedViewController;
  return vc;
}

}  // namespace

void OcpnIosShareFile(const char* path) {
  if (!path || !*path) return;
  NSString* p = [NSString stringWithUTF8String:path];
  if (![[NSFileManager defaultManager] fileExistsAtPath:p]) return;
  NSURL* url = [NSURL fileURLWithPath:p];
  // Na de huidige event-ronde, zodat de afdrukcode helemaal klaar is.
  dispatch_async(dispatch_get_main_queue(), ^{
    UIViewController* top = TopViewController();
    if (!top) return;
    UIActivityViewController* avc =
        [[UIActivityViewController alloc] initWithActivityItems:@[ url ]
                                          applicationActivities:nil];
    // iPad: het deelvenster is daar een popover en heeft een ankerpunt nodig.
    UIPopoverPresentationController* pop = avc.popoverPresentationController;
    if (pop) {
      pop.sourceView = top.view;
      pop.sourceRect = CGRectMake(CGRectGetMidX(top.view.bounds),
                                  CGRectGetMidY(top.view.bounds), 1, 1);
      pop.permittedArrowDirections = 0;
    }
    [top presentViewController:avc animated:YES completion:nil];
  });
}
