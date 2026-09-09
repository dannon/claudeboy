import CoreBluetooth
import Foundation

// The contract with src/device/ble.cpp. Changing either means changing both.
let serviceUUID = CBUUID(string: "36979B4A-6F15-447A-A4AE-0DD4ECF0967B")
let snapshotUUID = CBUUID(string: "678C5468-AD9D-402F-A06E-267C729245F3")

let XFER_START: UInt8 = 0x01
let XFER_DATA: UInt8 = 0x02

// The agent connects here. There is no stdin under launchd, and a child process
// spawned by the agent would not have Bluetooth permission -- see the spec.
let socketPath = NSHomeDirectory() + "/Library/Application Support/claudeboy/ble.sock"

final class Helper: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var board: CBPeripheral?
    private var snapshot: CBCharacteristic?
    private var pending: Data?
    private var clients: [Int32] = []
    private let lock = NSLock()

    func start() {
        central = CBCentralManager(delegate: self, queue: nil)
        listenOnSocket()
    }

    // Status goes to every connected agent and to stdout, which is the launchd log.
    func emit(_ obj: [String: Any]) {
        guard let d = try? JSONSerialization.data(withJSONObject: obj),
              let body = String(data: d, encoding: .utf8) else { return }
        let line = body + "\n"
        print(line, terminator: "")
        fflush(stdout)
        lock.lock(); let fds = clients; lock.unlock()
        for fd in fds { _ = line.withCString { write(fd, $0, strlen($0)) } }
    }

    private func listenOnSocket() {
        let dir = (socketPath as NSString).deletingLastPathComponent
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        unlink(socketPath)   // a stale socket from a crashed run would block bind()

        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { emit(["error": "socket() failed"]); exit(1) }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let bytes = Array(socketPath.utf8)
        guard bytes.count < MemoryLayout.size(ofValue: addr.sun_path) else {
            emit(["error": "socket path too long: \(socketPath)"]); exit(1)
        }
        withUnsafeMutablePointer(to: &addr.sun_path) { p in
            p.withMemoryRebound(to: CChar.self, capacity: bytes.count + 1) { dst in
                for (i, b) in bytes.enumerated() { dst[i] = CChar(bitPattern: b) }
                dst[bytes.count] = 0
            }
        }
        let size = socklen_t(MemoryLayout<sockaddr_un>.size)
        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, size) }
        }
        guard bound == 0, Darwin.listen(fd, 4) == 0 else {
            emit(["error": "bind/listen failed on \(socketPath)"]); exit(1)
        }
        emit(["state": "listening", "socket": socketPath])

        Thread {
            while true {
                let c = accept(fd, nil, nil)
                if c < 0 { continue }
                self.lock.lock(); self.clients.append(c); self.lock.unlock()
                Thread { self.serve(c) }.start()
            }
        }.start()
    }

    private func serve(_ fd: Int32) {
        var carry = Data()
        var buf = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = read(fd, &buf, buf.count)
            if n <= 0 { break }
            carry.append(contentsOf: buf[0..<n])
            while let nl = carry.firstIndex(of: 0x0a) {
                let line = carry.subdata(in: carry.startIndex..<nl)
                carry = carry.subdata(in: (nl + 1)..<carry.endIndex)
                guard let o = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
                      let snap = o["snapshot"],
                      let payload = try? JSONSerialization.data(withJSONObject: snap)
                else { continue }
                DispatchQueue.main.async { self.send(payload) }
            }
        }
        lock.lock(); clients.removeAll { $0 == fd }; lock.unlock()
        close(fd)
    }

    private func send(_ body: Data) {
        guard let p = board, let c = snapshot else {
            pending = body   // not connected yet; send it the moment we are
            return
        }
        // .withResponse throughout, and not incidentally -- though not
        // because the board would refuse anything else. NimBLE dispatches
        // onWrite() for a Write Command too. What this buys is pacing: a Write
        // Request is confirmed and one in flight, so these frames arrive in
        // order and one at a time, which is what the reassembler assumes. A
        // dropped Write Command would leave a transfer short of its declared
        // length and cost a payload.
        let room = max(20, p.maximumWriteValueLength(for: .withResponse) - 1)

        var start = Data([XFER_START])
        var total = UInt32(body.count).littleEndian
        withUnsafeBytes(of: &total) { start.append(contentsOf: $0) }
        p.writeValue(start, for: c, type: .withResponse)

        var off = 0
        while off < body.count {
            let take = min(room, body.count - off)
            var frame = Data([XFER_DATA])
            frame.append(body.subdata(in: off..<(off + take)))
            p.writeValue(frame, for: c, type: .withResponse)
            off += take
        }
        emit(["wrote": body.count])
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        if c.state == .poweredOn {
            emit(["state": "scanning"])
            c.scanForPeripherals(withServices: [serviceUUID])
        } else {
            // State 3 is .unauthorized: TCC denied us. See Task 2.
            emit(["state": "unavailable", "raw": c.state.rawValue])
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        board = p
        p.delegate = self
        c.stopScan()
        c.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        emit(["state": "connected", "peripheral": p.identifier.uuidString])
        p.discoverServices([serviceUUID])
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral,
                        error: Error?) {
        emit(["state": "disconnected"])
        snapshot = nil
        c.scanForPeripherals(withServices: [serviceUUID])
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = p.services?.first(where: { $0.uuid == serviceUUID }) else { return }
        p.discoverCharacteristics([snapshotUUID], for: s)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard let ch = s.characteristics?.first(where: { $0.uuid == snapshotUUID }) else { return }
        snapshot = ch
        emit(["state": "ready", "mtu": p.maximumWriteValueLength(for: .withResponse)])
        if let body = pending { pending = nil; send(body) }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        if let e = error { emit(["error": e.localizedDescription]) }
    }
}

let helper = Helper()
helper.start()
RunLoop.main.run()
