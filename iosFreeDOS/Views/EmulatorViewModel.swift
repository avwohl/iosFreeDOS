/*
 * EmulatorViewModel.swift - View model for DOS emulator
 */

import SwiftUI
import Combine
import UniformTypeIdentifiers
import CryptoKit

// MARK: - Catalog Image Types

enum CatalogImageType: String, Codable {
    case floppy
    case hdd
    case iso

    var label: String {
        switch self {
        case .floppy: return "Floppy"
        case .hdd: return "Hard Disk"
        case .iso: return "CD-ROM ISO"
        }
    }
}

// MARK: - Downloadable Disk

struct DownloadableDisk: Identifiable, Codable {
    let filename: String
    let name: String
    let description: String
    let url: String
    let sizeBytes: Int64
    let license: String
    let sha256: String?
    let defaultDrive: Int?
    let type: CatalogImageType
    var id: String { filename }

    var formattedSize: String {
        sizeBytes >= 1024 * 1024
            ? String(format: "%.1f MB", Double(sizeBytes) / (1024 * 1024))
            : "\(sizeBytes / 1024) KB"
    }
}

enum DownloadState: Equatable {
    case notDownloaded
    case downloading(progress: Double)
    case downloaded
    case error(String)
}

// MARK: - View Model

class EmulatorViewModel: NSObject, ObservableObject, DOSEmulatorDelegate {

    // Terminal
    @Published var terminalCells: [[TerminalCell]] = []
    @Published var cursorRow: Int = 0
    @Published var cursorCol: Int = 0
    @Published var terminalShouldFocus: Bool = false
    @Published var gfxImage: UIImage? = nil  // VGA mode 13h framebuffer
    let terminalRows = 25
    let terminalCols = 80
    weak var terminalView: TerminalUIView?

    // Emulator state
    @Published var isRunning: Bool = false
    @Published var statusText: String = ""
    @Published var isControlifyActive: Bool = false

    // Configuration
    @Published var configManager = ConfigManager()

    // Disk paths
    @Published var floppyAPath: URL? = nil
    @Published var floppyBPath: URL? = nil
    @Published var hddCPath: URL? = nil
    @Published var hddDPath: URL? = nil
    @Published var isoPath: URL? = nil
    // bootDrive is stored per-config (config.bootDrive)

    // File picker
    @Published var showingDiskPicker: Bool = false
    @Published var showingDiskExporter: Bool = false
    @Published var showingError: Bool = false
    @Published var errorMessage: String = ""
    @Published var showingManifestWriteWarning: Bool = false

    // Manifest disk tracking - which drive slots hold catalog images
    private var manifestDrives: Set<Int> = []
    private var manifestWriteWarningShown = false
    private var manifestPollTimer: Timer?

    var warnManifestWrites: Bool {
        get {
            if UserDefaults.standard.object(forKey: "warnManifestWrites") == nil { return true }
            return UserDefaults.standard.bool(forKey: "warnManifestWrites")
        }
        set { UserDefaults.standard.set(newValue, forKey: "warnManifestWrites") }
    }

    // Catalog
    @Published var diskCatalog: [DownloadableDisk] = []
    @Published var downloadStates: [String: DownloadState] = [:]
    @Published var catalogLoading: Bool = false
    @Published var catalogError: String? = nil

    // URL download
    @Published var urlInput: String = ""
    @Published var urlDownloading: Bool = false
    @Published var urlDownloadProgress: Double = 0

    var currentDiskUnit: Int = 0
    var exportDocument: DiskImageDocument? = nil

    private var emulator: DOSEmulator?
    private var diskSaveTimer: Timer?
    private var configCancellable: AnyCancellable?
    private var pendingAttachments: [String: Int] = [:]
    private var bookmarksResolved = false

    // Dedicated URLSession with no caching for disk downloads (avoids redirect caching issues)
    private lazy var downloadSession: URLSession = {
        let config = URLSessionConfiguration.ephemeral
        config.requestCachePolicy = .reloadIgnoringLocalAndRemoteCacheData
        config.urlCache = nil
        return URLSession(configuration: config)
    }()

