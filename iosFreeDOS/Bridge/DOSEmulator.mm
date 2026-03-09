/*
 * DOSEmulator.mm - Objective-C++ bridge for DOS emulator core
 */

#import "DOSEmulator.h"
#include "dos_machine.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <mach/mach_time.h>

//=============================================================================
// iOS dos_io implementation
//=============================================================================

class dos_io_ios : public dos_io {
public:
    static constexpr int MAX_DRIVES = 5;

    dos_io_ios() {
        for (int i = 0; i < MAX_DRIVES; i++) {
            disk_data[i] = nullptr;
            disk_sz[i] = 0;
            disk_is_manifest[i] = false;
        }
        mach_timebase_info(&_tb);
        _last_video_ns = 0;
    }

    ~dos_io_ios() {
        for (int i = 0; i < MAX_DRIVES; i++)
            delete[] disk_data[i];
    }

    bool load_disk(int drive, const uint8_t *data, size_t size) {
        int idx = drive_index(drive);
        if (idx < 0) return false;
        delete[] disk_data[idx];
        disk_data[idx] = new uint8_t[size];
        memcpy(disk_data[idx], data, size);
        disk_sz[idx] = size;
        return true;
    }

    bool create_disk(int drive, uint64_t size) {
        int idx = drive_index(drive);
        if (idx < 0) return false;
        delete[] disk_data[idx];
        disk_data[idx] = new uint8_t[size];
        memset(disk_data[idx], 0, size);
        disk_sz[idx] = size;
        return true;
    }

    const uint8_t *get_disk_data(int drive) {
        int idx = drive_index(drive);
        return (idx >= 0) ? disk_data[idx] : nullptr;
    }

    uint64_t get_disk_size(int drive) {
        int idx = drive_index(drive);
        return (idx >= 0) ? disk_sz[idx] : 0;
    }

    bool is_loaded(int drive) {
        int idx = drive_index(drive);
        return idx >= 0 && disk_data[idx] != nullptr;
    }

    void set_manifest(int drive, bool manifest) {
        int idx = drive_index(drive);
        if (idx >= 0) disk_is_manifest[idx] = manifest;
    }

    bool poll_manifest_write_warning() {
        return manifest_write_fired.exchange(false);
    }

    __weak id delegate;

    // Mouse state (atomic for thread safety)
    std::atomic<int> mouse_x{320}, mouse_y{100}, mouse_btn{0};
    bool has_mouse = true;

    void console_write(uint8_t ch) override { (void)ch; }
    bool console_has_input() override { return false; }
    int console_read() override { return -1; }

    void video_mode_changed(int mode, int cols, int rows) override {
        if (delegate && [delegate respondsToSelector:@selector(emulatorVideoModeChanged:cols:rows:)]) {
            int m = mode, c = cols, r = rows;
            dispatch_async(dispatch_get_main_queue(), ^{
                [delegate emulatorVideoModeChanged:m cols:c rows:r];
            });
        }
    }

    void video_refresh(const uint8_t *vram, int cols, int rows) override {
        if (!should_refresh()) return;
        if (delegate && [delegate respondsToSelector:@selector(emulatorVideoRefresh:cols:rows:)]) {
            NSData *data = [NSData dataWithBytes:vram length:cols * rows * 2];
            int c = cols, r = rows;
            dispatch_async(dispatch_get_main_queue(), ^{
                [delegate emulatorVideoRefresh:data cols:c rows:r];
            });
        }
    }

    void video_refresh_gfx(const uint8_t *framebuf, int width, int height,
                            const uint8_t palette[][3]) override {
        if (!should_refresh()) return;
        if (delegate && [delegate respondsToSelector:@selector(emulatorVideoRefreshGfx:width:height:palette:)]) {
            NSData *fb = [NSData dataWithBytes:framebuf length:width * height];
            NSData *pal = [NSData dataWithBytes:palette length:256 * 3];
            int w = width, h = height;
            dispatch_async(dispatch_get_main_queue(), ^{
                [delegate emulatorVideoRefreshGfx:fb width:w height:h palette:pal];
            });
        }
    }

