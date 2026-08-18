// Формы доступа к плоскому дереву: сколько стоит безопасность и что дают
// borrowing / ~Copyable / Span на горячем пути раскладки.
//
// Считается ровно тот же проход, что и в основном бенче (BFS + AoS + packed —
// победитель прошлого прогона), в шести формах:
//   1. unsafe   — withUnsafeMutableBufferPointer, потолок;
//   2. array    — обычный subscript у [T], с проверками границ и эксклюзивности;
//   3. span     — MutableSpan (Swift 6.2): безопасно, без ARC, ~Escapable;
//   4. borrow   — unsafe + вынесенные @inline(never) помощники с borrowing;
//   5. byValue  — то же, но помощники берут узел по значению (копия 88 байт);
//   6. noncopy  — ~Copyable-хранилище, владеющее буфером единолично.

import Foundation

// MARK: - Данные

struct SizeSpecP { var kind: UInt8 = 1; var value: Float = 0 }
struct InsetsP { var l: Float = 0, t: Float = 0, r: Float = 0, b: Float = 0 }

struct LayoutPacked {
    var width = SizeSpecP()
    var height = SizeSpecP()
    var margins = InsetsP()
    var paddings = InsetsP()
    var alignment: UInt8 = 0
    var flags: UInt8 = 0
}

struct FramePacked { var x: Float = 0, y: Float = 0, w: Float = 0, h: Float = 0 }

struct NodeP {
    var parent: Int32 = -1
    var childStart: Int32 = 0
    var childCount: Int32 = 0
    var subtreeSize: Int32 = 0
    var kind: UInt8 = 0
    var layout = LayoutPacked()
    var frame = FramePacked()
}

@inline(__always)
func resolveP(_ s: SizeSpecP, content: Float, available: Float) -> Float {
    switch s.kind {
    case 0: return s.value
    case 1: return content
    case 2: return available
    default: return available * 0.5
    }
}

// MARK: - Дерево

struct RNG {
    var s: UInt64
    mutating func next() -> UInt64 { s = s &* 6364136223846793005 &+ 1442695040888963407; return s >> 33 }
    mutating func int(_ n: Int) -> Int { Int(next() % UInt64(n)) }
}

func makeNodes(_ nodeCount: Int, seed: UInt64) -> [NodeP] {
    var rng = RNG(s: seed)
    var parent: [Int32] = [-1], childStart: [Int32] = [0], childCount: [Int32] = [0], depth: [Int32] = [0]

    var head = 0
    while parent.count < nodeCount && head < parent.count {
        let d = depth[head]
        let k = d >= 8 ? 0 : max(0, min(1 + rng.int(4), nodeCount - parent.count))
        if k > 0 {
            childStart[head] = Int32(parent.count)
            childCount[head] = Int32(k)
            for _ in 0..<k { parent.append(Int32(head)); childStart.append(0); childCount.append(0); depth.append(d + 1) }
        }
        head += 1
    }

    let n = parent.count
    var nodes = [NodeP](repeating: NodeP(), count: n)
    for i in 0..<n {
        nodes[i].parent = parent[i]
        nodes[i].childStart = childStart[i]
        nodes[i].childCount = childCount[i]
        let kw = rng.int(4), kh = rng.int(4)
        nodes[i].layout = LayoutPacked(
            width: SizeSpecP(kind: UInt8(kw), value: Float(rng.int(300))),
            height: SizeSpecP(kind: UInt8(kh), value: Float(rng.int(200))),
            margins: InsetsP(l: Float(rng.int(16)), t: 4, r: 4, b: 4),
            paddings: InsetsP(l: Float(rng.int(12)), t: 6, r: 6, b: 6),
            alignment: UInt8(rng.int(3)), flags: 0)
    }
    return nodes
}

// MARK: - 1. unsafe