    private let catalogURL = "https://github.com/avwohl/iosFreeDOS/releases/latest/download/disks.xml"
    private let releaseBaseURL = "https://github.com/avwohl/iosFreeDOS/releases/latest/download"

    var disksDirectory: URL {
        let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Disks")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    // Convenience accessor for current config
    var config: MachineConfig {
        get { configManager.activeConfig ?? MachineConfig() }
        set { configManager.updateConfig(newValue) }
    }

    override init() {
        super.init()
        configCancellable = configManager.objectWillChange
            .sink { [weak self] _ in
                self?.objectWillChange.send()
            }
        clearTerminal()
        _ = loadCachedCatalog()
        // Resolve disk bookmarks off main thread — bookmark resolution can
        // stall for seconds per bookmark on real devices, freezing the UI.
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            self?.resolveBookmarksInBackground()
        }
        fetchDiskCatalog()
    }

    func clearTerminal() {
        terminalCells = Array(
            repeating: Array(repeating: TerminalCell(), count: terminalCols),
            count: terminalRows
        )
        cursorRow = 0; cursorCol = 0
    }

    // MARK: - Emulator Lifecycle

    func start() {
        guard !isRunning else { return }
        guard floppyAPath != nil || hddCPath != nil || isoPath != nil else {
            errorMessage = "No disk image loaded."
            showingError = true
            return
        }

        emulator = DOSEmulator()
        emulator?.delegate = self

        // Apply machine config
        let cfg = config
        emulator?.setDisplayAdapter(DOSDisplayAdapter(rawValue: cfg.displayAdapter) ?? .cga)
        emulator?.setMouseEnabled(cfg.mouseEnabled)
        emulator?.setSpeakerEnabled(cfg.speakerEnabled)
        emulator?.setSoundCard(Int32(cfg.soundCard))
        emulator?.setCDROMEnabled(cfg.cdromEnabled)
        emulator?.setSpeed(DOSSpeedMode(rawValue: cfg.speedMode) ?? .pc)

        // Load disks
        if let url = floppyAPath {
            _ = url.startAccessingSecurityScopedResource()
            if !emulator!.loadDisk(0, fromPath: url.path) {
                url.stopAccessingSecurityScopedResource()
                errorMessage = "Failed to load floppy A:"
                showingError = true; return
            }
            url.stopAccessingSecurityScopedResource()
        }
        if let url = floppyBPath {
            _ = url.startAccessingSecurityScopedResource()
            _ = emulator!.loadDisk(1, fromPath: url.path)
            url.stopAccessingSecurityScopedResource()
        }
        if let url = hddCPath {
            _ = url.startAccessingSecurityScopedResource()
            if !emulator!.loadDisk(0x80, fromPath: url.path) {
                url.stopAccessingSecurityScopedResource()
                errorMessage = "Failed to load hard disk C:"
                showingError = true; return
            }
            url.stopAccessingSecurityScopedResource()
        }
        if let url = hddDPath {
            _ = url.startAccessingSecurityScopedResource()
            _ = emulator!.loadDisk(0x81, fromPath: url.path)
            url.stopAccessingSecurityScopedResource()
        }
        if let url = isoPath {
            _ = url.startAccessingSecurityScopedResource()
            let result = emulator!.loadISO(url.path)
            url.stopAccessingSecurityScopedResource()
            if result < 0 {
                errorMessage = "Failed to load CD-ROM ISO"
                showingError = true; return
            }
        }

        // Flag manifest drives so the emulator can detect writes to catalog disks
        for drive in manifestDrives {
            emulator?.setDiskIsManifest(Int32(drive), isManifest: true)
        }

        clearTerminal()
        isRunning = true
        terminalShouldFocus = true
        manifestWriteWarningShown = false
        emulator?.start(withBootDrive: Int32(config.bootDrive))

        diskSaveTimer = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in
            self?.saveAllDisks()
        }

