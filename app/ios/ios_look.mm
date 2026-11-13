// iOS-symbolen (SF Symbols) als QImage, wit, voor de gekleurde vierkantjes in lijsten
// (nav_list.cpp). De symbolen zitten in iOS zelf; niets te downloaden.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <QImage>

QImage ocpnios_SymbolImage(const char* name, int pointSize) {
  @autoreleasepool {
    UIImageSymbolConfiguration* cfg =
        [UIImageSymbolConfiguration configurationWithPointSize:pointSize
                                                        weight:UIImageSymbolWeightMedium];
    UIImage* sym = [UIImage systemImageNamed:[NSString stringWithUTF8String:name]
                           withConfiguration:cfg];
    if (!sym) return QImage();
    sym = [sym imageWithTintColor:[UIColor whiteColor]
                    renderingMode:UIImageRenderingModeAlwaysOriginal];
    const CGFloat scale = [UIScreen mainScreen].scale;
    UIGraphicsImageRendererFormat* fmt = [UIGraphicsImageRendererFormat defaultFormat];
    fmt.scale = scale;
    fmt.opaque = NO;
    UIGraphicsImageRenderer* r =
        [[[UIGraphicsImageRenderer alloc] initWithSize:sym.size format:fmt] autorelease];
    NSData* png = [r PNGDataWithActions:^(UIGraphicsImageRendererContext*) {
      [sym drawAtPoint:CGPointZero];
    }];
    QImage img;
    img.loadFromData(static_cast<const uchar*>(png.bytes), (int)png.length, "PNG");
    img.setDevicePixelRatio(scale);
    return img;
  }
}
