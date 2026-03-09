/*
 * TerminalView.swift - CGA terminal display with mouse support
 */

import SwiftUI
import UIKit

struct TerminalCell: Equatable {
    var character: Character = " "
    var foreground: UInt8 = 7
    var background: UInt8 = 0
}

struct TerminalView: UIViewRepresentable {
    @Binding var cells: [[TerminalCell]]
    @Binding var cursorRow: Int
    @Binding var cursorCol: Int
    @Binding var shouldFocus: Bool
    var onKeyInput: ((Character) -> Void)?
    var onMouseUpdate: ((Int, Int, Int) -> Void)?  // x, y, buttons

    let rows: Int
    let cols: Int
    let fontSize: CGFloat

    func makeUIView(context: Context) -> TerminalUIView {
        let view = TerminalUIView(rows: rows, cols: cols, fontSize: fontSize)
        view.onKeyInput = onKeyInput
        view.onMouseUpdate = onMouseUpdate
        return view
    }

    func updateUIView(_ uiView: TerminalUIView, context: Context) {
        uiView.updateFontSize(fontSize)
        uiView.updateCells(cells, cursorRow: cursorRow, cursorCol: cursorCol)
        uiView.onMouseUpdate = onMouseUpdate
        if shouldFocus && !uiView.isFirstResponder {
            DispatchQueue.main.async { uiView.becomeFirstResponder() }
        }
    }
}

// MARK: - Terminal with Control Toolbar

struct TerminalWithToolbar: View {
    @Binding var cells: [[TerminalCell]]
    @Binding var cursorRow: Int
    @Binding var cursorCol: Int
    @Binding var shouldFocus: Bool
    var onKeyInput: ((Character) -> Void)?
    var onSetControlify: ((DOSControlifyMode) -> Void)?
    var onMouseUpdate: ((Int, Int, Int) -> Void)?
    var isControlifyActive: Bool = false

    let rows: Int
    let cols: Int
    let fontSize: CGFloat

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 6) {
                ToolbarButton(title: "Ctrl", isActive: isControlifyActive) {
                    onSetControlify?(isControlifyActive ? .off : .oneChar)
                }
                ToolbarButton(title: "Esc", isActive: false) {
                    onSetControlify?(.off)
                    onKeyInput?(Character(UnicodeScalar(27)))
                }
                ToolbarButton(title: "Tab", isActive: false) {
                    onSetControlify?(.off)
                    onKeyInput?(Character(UnicodeScalar(9)))
                }
                Spacer()
            }
            .padding(.vertical, 8)
            .padding(.horizontal, 4)
            .background(Color(UIColor.systemGray5))

            TerminalView(
                cells: $cells,
                cursorRow: $cursorRow,
                cursorCol: $cursorCol,
                shouldFocus: $shouldFocus,
                onKeyInput: onKeyInput,
                onMouseUpdate: onMouseUpdate,
                rows: rows,
                cols: cols,
                fontSize: fontSize
            )
        }
    }
}

struct ToolbarButton: View {
    let title: String
    let isActive: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 14, weight: .medium))
                .frame(width: 36)
                .padding(.vertical, 6)
                .background(isActive ? Color.blue : Color(UIColor.systemGray4))
                .foregroundColor(isActive ? .white : .primary)
                .cornerRadius(6)
        }
        .buttonStyle(.plain)
    }
}

// MARK: - Terminal UI View

class TerminalUIView: UIView, UIKeyInput {
    var onKeyInput: ((Character) -> Void)?
    var onMouseUpdate: ((Int, Int, Int) -> Void)?

    private let rows: Int
    private let cols: Int
    private var cells: [[TerminalCell]] = []
    private var cursorRow: Int = 0
    private var cursorCol: Int = 0
    private var charWidth: CGFloat = 0
    private var charHeight: CGFloat = 0
    private var font: UIFont
    private var currentFontSize: CGFloat

    // Mouse tracking
    private var mouseButtons: Int = 0

    // Haptic feedback for keyboard
    private let keyFeedback = UIImpactFeedbackGenerator(style: .light)

    private let cgaColors: [UIColor] = [
        UIColor(red: 0/255, green: 0/255, blue: 0/255, alpha: 1),
        UIColor(red: 0/255, green: 0/255, blue: 170/255, alpha: 1),
        UIColor(red: 0/255, green: 170/255, blue: 0/255, alpha: 1),
        UIColor(red: 0/255, green: 170/255, blue: 170/255, alpha: 1),
        UIColor(red: 170/255, green: 0/255, blue: 0/255, alpha: 1),
        UIColor(red: 170/255, green: 0/255, blue: 170/255, alpha: 1),
        UIColor(red: 170/255, green: 85/255, blue: 0/255, alpha: 1),
        UIColor(red: 170/255, green: 170/255, blue: 170/255, alpha: 1),
        UIColor(red: 85/255, green: 85/255, blue: 85/255, alpha: 1),
        UIColor(red: 85/255, green: 85/255, blue: 255/255, alpha: 1),
        UIColor(red: 85/255, green: 255/255, blue: 85/255, alpha: 1),
        UIColor(red: 85/255, green: 255/255, blue: 255/255, alpha: 1),
        UIColor(red: 255/255, green: 85/255, blue: 85/255, alpha: 1),
        UIColor(red: 255/255, green: 85/255, blue: 255/255, alpha: 1),
        UIColor(red: 255/255, green: 255/255, blue: 85/255, alpha: 1),
        UIColor(red: 255/255, green: 255/255, blue: 255/255, alpha: 1)
    ]