        // Poll for manifest disk write warnings
        manifestPollTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.checkManifestWriteWarning()
        }
    }

    func stop() {
        guard isRunning else { return }
        diskSaveTimer?.invalidate(); diskSaveTimer = nil
        manifestPollTimer?.invalidate(); manifestPollTimer = nil
        saveAllDisks()
        emulator?.stop()
        isRunning = false
    }

    func reset() { stop(); emulator = nil }

    // MARK: - Input

    func sendKey(_ char: Character) {
        guard let emu = emulator else { return }
        let scalar = char.unicodeScalars.first?.value ?? 0
        switch scalar {
        case 0xF700: emu.sendScancode(0, scancode: 0x48); return
        case 0xF701: emu.sendScancode(0, scancode: 0x50); return
        case 0xF702: emu.sendScancode(0, scancode: 0x4B); return
        case 0xF703: emu.sendScancode(0, scancode: 0x4D); return
        default: break
        }
        emu.sendCharacter(char.unicodeScalars.first.map { unichar($0.value) } ?? 0)
    }

    func sendMouseUpdate(x: Int, y: Int, buttons: Int) {
        emulator?.updateMouseX(Int32(x), y: Int32(y), buttons: Int32(buttons))
    }

    func setControlify(_ mode: DOSControlifyMode) {
        emulator?.setControlify(mode)
        isControlifyActive = (mode != .off)
    }

    func setSpeed(_ mode: DOSSpeedMode) {
        var cfg = config
        cfg.speedMode = mode.rawValue
        configManager.updateConfig(cfg)
        emulator?.setSpeed(mode)
    }

    // MARK: - Disk Management

    func loadDisk(_ unit: Int) {
        currentDiskUnit = unit
        showingDiskPicker = true
    }

    func handleDiskImport(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }
        let bookmark = try? url.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil)
        setDiskPath(currentDiskUnit, url: url, bookmark: bookmark)
    }

    func setDiskPath(_ unit: Int, url: URL, bookmark: Data? = nil) {
        switch unit {
        case 0:
            floppyAPath = url
            if let b = bookmark { UserDefaults.standard.set(b, forKey: "floppyABookmark") }
            setConfigBootDrive(0)
        case 1:
            floppyBPath = url
            if let b = bookmark { UserDefaults.standard.set(b, forKey: "floppyBBookmark") }
        case 0x80:
            hddCPath = url
            if let b = bookmark { UserDefaults.standard.set(b, forKey: "hddCBookmark") }
            if floppyAPath == nil { setConfigBootDrive(0x80) }
        case 0x81:
            hddDPath = url
            if let b = bookmark { UserDefaults.standard.set(b, forKey: "hddDBookmark") }
        case 0xE0:
            isoPath = url
            if let b = bookmark { UserDefaults.standard.set(b, forKey: "isoBookmark") }
            if floppyAPath == nil && hddCPath == nil { setConfigBootDrive(0xE0) }
        default: break
        }
        statusText = "Loaded \(url.lastPathComponent)"
    }

    private func setConfigBootDrive(_ drive: Int) {
        var cfg = config
        cfg.bootDrive = drive
        configManager.updateConfig(cfg)
    }

    func saveDisk(_ unit: Int) {
        guard let emu = emulator, let data = emu.getDiskData(Int32(unit)) else { return }
        currentDiskUnit = unit
        exportDocument = DiskImageDocument(data: data)
        showingDiskExporter = true
    }

    func handleExportResult(_ result: Result<URL, Error>) { exportDocument = nil }

    func createBlankFloppy(sizeKB: Int) {
        let data = Data(repeating: 0, count: sizeKB * 1024)
        let url = disksDirectory.appendingPathComponent("floppy_\(sizeKB)KB.img")
        do {
            try data.write(to: url)
            floppyAPath = url; setConfigBootDrive(0)
            UserDefaults.standard.removeObject(forKey: "floppyABookmark")
            statusText = "Created \(sizeKB)KB floppy"
        } catch {
            errorMessage = error.localizedDescription; showingError = true
        }
    }

    func createBlankHDD(sizeMB: Int) {
        let url = disksDirectory.appendingPathComponent("hdd_\(sizeMB)MB.img")
        if FileManager.default.createFile(atPath: url.path, contents: nil) {
            FileHandle(forWritingAtPath: url.path)?.truncateFile(atOffset: UInt64(sizeMB) * 1024 * 1024)
            hddCPath = url
            if floppyAPath == nil { setConfigBootDrive(0x80) }
            UserDefaults.standard.removeObject(forKey: "hddCBookmark")
            statusText = "Created \(sizeMB)MB hard disk"
        } else {
            errorMessage = "Failed to create hard disk"; showingError = true
        }
    }

    func saveAllDisks() {
        guard let emu = emulator else { return }
        for (drive, path) in [(0, floppyAPath), (1, floppyBPath), (0x80, hddCPath), (0x81, hddDPath)] {
            guard let url = path, url.path.contains("/Documents/"),
                  let data = emu.getDiskData(Int32(drive)) else { continue }
            try? data.write(to: url)
        }
    }

    func saveDisksOnBackground() { saveAllDisks() }

    func removeDisk(_ unit: Int) {
        switch unit {
        case 0: floppyAPath = nil; UserDefaults.standard.removeObject(forKey: "floppyABookmark")
        case 1: floppyBPath = nil; UserDefaults.standard.removeObject(forKey: "floppyBBookmark")
        case 0x80: hddCPath = nil; UserDefaults.standard.removeObject(forKey: "hddCBookmark")
        case 0x81: hddDPath = nil; UserDefaults.standard.removeObject(forKey: "hddDBookmark")
        case 0xE0: isoPath = nil; UserDefaults.standard.removeObject(forKey: "isoBookmark")
        default: break
        }
        manifestDrives.remove(unit)
    }

    /// Resolve security-scoped bookmarks on a background thread, then apply
    /// results on main.  Bookmark resolution can take seconds per entry on
    /// real devices (especially after reboot or for iCloud files).
    private func resolveBookmarksInBackground() {
        let keys = ["floppyABookmark", "floppyBBookmark", "hddCBookmark", "hddDBookmark", "isoBookmark"]
        var resolved: [(String, URL)] = []
        for key in keys {
            if let data = UserDefaults.standard.data(forKey: key) {
                var stale = false
                if let url = try? URL(resolvingBookmarkData: data, bookmarkDataIsStale: &stale) {
                    resolved.append((key, url))
                }
            }
        }
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            for (key, url) in resolved {
                switch key {
                case "floppyABookmark": self.floppyAPath = url
                case "floppyBBookmark": self.floppyBPath = url
                case "hddCBookmark":    self.hddCPath = url
                case "hddDBookmark":    self.hddDPath = url
                case "isoBookmark":     self.isoPath = url
                default: break
                }
            }
            self.bookmarksResolved = true
            self.autoAttachDefaultDisks()
        }
    }

    // MARK: - Disk Catalog

    func fetchDiskCatalog() {
        guard let url = URL(string: catalogURL) else { return }
        catalogLoading = true; catalogError = nil
        URLSession.shared.dataTask(with: url) { [weak self] data, response, error in
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.catalogLoading = false
                if error != nil {
                    _ = self.loadCachedCatalog() ? () : (self.catalogError = error!.localizedDescription)
                    return
                }
                if let http = response as? HTTPURLResponse, http.statusCode != 200 {
                    _ = self.loadCachedCatalog() ? () : (self.catalogError = "HTTP \(http.statusCode) from \(url.absoluteString)")
                    return
                }
                guard let data = data else { self.catalogError = "No data"; return }
                try? data.write(to: self.disksDirectory.appendingPathComponent("disks_catalog.xml"))
                self.parseCatalogXML(data)
                self.refreshDownloadStates()
                self.autoAttachDefaultDisks()
            }
        }.resume()
    }

    private func loadCachedCatalog() -> Bool {
        guard let data = try? Data(contentsOf: disksDirectory.appendingPathComponent("disks_catalog.xml")) else { return false }
        parseCatalogXML(data); refreshDownloadStates()
        autoAttachDefaultDisks()
        return !diskCatalog.isEmpty
    }

    private func parseCatalogXML(_ data: Data) {
        let parser = DiskCatalogXMLParser(data: data, baseURL: releaseBaseURL)
        diskCatalog = parser.parse()
        if let newVersion = parser.catalogVersion.map(String.init) {
            checkCatalogVersionAndInvalidate(newVersion: newVersion)
        }
    }

    func refreshDownloadStates() {
        for disk in diskCatalog {
            let path = disksDirectory.appendingPathComponent(disk.filename)
            downloadStates[disk.filename] = FileManager.default.fileExists(atPath: path.path) ? .downloaded : (downloadStates[disk.filename] ?? .notDownloaded)
        }
    }

    /// On first launch (no disks attached), auto-download and attach default disks from catalog.
    /// Waits for bookmark resolution so we know whether the user already has disks attached.
    private func autoAttachDefaultDisks() {
        guard bookmarksResolved else { return }
        let hasAnyDisk = floppyAPath != nil || floppyBPath != nil || hddCPath != nil || hddDPath != nil || isoPath != nil
        guard !hasAnyDisk else { return }

        for disk in diskCatalog {
            guard let drive = disk.defaultDrive else { continue }
            attachOrDownloadCatalogDisk(disk, forDrive: drive)
        }
    }

    // MARK: - Catalog Versioning

    private func checkCatalogVersionAndInvalidate(newVersion: String) {
        let storedVersion = UserDefaults.standard.string(forKey: "catalogVersion") ?? ""
        if storedVersion.isEmpty {
            UserDefaults.standard.set(newVersion, forKey: "catalogVersion")
        } else if storedVersion != newVersion {
            deleteAllDownloadedDisks()
            UserDefaults.standard.set(newVersion, forKey: "catalogVersion")
            statusText = "Catalog updated - disks need redownload"
            errorMessage = "Disk catalog has been updated (v\(storedVersion) -> v\(newVersion)). Downloaded disks have been cleared and need to be redownloaded."
            showingError = true
        }
    }

    private func deleteAllDownloadedDisks() {
        let fm = FileManager.default
        if let contents = try? fm.contentsOfDirectory(at: disksDirectory, includingPropertiesForKeys: nil) {
            for url in contents {
                let ext = url.pathExtension.lowercased()
                if ext == "img" || ext == "iso" || ext == "bin" {
                    try? fm.removeItem(at: url)
                    downloadStates[url.lastPathComponent] = .notDownloaded
                }
            }
        }
        // Clear any manifest drive assignments that pointed to catalog disks
        manifestDrives.removeAll()
    }

    // MARK: - Manifest Write Warning

    private func checkManifestWriteWarning() {
        guard warnManifestWrites, !manifestWriteWarningShown else { return }
        if emulator?.pollManifestWriteWarning() == true {
            manifestWriteWarningShown = true
            showingManifestWriteWarning = true
        }
    }

    func downloadDisk(_ disk: DownloadableDisk) {
        downloadDiskWithRetry(disk, attemptsRemaining: 3)
    }

    private func downloadDiskWithRetry(_ disk: DownloadableDisk, attemptsRemaining: Int) {
        guard let url = URL(string: disk.url) else {
            downloadStates[disk.filename] = .error("Invalid URL: \(disk.url)")
            return
        }
        downloadStates[disk.filename] = .downloading(progress: 0)

        let task = downloadSession.downloadTask(with: url) { [weak self] temp, resp, err in
            DispatchQueue.main.async {
                guard let self = self else { return }

                if let http = resp as? HTTPURLResponse, http.statusCode < 200 || http.statusCode >= 300 {
                    if attemptsRemaining > 1 {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            self.downloadDiskWithRetry(disk, attemptsRemaining: attemptsRemaining - 1)
                        }
                    } else {
                        self.downloadStates[disk.filename] = .error("HTTP \(http.statusCode) from \(disk.url)")
                        self.pendingAttachments.removeValue(forKey: disk.filename)
                    }
                    return
                }

                if let err = err {
                    if attemptsRemaining > 1 {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            self.downloadDiskWithRetry(disk, attemptsRemaining: attemptsRemaining - 1)
                        }
                    } else {
                        self.downloadStates[disk.filename] = .error("\(err.localizedDescription) (\(disk.url))")
                        self.pendingAttachments.removeValue(forKey: disk.filename)
                    }
                    return
                }

                guard let temp = temp else {
                    if attemptsRemaining > 1 {
                        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
                            self.downloadDiskWithRetry(disk, attemptsRemaining: attemptsRemaining - 1)
                        }
                    } else {
                        self.downloadStates[disk.filename] = .error("Download failed: \(disk.url)")
                        self.pendingAttachments.removeValue(forKey: disk.filename)
                    }
                    return
                }

                if let expected = disk.sha256, !expected.isEmpty,
                   let d = try? Data(contentsOf: temp) {
                    let hash = SHA256.hash(data: d).map { String(format: "%02x", $0) }.joined()
                    if hash != expected.lowercased() {
                        self.downloadStates[disk.filename] = .error("SHA256 mismatch")
                        self.pendingAttachments.removeValue(forKey: disk.filename)
                        return
                    }
                }

                let dest = self.disksDirectory.appendingPathComponent(disk.filename)
                try? FileManager.default.removeItem(at: dest)
                do {
                    try FileManager.default.moveItem(at: temp, to: dest)
                    self.downloadStates[disk.filename] = .downloaded
                    self.statusText = "Downloaded \(disk.name)"
                    // Auto-attach if there's a pending attachment for this disk
                    if let drive = self.pendingAttachments.removeValue(forKey: disk.filename) {
                        self.attachDiskPath(dest, forDrive: drive, diskName: disk.name)
                        self.manifestDrives.insert(drive)
                    }
                } catch {
                    self.downloadStates[disk.filename] = .error(error.localizedDescription)
                    self.pendingAttachments.removeValue(forKey: disk.filename)
                }
            }
        }
        let obs = task.progress.observe(\.fractionCompleted) { [weak self] p, _ in
            DispatchQueue.main.async { self?.downloadStates[disk.filename] = .downloading(progress: p.fractionCompleted) }
        }
        objc_setAssociatedObject(task, "obs", obs, .OBJC_ASSOCIATION_RETAIN)
        task.resume()
    }

    /// Explicit user selection from catalog — always re-download to ensure
    /// the latest version from the server.
    func useCatalogDisk(_ disk: DownloadableDisk, forDrive drive: Int) {
        pendingAttachments[disk.filename] = drive
        statusText = "Downloading \(disk.name)..."
        downloadDisk(disk)
    }

    /// Use cached disk if available, otherwise download.  Used for
    /// auto-attach on startup to avoid unnecessary network traffic.
    private func attachOrDownloadCatalogDisk(_ disk: DownloadableDisk, forDrive drive: Int) {
        let path = disksDirectory.appendingPathComponent(disk.filename)
        if FileManager.default.fileExists(atPath: path.path) {
            attachDiskPath(path, forDrive: drive, diskName: disk.name)
            manifestDrives.insert(drive)
        } else {
            pendingAttachments[disk.filename] = drive
            statusText = "Downloading \(disk.name)..."
            downloadDisk(disk)
        }
    }

    /// Delete a downloaded catalog disk image from local storage.
    func deleteCatalogDisk(_ disk: DownloadableDisk) {
        let path = disksDirectory.appendingPathComponent(disk.filename)
        try? FileManager.default.removeItem(at: path)
        downloadStates[disk.filename] = .notDownloaded
        // Detach from any drive that references this file
        let pairs: [(Int, URL?)] = [(0, floppyAPath), (1, floppyBPath),
                                     (0x80, hddCPath), (0x81, hddDPath), (0xE0, isoPath)]
        for (unit, diskPath) in pairs {
            if diskPath?.lastPathComponent == disk.filename {
                removeDisk(unit)
            }
        }
    }

    private func attachDiskPath(_ path: URL, forDrive drive: Int, diskName: String) {
        switch drive {
        case 0: floppyAPath = path; setConfigBootDrive(0)
        case 1: floppyBPath = path
        case 0x80: hddCPath = path; if floppyAPath == nil { setConfigBootDrive(0x80) }
        case 0x81: hddDPath = path
        case 0xE0: isoPath = path; if floppyAPath == nil && hddCPath == nil { setConfigBootDrive(0xE0) }
        default: break
        }
        statusText = "Loaded \(diskName)"
    }

    func downloadFromURL(toDrive drive: Int) {
        let trimmed = urlInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let url = URL(string: trimmed), url.scheme == "http" || url.scheme == "https" else {
            errorMessage = "Enter a valid URL"; showingError = true; return
        }
        urlDownloading = true; urlDownloadProgress = 0
        let task = downloadSession.downloadTask(with: url) { [weak self] temp, resp, err in
            DispatchQueue.main.async {
                guard let self = self else { return }
                self.urlDownloading = false
                if let err = err { self.errorMessage = "\(err.localizedDescription)\n\(trimmed)"; self.showingError = true; return }
                if let http = resp as? HTTPURLResponse, http.statusCode != 200 { self.errorMessage = "HTTP \(http.statusCode) from \(trimmed)"; self.showingError = true; return }
                guard let temp = temp else { return }
                let filename = url.lastPathComponent.isEmpty ? "download.img" : url.lastPathComponent
                let dest = self.disksDirectory.appendingPathComponent(filename)
                try? FileManager.default.removeItem(at: dest)
                do {
                    try FileManager.default.moveItem(at: temp, to: dest)
                    switch drive {
                    case 0: self.floppyAPath = dest; self.setConfigBootDrive(0)
                    case 1: self.floppyBPath = dest
                    case 0x80: self.hddCPath = dest; if self.floppyAPath == nil { self.setConfigBootDrive(0x80) }
                    case 0x81: self.hddDPath = dest
                    case 0xE0: self.isoPath = dest; if self.floppyAPath == nil && self.hddCPath == nil { self.setConfigBootDrive(0xE0) }
                    default: break
                    }
                    self.statusText = "Downloaded \(filename)"
                    self.urlInput = ""
                } catch { self.errorMessage = error.localizedDescription; self.showingError = true }
            }
        }
        let obs = task.progress.observe(\.fractionCompleted) { [weak self] p, _ in
            DispatchQueue.main.async { self?.urlDownloadProgress = p.fractionCompleted }
        }
        objc_setAssociatedObject(task, "obs", obs, .OBJC_ASSOCIATION_RETAIN)
        task.resume()
    }

    // MARK: - DOSEmulatorDelegate

    func emulatorVideoRefresh(_ vramData: Data, cols: Int32, rows: Int32) {
        let c = Int(cols), r = Int(rows)
        let bytes = (vramData as NSData).bytes.assumingMemoryBound(to: UInt8.self)
        var newCells = Array(repeating: Array(repeating: TerminalCell(), count: c), count: r)
        for row in 0..<r {
            for col in 0..<c {
                let off = (row * c + col) * 2
                let ch = bytes[off], attr = bytes[off + 1]
                let character: Character = (ch >= 0x20 && ch < 0x7F) ? Character(UnicodeScalar(ch)) : " "
                newCells[row][col] = TerminalCell(character: character, foreground: attr & 0x0F, background: (attr >> 4) & 0x07)
            }
        }
        // Update UIView directly, bypassing SwiftUI diffing
        if let tv = terminalView {
            tv.updateCells(newCells, cursorRow: cursorRow, cursorCol: cursorCol)
        } else {
            terminalCells = newCells
        }
    }

    func emulatorVideoRefreshGfx(_ framebuf: Data, width: Int32, height: Int32, palette: Data) {
        let w = Int(width), h = Int(height)
        let fbBytes = [UInt8](framebuf)
        let palBytes = [UInt8](palette)
        // Build RGBA bitmap
        var rgba = [UInt8](repeating: 255, count: w * h * 4)
        for i in 0..<(w * h) {
            let idx = Int(fbBytes[i])
            let r = Int(palBytes[idx * 3]) * 255 / 63
            let g = Int(palBytes[idx * 3 + 1]) * 255 / 63
            let b = Int(palBytes[idx * 3 + 2]) * 255 / 63
            rgba[i * 4] = UInt8(r)
            rgba[i * 4 + 1] = UInt8(g)
            rgba[i * 4 + 2] = UInt8(b)
            rgba[i * 4 + 3] = 255
        }
        let colorSpace = CGColorSpaceCreateDeviceRGB()
        if let ctx = CGContext(data: &rgba, width: w, height: h, bitsPerComponent: 8,
                               bytesPerRow: w * 4, space: colorSpace,
                               bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
           let cgImage = ctx.makeImage() {
            gfxImage = UIImage(cgImage: cgImage)
        }
    }

    func emulatorVideoModeChanged(_ mode: Int32, cols: Int32, rows: Int32) {
        gfxImage = nil
        clearTerminal()
    }
    func emulatorVideoSetCursorRow(_ row: Int32, col: Int32) {
        cursorRow = Int(row)
        cursorCol = Int(col)
        terminalView?.updateCursor(row: cursorRow, col: cursorCol)
    }
    func emulatorDidRequestInput() { if !terminalShouldFocus { terminalShouldFocus = true } }
}