    void video_set_cursor(int row, int col) override {
        // Cursor updates are coalesced with video refresh — only dispatch
        // if we actually sent a video frame this cycle (avoids flooding main queue)
        if (_cursor_row == row && _cursor_col == col) return;
        _cursor_row = row;
        _cursor_col = col;
        if (delegate && [delegate respondsToSelector:@selector(emulatorVideoSetCursorRow:col:)]) {
            int r = row, c = col;
            dispatch_async(dispatch_get_main_queue(), ^{
                [delegate emulatorVideoSetCursorRow:r col:c];
            });
        }
    }

    bool disk_present(int drive) override {
        int idx = drive_index(drive);
        return idx >= 0 && disk_data[idx] != nullptr;
    }

    size_t disk_read(int drive, uint64_t offset, uint8_t *buf, size_t count) override {
        int idx = drive_index(drive);
        if (idx < 0 || !disk_data[idx]) return 0;
        if (offset >= disk_sz[idx]) return 0;
        if (offset + count > disk_sz[idx]) count = disk_sz[idx] - offset;
        memcpy(buf, disk_data[idx] + offset, count);
        return count;
    }

    size_t disk_write(int drive, uint64_t offset, const uint8_t *buf, size_t count) override {
        int idx = drive_index(drive);
        if (idx < 0 || !disk_data[idx]) return 0;
        if (offset >= disk_sz[idx]) return 0;
        if (offset + count > disk_sz[idx]) count = disk_sz[idx] - offset;
        memcpy(disk_data[idx] + offset, buf, count);
        if (disk_is_manifest[idx]) manifest_write_fired.store(true);
        return count;
    }

    uint64_t disk_size(int drive) override {
        int idx = drive_index(drive);
        return (idx >= 0) ? disk_sz[idx] : 0;
    }

    void get_time(int &hour, int &min, int &sec, int &hundredths) override {
        NSDate *now = [NSDate date];
        NSCalendar *cal = [NSCalendar currentCalendar];
        NSDateComponents *comp = [cal components:(NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond) fromDate:now];
        hour = (int)comp.hour;
        min = (int)comp.minute;
        sec = (int)comp.second;
        hundredths = 0;
    }

    void get_date(int &year, int &month, int &day, int &weekday) override {
        NSDate *now = [NSDate date];
        NSCalendar *cal = [NSCalendar currentCalendar];
        NSDateComponents *comp = [cal components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay | NSCalendarUnitWeekday) fromDate:now];
        year = (int)comp.year;
        month = (int)comp.month;
        day = (int)comp.day;
        weekday = ((int)comp.weekday + 5) % 7;
    }

    // Mouse interface
    void mouse_get_state(int &x, int &y, int &buttons) override {
        x = mouse_x.load();
        y = mouse_y.load();
        buttons = mouse_btn.load();
    }

    bool mouse_present() override { return has_mouse; }

private:
    uint8_t *disk_data[MAX_DRIVES];
    uint64_t disk_sz[MAX_DRIVES];
    bool disk_is_manifest[MAX_DRIVES];
    std::atomic<bool> manifest_write_fired{false};
    mach_timebase_info_data_t _tb;
    uint64_t _last_video_ns;
    int _cursor_row = -1, _cursor_col = -1;

    // Throttle video refreshes to ~60fps to avoid flooding the main queue
    bool should_refresh() {
        uint64_t now = mach_absolute_time() * _tb.numer / _tb.denom;
        if (now - _last_video_ns < 16000000) return false;  // 16ms
        _last_video_ns = now;
        return true;
    }

    int drive_index(int drive) {
        if (drive >= 0 && drive < 2) return drive;
        if (drive >= 0x80 && drive < 0x82) return drive - 0x80 + 2;
        if (drive == 0xE0) return 4;
        return -1;
    }
};

//=============================================================================
// Scancode mapping
//=============================================================================

static uint8_t ascii_to_scancode(uint8_t ascii) {
    if (ascii >= 'a' && ascii <= 'z') {
        static const uint8_t sc[] = {
            0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,
            0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,
            0x16,0x2F,0x11,0x2D,0x15,0x2C
        };
        return sc[ascii - 'a'];
    }
    if (ascii >= 'A' && ascii <= 'Z')
        return ascii_to_scancode(ascii - 'A' + 'a');
    if (ascii >= '1' && ascii <= '9') return 0x02 + (ascii - '1');
    if (ascii == '0') return 0x0B;
    switch (ascii) {
        case '\r': case '\n': return 0x1C;
        case '\t': return 0x0F;
        case 0x1B: return 0x01;
        case ' ':  return 0x39;
        case '-':  return 0x0C;
        case '=':  return 0x0D;
        case '[':  return 0x1A;
        case ']':  return 0x1B;
        case '\\': return 0x2B;
        case ';':  return 0x27;
        case '\'': return 0x28;
        case '`':  return 0x29;
        case ',':  return 0x33;
        case '.':  return 0x34;
        case '/':  return 0x35;
        case 0x08: return 0x0E;
        default:   return 0x00;
    }
}