    init(rows: Int, cols: Int, fontSize: CGFloat = 20) {
        self.rows = rows
        self.cols = cols
        self.currentFontSize = fontSize
        self.font = UIFont.monospacedSystemFont(ofSize: fontSize, weight: .regular)
        super.init(frame: .zero)
        updateCharDimensions()
        cells = Array(repeating: Array(repeating: TerminalCell(), count: cols), count: rows)
        backgroundColor = .black
        contentMode = .redraw
        autoresizingMask = [.flexibleWidth, .flexibleHeight]
        isMultipleTouchEnabled = false

        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        addGestureRecognizer(tap)

        let longPress = UILongPressGestureRecognizer(target: self, action: #selector(handleLongPress(_:)))
        addGestureRecognizer(longPress)

        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        addGestureRecognizer(pan)
    }

    required init?(coder: NSCoder) { fatalError() }

    override func layoutSubviews() {
        super.layoutSubviews()
        setNeedsDisplay()
    }

    private func updateCharDimensions() {
        let size = ("M" as NSString).size(withAttributes: [.font: font])
        charWidth = size.width
        charHeight = size.height
    }

    func updateFontSize(_ newSize: CGFloat) {
        guard newSize != currentFontSize else { return }
        currentFontSize = newSize
        font = UIFont.monospacedSystemFont(ofSize: newSize, weight: .regular)
        updateCharDimensions()
        setNeedsDisplay()
    }

    func updateCells(_ newCells: [[TerminalCell]], cursorRow: Int, cursorCol: Int) {
        self.cells = newCells
        self.cursorRow = cursorRow
        self.cursorCol = cursorCol
        setNeedsDisplay()
    }

    // MARK: - Coordinate conversion

    private func terminalLayout() -> (offsetX: CGFloat, offsetY: CGFloat, scale: CGFloat) {
        let tw = CGFloat(cols) * charWidth
        let th = CGFloat(rows) * charHeight
        let sx = bounds.width / tw
        let sy = bounds.height / th
        let s = min(sx, sy)
        let ox = (bounds.width - tw * s) / 2 + 2
        let oy = (bounds.height - th * s) / 2
        return (ox, oy, s)
    }

    /// Convert a view point to virtual DOS mouse coordinates (0-639, 0-199)
    private func viewPointToMouse(_ point: CGPoint) -> (x: Int, y: Int) {
        let layout = terminalLayout()
        // Normalize to 0..1 within the terminal area
        let nx = (point.x - layout.offsetX) / (CGFloat(cols) * charWidth * layout.scale)
        let ny = (point.y - layout.offsetY) / (CGFloat(rows) * charHeight * layout.scale)
        let mx = Int(max(0, min(639, nx * 640)))
        let my = Int(max(0, min(199, ny * 200)))
        return (mx, my)
    }

    // MARK: - Touch / Mouse

    @objc private func handleTap(_ gesture: UITapGestureRecognizer) {
        becomeFirstResponder()
        let pt = gesture.location(in: self)
        let (mx, my) = viewPointToMouse(pt)
        // Tap = left click + release
        onMouseUpdate?(mx, my, 1)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in
            self?.onMouseUpdate?(mx, my, 0)
        }
    }

