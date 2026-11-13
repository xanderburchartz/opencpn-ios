// Bezig-melding tijdens het opstarten.
//
// Na het eerste venster duurt het opstarten op de iPad ~6,5 s (kaartdatabase, plugins,
// OnInitTimer; gemeten 10 nov 2026, iPad Pro 12,9" 4e gen.). Zonder teken van leven leek de
// app te hangen (Xander). Een eigen UIWindow boven de Qt-vensters met een draaiend
// wieltje en "OpenCPN wordt gestart…"; het wieltje is een Core Animation-animatie en
// draait dus ook als de hoofdthread bezig is. Weg zodra OnInitTimer klaar is.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>

#if __has_feature(objc_arc)
#error "startup_busy.mm gaat uit van handmatig geheugenbeheer (geen ARC)"
#endif

static UIWindow* s_busyWindow = nil;

void ocpnios_ShowStartupBusy() {
  if (s_busyWindow) return;
  UIWindowScene* scene = nil;
  for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
    if ([sc isKindOfClass:[UIWindowScene class]]) {
      scene = (UIWindowScene*)sc;
      break;
    }
  if (!scene) return;

  UIWindow* w = [[UIWindow alloc] initWithWindowScene:scene];
  w.windowLevel = UIWindowLevelAlert + 1;
  UIViewController* vc = [[[UIViewController alloc] init] autorelease];
  UIView* root = vc.view;
  root.backgroundColor = [UIColor systemGroupedBackgroundColor];

  UIActivityIndicatorView* spin = [[[UIActivityIndicatorView alloc]
      initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge] autorelease];
  spin.translatesAutoresizingMaskIntoConstraints = NO;
  [spin startAnimating];
  [root addSubview:spin];

  NSString* lang = [NSLocale preferredLanguages].firstObject ?: @"en";
  UILabel* label = [[[UILabel alloc] init] autorelease];
  label.text = [lang hasPrefix:@"nl"] ? @"OpenCPN wordt gestart…" : @"Starting OpenCPN…";
  label.textColor = [UIColor secondaryLabelColor];
  label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  [root addSubview:label];

  [NSLayoutConstraint activateConstraints:@[
    [spin.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
    [spin.centerYAnchor constraintEqualToAnchor:root.centerYAnchor constant:-16],
    [label.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
    [label.topAnchor constraintEqualToAnchor:spin.bottomAnchor constant:16],
  ]];

  w.rootViewController = vc;
  w.hidden = NO;
  s_busyWindow = w;  // +1 van alloc
  // Meteen tonen: de hoofdthread gaat hierna een tijd door zonder de runloop.
  [CATransaction flush];
}

void ocpnios_HideStartupBusy() {
  if (!s_busyWindow) return;
  s_busyWindow.hidden = YES;
  [s_busyWindow release];
  s_busyWindow = nil;
}

// Bezig-melding tijdens werk op de hoofdthread (AbstractPlatform::ShowBusySpinner, o.a. het
// voorbereiden van een nieuwe ENC-kaart, 13 nov 2026). Klein, in het midden, over de app.
// Pas zichtbaar na 0,5 s: korte klussen (bijv. kaartcellen bij het scrollen) gaven anders
// steeds een flits "Bezig…" (Xander, 13 nov 2026). De hoofdthread is tijdens het werk
// bezet, dus geen timer: Core Animation laat het venster zelf na 0,5 s invagen; is het werk
// eerder klaar, dan gaat het weg voor het zichtbaar werd.
static UIWindow* s_hudWindow = nil;
static const double kHudDelay = 0.5;
static const double kHudFade = 0.2;

static UIWindow* MakeHudWindow() {
  UIWindowScene* scene = nil;
  for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
    if ([sc isKindOfClass:[UIWindowScene class]]) {
      scene = (UIWindowScene*)sc;
      break;
    }
  if (!scene) return nil;

  UIWindow* w = [[UIWindow alloc] initWithWindowScene:scene];
  w.windowLevel = UIWindowLevelAlert + 1;
  w.backgroundColor = [UIColor clearColor];
  w.userInteractionEnabled = NO;
  UIViewController* vc = [[[UIViewController alloc] init] autorelease];
  UIView* root = vc.view;
  root.backgroundColor = [UIColor clearColor];

  UIVisualEffectView* box = [[[UIVisualEffectView alloc]
      initWithEffect:[UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemMaterial]] autorelease];
  box.layer.cornerRadius = 14;
  box.layer.masksToBounds = YES;
  box.translatesAutoresizingMaskIntoConstraints = NO;
  [root addSubview:box];

  UIActivityIndicatorView* spin = [[[UIActivityIndicatorView alloc]
      initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge] autorelease];
  spin.translatesAutoresizingMaskIntoConstraints = NO;
  [spin startAnimating];
  [box.contentView addSubview:spin];

  NSString* lang = [NSLocale preferredLanguages].firstObject ?: @"en";
  UILabel* label = [[[UILabel alloc] init] autorelease];
  label.text = [lang hasPrefix:@"nl"] ? @"Bezig…" : @"Working…";
  label.textColor = [UIColor secondaryLabelColor];
  label.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  [box.contentView addSubview:label];

  [NSLayoutConstraint activateConstraints:@[
    [box.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
    [box.centerYAnchor constraintEqualToAnchor:root.centerYAnchor],
    [box.widthAnchor constraintEqualToConstant:140],
    [box.heightAnchor constraintEqualToConstant:120],
    [spin.centerXAnchor constraintEqualToAnchor:box.centerXAnchor],
    [spin.topAnchor constraintEqualToAnchor:box.topAnchor constant:28],
    [label.centerXAnchor constraintEqualToAnchor:box.centerXAnchor],
    [label.topAnchor constraintEqualToAnchor:spin.bottomAnchor constant:14],
  ]];
  w.rootViewController = vc;
  return w;
}

void ocpnios_ShowBusyHud() {
  if (s_busyWindow) return;
  if (!s_hudWindow) s_hudWindow = MakeHudWindow();  // eenmalig, daarna hergebruikt
  if (!s_hudWindow) return;
  if (!s_hudWindow.hidden) return;

  // Niet de laag van het UIWindow zelf (de wortel van zijn context): een animatie daarop
  // werd niet getoond. De inhoud van het venster laten invagen.
  CALayer* layer = s_hudWindow.rootViewController.view.layer;
  layer.opacity = 0;  // eindwaarde zonder animatie: onzichtbaar
  CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
  fade.fromValue = @0.0;
  fade.toValue = @1.0;
  fade.beginTime = CACurrentMediaTime() + kHudDelay;
  fade.duration = kHudFade;
  fade.fillMode = kCAFillModeForwards;  // na het invagen zichtbaar blijven
  fade.removedOnCompletion = NO;
  [layer addAnimation:fade forKey:@"ocpnios_hud"];
  s_hudWindow.hidden = NO;
  // Opmaak nu doen: de hoofdthread rekent hierna door en komt niet aan de normale
  // layoutronde toe; zonder dit had het vak nog geen maat en bleef het onzichtbaar.
  [s_hudWindow layoutIfNeeded];
  [CATransaction flush];  // nu doorgeven
}

void ocpnios_HideBusyHud() {
  if (!s_hudWindow || s_hudWindow.hidden) return;
  [s_hudWindow.rootViewController.view.layer removeAnimationForKey:@"ocpnios_hud"];
  s_hudWindow.hidden = YES;
}