//=============================================================================
// DOSEmulator Implementation
//=============================================================================

@implementation DOSEmulator {
    std::unique_ptr<emu88_mem> _mem;
    std::unique_ptr<dos_io_ios> _io;
    std::unique_ptr<dos_machine> _machine;
    dispatch_queue_t _emulatorQueue;
    BOOL _shouldRun;
    DOSControlifyMode _controlifyMode;
    dos_machine::Config _config;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _emulatorQueue = dispatch_queue_create("com.iosFreeDOS.emulator", DISPATCH_QUEUE_SERIAL);
        _shouldRun = NO;
        _controlifyMode = DOSControlifyOff;
    }
    return self;
}

- (void)dealloc { [self stop]; }

// Configuration
- (void)setDisplayAdapter:(DOSDisplayAdapter)adapter {
    _config.display = static_cast<dos_machine::DisplayAdapter>(adapter);
}

- (void)setMouseEnabled:(BOOL)enabled {
    _config.mouse_enabled = enabled;
    if (_io) _io->has_mouse = enabled;
}

- (void)setSpeakerEnabled:(BOOL)enabled {
    _config.speaker_enabled = enabled;
}

- (void)setSoundCard:(int)type {
    _config.sound_card = type;
}

- (void)setCDROMEnabled:(BOOL)enabled {
    _config.cdrom_enabled = enabled;
}

// Disk Management
- (BOOL)loadDisk:(int)drive fromPath:(NSString*)path {
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data) return NO;
    return [self loadDisk:drive fromData:data];
}

- (BOOL)loadDisk:(int)drive fromData:(NSData*)data {
    if (!_io) _io = std::make_unique<dos_io_ios>();
    return _io->load_disk(drive, (const uint8_t*)data.bytes, data.length);
}

- (BOOL)createDisk:(int)drive size:(uint64_t)sizeBytes {
    if (!_io) _io = std::make_unique<dos_io_ios>();
    return _io->create_disk(drive, sizeBytes);
}

- (nullable NSData*)getDiskData:(int)drive {
    if (!_io) return nil;
    const uint8_t *data = _io->get_disk_data(drive);
    uint64_t size = _io->get_disk_size(drive);
    if (!data || size == 0) return nil;
    return [NSData dataWithBytes:data length:(NSUInteger)size];
}

- (BOOL)saveDisk:(int)drive toPath:(NSString*)path {
    NSData *data = [self getDiskData:drive];
    if (!data) return NO;
    return [data writeToFile:path atomically:YES];
}

- (BOOL)isDiskLoaded:(int)drive { return _io && _io->is_loaded(drive); }
- (uint64_t)diskSize:(int)drive { return _io ? _io->get_disk_size(drive) : 0; }
- (int)loadISO:(NSString*)path {
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data) return -1;
    if (!_io) _io = std::make_unique<dos_io_ios>();
    if (!_io->load_disk(0xE0, (const uint8_t*)data.bytes, data.length)) return -1;
    return 0xE0;
}

// Execution
- (BOOL)isRunning { return _shouldRun; }
- (BOOL)isWaitingForInput { return _machine && _machine->is_waiting_for_key(); }

- (void)startWithBootDrive:(int)drive {
    if (_shouldRun) return;
    if (!_io) _io = std::make_unique<dos_io_ios>();
    _io->delegate = self.delegate;
    _io->has_mouse = _config.mouse_enabled;

    _mem = std::make_unique<emu88_mem>(0x1000000);
    _machine = std::make_unique<dos_machine>(_mem.get(), _io.get());
    _machine->configure(_config);

    if (!_machine->boot(drive)) {
        NSLog(@"[DOSEmu] Boot failed for drive 0x%02X", drive);
        return;
    }

    _shouldRun = YES;
    dispatch_async(_emulatorQueue, ^{ [self runLoop]; });
}

