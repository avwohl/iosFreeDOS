/*
 * MachineConfig.swift - Named machine configuration profiles
 */

import Foundation

struct MachineConfig: Codable, Identifiable, Equatable {
    var id: UUID = UUID()
    var name: String = "Default"

    // CPU
    var speedMode: Int = 1  // 0=full, 1=PC 4.77, 2=AT 8, 3=386SX-16, 4=386DX-33, 5=486DX2-66

    // Display
    var displayAdapter: Int = 0  // 0=CGA, 1=MDA, 2=Hercules, 3=CGA+MDA, 4=EGA, 5=VGA

    // Peripherals
    var mouseEnabled: Bool = true
    var speakerEnabled: Bool = true
    var soundCard: Int = 0  // 0=none, 1=Adlib, 2=SoundBlaster
    var cdromEnabled: Bool = true

    // Disks (stored as paths relative to Documents/Disks or bookmark data)
    var floppyAFilename: String?
    var floppyBFilename: String?
    var hddCFilename: String?
    var hddDFilename: String?
    var bootDrive: Int = 0  // 0=A, 0x80=C, 0xE0=CD-ROM

    // Helpers
    var speedLabel: String {
        switch speedMode {
        case 0: return "Full Speed"
        case 1: return "IBM PC (4.77 MHz)"
        case 2: return "IBM AT (8 MHz)"
        case 3: return "386SX (16 MHz)"
        case 4: return "386DX (33 MHz)"
        case 5: return "486DX2 (66 MHz)"
        default: return "Unknown"
        }
    }

    var displayLabel: String {
        switch displayAdapter {
        case 0: return "CGA"
        case 1: return "MDA"
        case 2: return "Hercules"
        case 3: return "CGA + MDA"
        case 4: return "EGA"
        case 5: return "VGA"
        default: return "Unknown"
        }
    }
}

// MARK: - Config Storage

class ConfigManager: ObservableObject {
    @Published var configs: [MachineConfig] = []
    @Published var activeConfigId: UUID?

    private let configsKey = "machineConfigs"
    private let activeKey = "activeConfigId"

    var activeConfig: MachineConfig? {
        get { configs.first { $0.id == activeConfigId } }
        set {
            if let cfg = newValue, let idx = configs.firstIndex(where: { $0.id == cfg.id }) {
                configs[idx] = cfg
                save()
            }
        }
    }

    init() {
        load()
        if configs.isEmpty {
            let def = MachineConfig(name: "Default PC")
            configs.append(def)
            activeConfigId = def.id
            save()
        }
        if activeConfigId == nil {
            activeConfigId = configs.first?.id
        }
    }

    func save() {
        if let data = try? JSONEncoder().encode(configs) {
            UserDefaults.standard.set(data, forKey: configsKey)
        }
        if let id = activeConfigId {
            UserDefaults.standard.set(id.uuidString, forKey: activeKey)
        }
    }

    func load() {
        if let data = UserDefaults.standard.data(forKey: configsKey),
           let loaded = try? JSONDecoder().decode([MachineConfig].self, from: data) {
            configs = loaded
        }
        if let str = UserDefaults.standard.string(forKey: activeKey) {
            activeConfigId = UUID(uuidString: str)
        }
    }

    func addConfig(name: String) -> MachineConfig {
        var cfg = activeConfig ?? MachineConfig()
        cfg.id = UUID()
        cfg.name = name
        configs.append(cfg)
        activeConfigId = cfg.id
        save()
        return cfg
    }

    func duplicateConfig(_ config: MachineConfig, name: String) -> MachineConfig {
        var dup = config
        dup.id = UUID()
        dup.name = name
        configs.append(dup)
        save()
        return dup
    }

    func deleteConfig(_ config: MachineConfig) {
        configs.removeAll { $0.id == config.id }
        if activeConfigId == config.id {
            activeConfigId = configs.first?.id
        }
        save()
    }

    func selectConfig(_ config: MachineConfig) {
        activeConfigId = config.id
        save()
    }

    func updateConfig(_ config: MachineConfig) {
        if let idx = configs.firstIndex(where: { $0.id == config.id }) {
            configs[idx] = config
            save()
        }
    }

    func exportConfig(_ config: MachineConfig) -> Data? {
        return try? JSONEncoder().encode(config)
    }

    func importConfig(from data: Data) -> MachineConfig? {
        guard var cfg = try? JSONDecoder().decode(MachineConfig.self, from: data) else { return nil }
        cfg.id = UUID()  // New ID to avoid conflicts
        configs.append(cfg)
        save()
        return cfg
    }
}