func layoutUnsafe(_ nodes: inout [NodeP], root: Float) -> Double {
    let n = nodes.count
    nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r); ch += f.h + m.t + m.b
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveP(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveP(b[i].layout.height, content: ch + p.t + p.b, available: root)
            i -= 1
        }
        var k = 0
        while k < n {
            let cs = Int(b[k].childStart), cc = Int(b[k].childCount)
            let p = b[k].layout.paddings
            let ox = b[k].frame.x + p.l
            var oy = b[k].frame.y + p.t
            let inner = b[k].frame.w - p.l - p.r
            var j = cs
            while j < cs + cc {
                let m = b[j].layout.margins, al = b[j].layout.alignment
                let free = inner - b[j].frame.w - m.l - m.r
                b[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                b[j].frame.y = oy + m.t
                oy += b[j].frame.h + m.t + m.b
                j += 1
            }
            k += 1
        }
    }
    var sum = 0.0
    for x in nodes { sum += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }
    return sum
}

// MARK: - 2. обычный массив

func layoutArray(_ nodes: inout [NodeP], root: Float) -> Double {
    let n = nodes.count
    var i = n - 1
    while i >= 0 {
        let cs = Int(nodes[i].childStart), cc = Int(nodes[i].childCount)
        var cw: Float = 0, ch: Float = 0
        var j = cs
        while j < cs + cc {
            let f = nodes[j].frame, m = nodes[j].layout.margins
            cw = max(cw, f.w + m.l + m.r); ch += f.h + m.t + m.b
            j += 1
        }
        let p = nodes[i].layout.paddings
        nodes[i].frame.w = resolveP(nodes[i].layout.width, content: cw + p.l + p.r, available: root)
        nodes[i].frame.h = resolveP(nodes[i].layout.height, content: ch + p.t + p.b, available: root)
        i -= 1
    }
    var k = 0
    while k < n {
        let cs = Int(nodes[k].childStart), cc = Int(nodes[k].childCount)
        let p = nodes[k].layout.paddings
        let ox = nodes[k].frame.x + p.l
        var oy = nodes[k].frame.y + p.t
        let inner = nodes[k].frame.w - p.l - p.r
        var j = cs
        while j < cs + cc {
            let m = nodes[j].layout.margins, al = nodes[j].layout.alignment
            let free = inner - nodes[j].frame.w - m.l - m.r
            nodes[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
            nodes[j].frame.y = oy + m.t
            oy += nodes[j].frame.h + m.t + m.b
            j += 1
        }
        k += 1
    }
    var sum = 0.0
    for x in nodes { sum += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }
    return sum
}

// MARK: - 3. MutableSpan

func layoutSpan(_ nodes: inout [NodeP], root: Float) -> Double {
    let n = nodes.count
    do {
        var b = nodes.mutableSpan
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r); ch += f.h + m.t + m.b
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveP(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveP(b[i].layout.height, content: ch + p.t + p.b, available: root)
            i -= 1
        }
        var k = 0
        while k < n {
            let cs = Int(b[k].childStart), cc = Int(b[k].childCount)
            let p = b[k].layout.paddings
            let ox = b[k].frame.x + p.l
            var oy = b[k].frame.y + p.t
            let inner = b[k].frame.w - p.l - p.r
            var j = cs
            while j < cs + cc {
                let m = b[j].layout.margins, al = b[j].layout.alignment
                let free = inner - b[j].frame.w - m.l - m.r
                b[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                b[j].frame.y = oy + m.t
                oy += b[j].frame.h + m.t + m.b
                j += 1
            }
            k += 1
        }
    }
    var sum = 0.0
    for x in nodes { sum += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }
    return sum
}

// MARK: - 4/5. borrowing против передачи по значению

@inline(never)
func contribBorrow(_ node: borrowing NodeP) -> (Float, Float) {
    let f = node.frame, m = node.layout.margins
    return (f.w + m.l + m.r, f.h + m.t + m.b)
}

@inline(never)
func contribValue(_ node: NodeP) -> (Float, Float) {
    let f = node.frame, m = node.layout.margins
    return (f.w + m.l + m.r, f.h + m.t + m.b)
}

func layoutWithHelper(_ nodes: inout [NodeP], root: Float, borrow: Bool) -> Double {
    let n = nodes.count
    nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                let (w, h) = borrow ? contribBorrow(b[j]) : contribValue(b[j])
                cw = max(cw, w); ch += h
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveP(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveP(b[i].layout.height, content: ch + p.t + p.b, available: root)
            i -= 1
        }
        var k = 0
        while k < n {
            let cs = Int(b[k].childStart), cc = Int(b[k].childCount)
            let p = b[k].layout.paddings
            let ox = b[k].frame.x + p.l
            var oy = b[k].frame.y + p.t
            let inner = b[k].frame.w - p.l - p.r
            var j = cs
            while j < cs + cc {
                let m = b[j].layout.margins, al = b[j].layout.alignment
                let free = inner - b[j].frame.w - m.l - m.r
                b[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                b[j].frame.y = oy + m.t
                oy += b[j].frame.h + m.t + m.b
                j += 1
            }
            k += 1
        }
    }
    var sum = 0.0
    for x in nodes { sum += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }
    return sum
}

// MARK: - 6. ~Copyable хранилище

/// Владеет буфером единолично: скопировать нельзя, значит и совместного
/// доступа быть не может — эксклюзивность доказана типом, а не проверкой.
struct NodeStore: ~Copyable {
    private let buf: UnsafeMutableBufferPointer<NodeP>

    init(_ source: [NodeP]) {
        buf = UnsafeMutableBufferPointer<NodeP>.allocate(capacity: source.count)
        _ = buf.initialize(fromContentsOf: source)
    }

    deinit {
        buf.deinitialize()
        buf.deallocate()
    }

    var count: Int { buf.count }

    @inline(__always)
    subscript(i: Int) -> NodeP {
        get { buf[i] }
        set { buf[i] = newValue }
    }

    @inline(__always)
    func layout(root: Float) -> Double {
        let b = buf
        let n = b.count
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r); ch += f.h + m.t + m.b
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveP(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveP(b[i].layout.height, content: ch + p.t + p.b, available: root)
            i -= 1
        }
        var k = 0
        while k < n {
            let cs = Int(b[k].childStart), cc = Int(b[k].childCount)
            let p = b[k].layout.paddings
            let ox = b[k].frame.x + p.l
            var oy = b[k].frame.y + p.t
            let inner = b[k].frame.w - p.l - p.r
            var j = cs
            while j < cs + cc {
                let m = b[j].layout.margins, al = b[j].layout.alignment
                let free = inner - b[j].frame.w - m.l - m.r
                b[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                b[j].frame.y = oy + m.t
                oy += b[j].frame.h + m.t + m.b
                j += 1
            }
            k += 1
        }
        var sum = 0.0
        for x in b { sum += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }
        return sum
    }
}

// MARK: - Прогон

func bench(_ name: String, iters: Int, _ body: () -> Double) -> (Double, Double) {
    var best = Double.infinity
    var checksum = 0.0
    for _ in 0..<9 {
        let t0 = DispatchTime.now().uptimeNanoseconds
        for _ in 0..<iters { checksum = body() }
        best = min(best, Double(DispatchTime.now().uptimeNanoseconds - t0) / Double(iters))
    }
    return (best, checksum)
}

print("NodeP = \(MemoryLayout<NodeP>.stride) байт")

for n in [200, 1200, 6000] {
    var nodes = makeNodes(n, seed: 42)
    let real = nodes.count
    let iters = max(50, 3_000_000 / real)
    print("\n══════ узлов \(real) ══════")

    var store = NodeStore(nodes)

    let results: [(String, (Double, Double))] = [
        ("1 unsafe",          bench("", iters: iters) { layoutUnsafe(&nodes, root: 390) }),
        ("2 array",           bench("", iters: iters) { layoutArray(&nodes, root: 390) }),
        ("3 span",            bench("", iters: iters) { layoutSpan(&nodes, root: 390) }),
        ("4 borrow helper",   bench("", iters: iters) { layoutWithHelper(&nodes, root: 390, borrow: true) }),
        ("5 byValue helper",  bench("", iters: iters) { layoutWithHelper(&nodes, root: 390, borrow: false) }),
        ("6 ~Copyable store", bench("", iters: iters) { store.layout(root: 390) }),
    ]

    let base = results[0].1.0
    for (name, r) in results {
        print(String(format: "  %-18@ %9.1f нс   ×%.2f   (чек %.0f)", name as NSString, r.0, r.0 / base, r.1))
    }
}