- (void)stop {
    if (!_shouldRun) return;
    _shouldRun = NO;
    // Wait for the emulator thread to finish before returning
    dispatch_sync(_emulatorQueue, ^{});
}

- (void)reset {
    [self stop];
    _machine.reset();
    _mem.reset();
}

- (void)runLoop {
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    uint64_t wall_start = mach_absolute_time();
    unsigned long long cycle_start = _machine->cycles;

    while (_shouldRun) {
        bool ok = _machine->run_batch(50000);

        if (!ok) {
            // CPU permanently halted (HLT with IF=0, or unimplemented opcode)
            NSLog(@"[DOSEmu] CPU halted at %04X:%04X (cycles=%llu)",
                  _machine->sregs[dos_machine::seg_CS], _machine->ip, _machine->cycles);
            break;
        }

        uint32_t cps = 0;
        switch (_machine->get_speed()) {
            case dos_machine::SPEED_FULL:      cps = 0; break;
            case dos_machine::SPEED_PC_4_77:   cps = 4770000; break;
            case dos_machine::SPEED_AT_8:      cps = 8000000; break;
            case dos_machine::SPEED_386SX_16:  cps = 48000000; break;
            case dos_machine::SPEED_386DX_33:  cps = 100000000; break;
            case dos_machine::SPEED_486DX2_66: cps = 260000000; break;
        }

        if (cps > 0) {
            unsigned long long elapsed_cycles = _machine->cycles - cycle_start;
            uint64_t wall_now = mach_absolute_time();
            uint64_t wall_elapsed_ns = (wall_now - wall_start) * timebase.numer / timebase.denom;
            uint64_t target_ns = (uint64_t)elapsed_cycles * 1000000000ULL / cps;
            if (target_ns > wall_elapsed_ns) {
                uint64_t sleep_ns = target_ns - wall_elapsed_ns;
                if (sleep_ns > 100000) usleep((unsigned)(sleep_ns / 1000));
            }
        }

        if (_machine->is_waiting_for_key()) {
            if (_io->delegate && [_io->delegate respondsToSelector:@selector(emulatorDidRequestInput)]) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self->_io->delegate emulatorDidRequestInput];
                });
            }
            // Sleep 16ms (~60Hz) when idle at a prompt to avoid burning CPU
            // with continuous video refresh dispatches to the main thread
            [NSThread sleepForTimeInterval:0.016];
        } else if (cps == 0) {
            [NSThread sleepForTimeInterval:0.0001];
        }
    }

    _shouldRun = NO;
}

// Input
- (void)sendCharacter:(unichar)ch {
    if (!_machine) return;
    uint8_t ascii = (uint8_t)ch;
    if (_controlifyMode != DOSControlifyOff) {
        if (ascii >= 'a' && ascii <= 'z') ascii = ascii - 'a' + 1;
        else if (ascii >= 'A' && ascii <= 'Z') ascii = ascii - 'A' + 1;
        if (_controlifyMode == DOSControlifyOneChar) _controlifyMode = DOSControlifyOff;
    }
    if (ascii == '\n') ascii = '\r';
    _machine->queue_key(ascii, ascii_to_scancode(ascii));
}

- (void)sendScancode:(uint8_t)ascii scancode:(uint8_t)scancode {
    if (_machine) _machine->queue_key(ascii, scancode);
}

- (void)updateMouseX:(int)x y:(int)y buttons:(int)buttons {
    if (_io) {
        _io->mouse_x.store(x);
        _io->mouse_y.store(y);
        _io->mouse_btn.store(buttons);
    }
}

- (void)setControlify:(DOSControlifyMode)mode { _controlifyMode = mode; }
- (DOSControlifyMode)getControlify { return _controlifyMode; }

- (void)setSpeed:(DOSSpeedMode)mode {
    _config.speed = static_cast<dos_machine::SpeedMode>(mode);
    if (_machine) _machine->set_speed(_config.speed);
}

- (DOSSpeedMode)getSpeed {
    if (!_machine) return DOSSpeedFull;
    return static_cast<DOSSpeedMode>(_machine->get_speed());
}

- (void)setDiskIsManifest:(int)drive isManifest:(BOOL)manifest {
    if (_io) _io->set_manifest(drive, manifest);
}

- (BOOL)pollManifestWriteWarning {
    return _io ? _io->poll_manifest_write_warning() : NO;
}

@end
