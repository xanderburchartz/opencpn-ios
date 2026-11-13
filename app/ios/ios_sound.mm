// Geluid op iOS via AVFoundation.
//
// wxQt heeft op iOS geen werkende wxSound (de Unix-variant zoekt /dev/dsp), dus de
// knop "Test" bij Geluiden en de AIS- en ankeralarmen bleven stil (Xander, 10 nov 2026).
// o_sound::Factory() in libs/o_sound gebruikt op iOS deze klasse.
//
// Audiosessie: categorie Playback, zodat een alarm ook klinkt als de stil-schakelaar
// aan staat, en MixWithOthers, zodat muziek van een andere app niet stopt.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <string>

#include "o_sound/sound.h"

#if __has_feature(objc_arc)
#error "ios_sound.mm gaat uit van handmatig geheugenbeheer (geen ARC)"
#endif

class IosSound;

@interface OcpnIosSoundDelegate : NSObject <AVAudioPlayerDelegate>
@property(nonatomic, assign) IosSound* owner;
@end

class IosSound : public o_sound::Sound {
 public:
  IosSound() = default;
  ~IosSound() override { Release(); }

  std::string GetDeviceInfo(int) override { return "iOS AVAudioPlayer"; }

  bool Load(const char* path, int deviceIx) override {
    Release();
    m_ok = false;
    if (!path || !*path) return false;
    NSString* p = ResolvePath([NSString stringWithUTF8String:path]);
    if (!p) return false;
    PrepareSession();
    NSError* err = nil;
    AVAudioPlayer* pl =
        [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:p] error:&err];
    if (!pl) {
      NSLog(@"ocpnios: geluid laden mislukt %@: %@", p, err);
      return false;
    }
    m_delegate = (void*)[[OcpnIosSoundDelegate alloc] init];  // +1, zonder ARC
    ((OcpnIosSoundDelegate*)m_delegate).owner = this;
    pl.delegate = (OcpnIosSoundDelegate*)m_delegate;
    [pl prepareToPlay];
    m_player = (void*)pl;  // +1 van alloc
    m_soundfile = p.UTF8String;
    m_ok = true;
    return true;
  }

  void UnLoad() override { Release(); }

  bool Play() override {
    if (!m_ok || !m_player) return false;
    AVAudioPlayer* pl = (AVAudioPlayer*)m_player;
    if (pl.playing) return false;
    if (![pl play]) return false;
    if (m_on_finished) return true;  // asynchroon: de delegate meldt het einde
    // Synchroon: wachten tot het geluid klaar is (de audio loopt op een eigen thread).
    const NSTimeInterval limit = pl.duration + 1.0;
    NSDate* start = [NSDate date];
    while (pl.playing && -[start timeIntervalSinceNow] < limit) usleep(20000);
    return true;
  }

  bool Stop() override {
    if (m_player) [(AVAudioPlayer*)m_player stop];
    m_ok = false;
    return true;
  }

  void Finished() {
    if (m_on_finished) {
      auto cb = m_on_finished;
      m_on_finished = nullptr;
      cb(m_callback_data);  // kan dit object verwijderen; daarna niets meer doen
    }
  }

 private:
  void Release() {
    if (m_player) {
      AVAudioPlayer* pl = (AVAudioPlayer*)m_player;
      pl.delegate = nil;
      [pl stop];
      CFRelease(m_player);
      m_player = nullptr;
    }
    if (m_delegate) {
      ((OcpnIosSoundDelegate*)m_delegate).owner = nullptr;
      CFRelease(m_delegate);
      m_delegate = nullptr;
    }
  }

  // De instellingen bewaren een absoluut pad in de app-bundel; dat pad verandert bij elke
  // installatie. Bestaat het bestand niet, dan hetzelfde deel na "opencpn.app/" in de
  // huidige bundel zoeken.
  static NSString* ResolvePath(NSString* p) {
    NSFileManager* fm = [NSFileManager defaultManager];
    if ([fm fileExistsAtPath:p]) return p;
    NSRange r = [p rangeOfString:@".app/" options:NSBackwardsSearch];
    if (r.location != NSNotFound) {
      NSString* rel = [p substringFromIndex:r.location + r.length];
      NSString* q = [[NSBundle mainBundle].bundlePath stringByAppendingPathComponent:rel];
      if ([fm fileExistsAtPath:q]) return q;
    }
    NSLog(@"ocpnios: geluidsbestand niet gevonden: %@", p);
    return nil;
  }

  static void PrepareSession() {
    static bool done = false;
    if (done) return;
    done = true;
    AVAudioSession* s = [AVAudioSession sharedInstance];
    NSError* err = nil;
    [s setCategory:AVAudioSessionCategoryPlayback
               withOptions:AVAudioSessionCategoryOptionMixWithOthers
                     error:&err];
    if (err) NSLog(@"ocpnios: audiosessie: %@", err);
    [s setActive:YES error:&err];
  }

  // Objective-C-objecten als CF-verwijzing, zodat dit ook zonder ARC klopt.
  void* m_player = nullptr;
  void* m_delegate = nullptr;
};

@implementation OcpnIosSoundDelegate
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer*)player successfully:(BOOL)flag {
  if (self.owner) self.owner->Finished();
}
- (void)audioPlayerDecodeErrorDidOccur:(AVAudioPlayer*)player error:(NSError*)error {
  if (self.owner) self.owner->Finished();
}
@end

o_sound::Sound* ocpnios_NewSound() { return new IosSound(); }
