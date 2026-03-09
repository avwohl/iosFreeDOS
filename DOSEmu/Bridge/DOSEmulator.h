/*
 * DOSEmulator.h - Objective-C bridge for DOS emulator core
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Display adapter
typedef NS_ENUM(NSInteger, DOSDisplayAdapter) {
    DOSDisplayCGA NS_SWIFT_NAME(cga) = 0,
    DOSDisplayMDA NS_SWIFT_NAME(mda) = 1,
    DOSDisplayHercules NS_SWIFT_NAME(hercules) = 2,
    DOSDisplayCGAMDA NS_SWIFT_NAME(cgaMDA) = 3,
    DOSDisplayEGA NS_SWIFT_NAME(ega) = 4,
    DOSDisplayVGA NS_SWIFT_NAME(vga) = 5
};

// Speed modes
typedef NS_ENUM(NSInteger, DOSSpeedMode) {
    DOSSpeedFull NS_SWIFT_NAME(full) = 0,
    DOSSpeedPC NS_SWIFT_NAME(pc) = 1,
    DOSSpeedAT NS_SWIFT_NAME(at) = 2,
    DOSSpeed386SX NS_SWIFT_NAME(i386sx) = 3,
    DOSSpeed386DX NS_SWIFT_NAME(i386dx) = 4,
    DOSSpeed486DX2 NS_SWIFT_NAME(i486dx2) = 5
};

// Controlify mode
typedef NS_ENUM(NSInteger, DOSControlifyMode) {
    DOSControlifyOff NS_SWIFT_NAME(off) = 0,
    DOSControlifyOneChar NS_SWIFT_NAME(oneChar) = 1,
    DOSControlifySticky NS_SWIFT_NAME(sticky) = 2
};

@protocol DOSEmulatorDelegate <NSObject>
@optional
- (void)emulatorVideoRefresh:(NSData*)vramData cols:(int)cols rows:(int)rows;
- (void)emulatorVideoRefreshGfx:(NSData*)framebuf width:(int)width height:(int)height
                        palette:(NSData*)palette;
- (void)emulatorVideoModeChanged:(int)mode cols:(int)cols rows:(int)rows;
- (void)emulatorVideoSetCursorRow:(int)row col:(int)col;
- (void)emulatorDidRequestInput;
@end

@interface DOSEmulator : NSObject

@property (weak, nonatomic) id<DOSEmulatorDelegate> delegate;
@property (readonly, nonatomic) BOOL isRunning;
@property (readonly, nonatomic) BOOL isWaitingForInput;

- (instancetype)init;

// Configuration (call before start)
- (void)setDisplayAdapter:(DOSDisplayAdapter)adapter;
- (void)setMouseEnabled:(BOOL)enabled;
- (void)setSpeakerEnabled:(BOOL)enabled;
- (void)setSoundCard:(int)type;
- (void)setCDROMEnabled:(BOOL)enabled;

// Disk management
- (BOOL)loadDisk:(int)drive fromPath:(NSString*)path;
- (BOOL)loadDisk:(int)drive fromData:(NSData*)data;
- (BOOL)createDisk:(int)drive size:(uint64_t)sizeBytes;
- (nullable NSData*)getDiskData:(int)drive;
- (BOOL)saveDisk:(int)drive toPath:(NSString*)path;
- (BOOL)isDiskLoaded:(int)drive;
- (uint64_t)diskSize:(int)drive;

// ISO boot
- (int)loadISO:(NSString*)path;

// Execution
- (void)startWithBootDrive:(int)drive;
- (void)stop;
- (void)reset;

// Input
- (void)sendCharacter:(unichar)ch;
- (void)sendScancode:(uint8_t)ascii scancode:(uint8_t)scancode;

// Mouse input from host (virtual coordinates 0-639, 0-199)
- (void)updateMouseX:(int)x y:(int)y buttons:(int)buttons;

// Speed
- (void)setSpeed:(DOSSpeedMode)mode;
- (DOSSpeedMode)getSpeed;

// Controlify
- (void)setControlify:(DOSControlifyMode)mode;
- (DOSControlifyMode)getControlify;

// Manifest disk tracking
- (void)setDiskIsManifest:(int)drive isManifest:(BOOL)manifest;
- (BOOL)pollManifestWriteWarning;

@end

NS_ASSUME_NONNULL_END