    @objc private func handleLongPress(_ gesture: UILongPressGestureRecognizer) {
        guard gesture.state == .began else { return }
        becomeFirstResponder()

        // Long press = right click
        let pt = gesture.location(in: self)
        let (mx, my) = viewPointToMouse(pt)
        onMouseUpdate?(mx, my, 2)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) { [weak self] in
            self?.onMouseUpdate?(mx, my, 0)
        }
    }

    @objc private func handlePan(_ gesture: UIPanGestureRecognizer) {
        let pt = gesture.location(in: self)
        let (mx, my) = viewPointToMouse(pt)
        switch gesture.state {
        case .began:
            mouseButtons = 1  // Left button down while dragging
            onMouseUpdate?(mx, my, mouseButtons)
        case .changed:
            onMouseUpdate?(mx, my, mouseButtons)
        case .ended, .cancelled:
            mouseButtons = 0
            onMouseUpdate?(mx, my, 0)
        default:
            break
        }
    }

    // MARK: - Drawing

    override func draw(_ rect: CGRect) {
        guard let context = UIGraphicsGetCurrentContext() else { return }
        UIColor.black.setFill()
        context.fill(bounds)

        let layout = terminalLayout()
        context.saveGState()
        context.translateBy(x: layout.offsetX, y: layout.offsetY)
        context.scaleBy(x: layout.scale, y: layout.scale)

        for row in 0..<min(rows, cells.count) {
            for col in 0..<min(cols, cells[row].count) {
                let cell = cells[row][col]
                let x = CGFloat(col) * charWidth
                let y = CGFloat(row) * charHeight
                if cell.background != 0 {
                    cgaColors[Int(cell.background) & 0x0F].setFill()
                    context.fill(CGRect(x: x, y: y, width: charWidth, height: charHeight))
                }
                let attrs: [NSAttributedString.Key: Any] = [
                    .font: font,
                    .foregroundColor: cgaColors[Int(cell.foreground) & 0x0F]
                ]
                (String(cell.character) as NSString).draw(at: CGPoint(x: x, y: y), withAttributes: attrs)
            }
        }

        let cx = CGFloat(cursorCol) * charWidth
        let cy = CGFloat(cursorRow) * charHeight
        UIColor.green.withAlphaComponent(0.7).setFill()
        context.fill(CGRect(x: cx, y: cy, width: charWidth, height: charHeight))
        if cursorRow < cells.count && cursorCol < cells[cursorRow].count {
            let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: UIColor.black]
            (String(cells[cursorRow][cursorCol].character) as NSString).draw(at: CGPoint(x: cx, y: cy), withAttributes: attrs)
        }

        context.restoreGState()
    }

    // MARK: - UIKeyInput

    override var canBecomeFirstResponder: Bool { true }
    var hasText: Bool { true }

    func insertText(_ text: String) {
        keyFeedback.impactOccurred()
        for char in text { onKeyInput?(char) }
    }

    func deleteBackward() {
        keyFeedback.impactOccurred()
        onKeyInput?(Character(UnicodeScalar(8)))
    }

    override var keyCommands: [UIKeyCommand]? {
        var cmds = [
            UIKeyCommand(input: "\r", modifierFlags: [], action: #selector(enterKey)),
            UIKeyCommand(input: UIKeyCommand.inputEscape, modifierFlags: [], action: #selector(escKey)),
            UIKeyCommand(input: UIKeyCommand.inputUpArrow, modifierFlags: [], action: #selector(upKey)),
            UIKeyCommand(input: UIKeyCommand.inputDownArrow, modifierFlags: [], action: #selector(downKey)),
            UIKeyCommand(input: UIKeyCommand.inputLeftArrow, modifierFlags: [], action: #selector(leftKey)),
            UIKeyCommand(input: UIKeyCommand.inputRightArrow, modifierFlags: [], action: #selector(rightKey)),
            UIKeyCommand(input: "c", modifierFlags: .command, action: #selector(copyText)),
            UIKeyCommand(input: "v", modifierFlags: .command, action: #selector(pasteText))
        ]
        for ch in "abcdefghijklmnopqrstuvwxyz" {
            cmds.append(UIKeyCommand(input: String(ch), modifierFlags: .control, action: #selector(ctrlKey(_:))))
        }
        return cmds
    }

    @objc private func copyText() {
        var text = ""
        for row in cells {
            var line = row.map { String($0.character) }.joined()
            while line.hasSuffix(" ") { line.removeLast() }
            text += line + "\n"
        }
        while text.hasSuffix("\n\n") { text.removeLast() }
        UIPasteboard.general.string = text
    }

    @objc private func pasteText() {
        guard let text = UIPasteboard.general.string else { return }
        for ch in text { onKeyInput?(ch == "\n" ? Character("\r") : ch) }
    }

    @objc private func enterKey() { onKeyInput?(Character("\r")) }
    @objc private func escKey() { onKeyInput?(Character(UnicodeScalar(27))) }
    @objc private func upKey() { onKeyInput?(Character(UnicodeScalar(0xF700)!)) }
    @objc private func downKey() { onKeyInput?(Character(UnicodeScalar(0xF701)!)) }
    @objc private func leftKey() { onKeyInput?(Character(UnicodeScalar(0xF702)!)) }
    @objc private func rightKey() { onKeyInput?(Character(UnicodeScalar(0xF703)!)) }

    @objc private func ctrlKey(_ cmd: UIKeyCommand) {
        guard let ch = cmd.input?.first, let a = ch.asciiValue else { return }
        onKeyInput?(Character(UnicodeScalar(a - 96)))
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            guard let key = press.key else { continue }
            if key.modifierFlags.contains(.command) || key.modifierFlags.contains(.control) { continue }
            for ch in key.characters { onKeyInput?(ch); handled = true }
        }
        if !handled { super.pressesBegan(presses, with: event) }
    }

    override func canPerformAction(_ action: Selector, withSender sender: Any?) -> Bool {
        action == #selector(copyText) || action == #selector(copy(_:)) || super.canPerformAction(action, withSender: sender)
    }

    @objc override func copy(_ sender: Any?) { copyText() }
}