// MARK: - XML Parser

class DiskCatalogXMLParser: NSObject, XMLParserDelegate {
    private let data: Data
    private let baseURL: String
    private var disks: [DownloadableDisk] = []
    var catalogVersion: Int?
    private var elem = "", inDisk = false
    private var cFilename = "", cName = "", cDesc = "", cLicense = "", cType = ""
    private var cSize: Int64 = 0
    private var cSHA: String?
    private var cDrive: Int?

    init(data: Data, baseURL: String) { self.data = data; self.baseURL = baseURL }

    func parse() -> [DownloadableDisk] {
        let p = XMLParser(data: data); p.delegate = self; p.parse()
        return disks
    }

    func parser(_ p: XMLParser, didStartElement e: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String] = [:]) {
        elem = e
        if e == "disks" || e == "catalog" { catalogVersion = attributes["version"].flatMap(Int.init) }
        else if e == "disk" { inDisk = true; cFilename = ""; cName = ""; cDesc = ""; cSize = 0; cLicense = ""; cSHA = nil; cDrive = nil; cType = "" }
    }

    func parser(_ p: XMLParser, foundCharacters s: String) {
        guard inDisk else { return }
        let t = s.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty else { return }
        switch elem {
        case "filename": cFilename += t
        case "name": cName += t
        case "description": cDesc += t
        case "size": cSize = Int64(t) ?? 0
        case "license": cLicense += t
        case "sha256": cSHA = (cSHA ?? "") + t
        case "defaultDrive": cDrive = Int(t)
        case "type": cType += t
        default: break
        }
    }

    func parser(_ p: XMLParser, didEndElement e: String, namespaceURI: String?, qualifiedName: String?) {
        if e == "disk" && inDisk {
            let imageType = CatalogImageType(rawValue: cType) ?? inferType(filename: cFilename, drive: cDrive)
            disks.append(DownloadableDisk(filename: cFilename, name: cName, description: cDesc, url: "\(baseURL)/\(cFilename)", sizeBytes: cSize, license: cLicense, sha256: cSHA, defaultDrive: cDrive, type: imageType))
            inDisk = false
        }
        elem = ""
    }

    /// Infer type from filename extension or drive number for v1 catalogs without <type>
    private func inferType(filename: String, drive: Int?) -> CatalogImageType {
        let ext = (filename as NSString).pathExtension.lowercased()
        if ext == "iso" || ext == "cue" || ext == "bin" { return .iso }
        if let d = drive, d == 0xE0 { return .iso }
        if let d = drive, d >= 0x80 { return .hdd }
        return .floppy
    }
}

// MARK: - File Document

struct DiskImageDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.data] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}
