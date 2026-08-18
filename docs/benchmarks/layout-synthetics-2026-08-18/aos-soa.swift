// Синтетика: AoS против SoA на горячем пути раскладки BDUI.
//
// Горячий путь моделируется двумя проходами по дереву виджетов, как в
// ContainerLayoutStrategy:
//   pass 1 (снизу вверх) — узел читает свои поля раскладки и размеры детей,
//                          пишет свой размер;
//   pass 2 (сверху вниз) — узел обходит детей, читает их поля, пишет их origin.
//
// Меряются четыре независимых решения:
//   * порядок узлов: BFS (дети непрерывны) против DFS (поддерево непрерывно);
//   * раскладка полей: AoS против SoA;
//   * ширина полей: Double/CGRect против Float32-упаковки;
//   * состояние кэша: горячий против вытесненного между кадрами.

import Foundation

// MARK: - Генератор дерева

struct RNG {
    var s: UInt64
    mutating func next() -> UInt64 {
        s = s &* 6364136223846793005 &+ 1442695040888963407
        return s >> 33
    }
    mutating func int(_ n: Int) -> Int { Int(next() % UInt64(n)) }
}

/// Дерево в BFS-порядке: дети каждого узла лежат непрерывным диапазоном.
struct BFSTree {
    var parent: [Int32] = []
    var childStart: [Int32] = []
    var childCount: [Int32] = []
    var depth: [Int32] = []
}

func makeTree(nodeCount: Int, seed: UInt64) -> BFSTree {
    var rng = RNG(s: seed)
    var t = BFSTree()
    t.parent = [-1]; t.childStart = [0]; t.childCount = [0]; t.depth = [0]

    var head = 0
    while t.parent.count < nodeCount && head < t.parent.count {
        let d = t.depth[head]
        // Ветвление как в реальных макетах: контейнеры вверху широкие, листья внизу.
        let k = d >= 8 ? 0 : max(0, min(1 + rng.int(4), nodeCount - t.parent.count))
        if k > 0 {
            t.childStart[head] = Int32(t.parent.count)
            t.childCount[head] = Int32(k)
            for _ in 0..<k {
                t.parent.append(Int32(head))
                t.childStart.append(0)
                t.childCount.append(0)
                t.depth.append(d + 1)
            }
        }
        head += 1
    }
    return t
}

// MARK: - Поля раскладки

enum SizeSpec { case fixed(Double), wrap, parent, floating }

struct Insets { var l: Double = 0, t: Double = 0, r: Double = 0, b: Double = 0 }

struct LayoutFat {
    var width: SizeSpec = .wrap
    var height: SizeSpec = .wrap
    var margins = Insets()
    var paddings = Insets()
    var alignment: UInt8 = 0
    var flags: UInt8 = 0
}

struct FrameFat { var x = 0.0, y = 0.0, w = 0.0, h = 0.0 }

/// Та же информация, но в Float32 и с видом размера, упакованным в байт.
struct SizeSpecP { var kind: UInt8 = 1; var value: Float = 0 }   // 0 fixed, 1 wrap, 2 parent, 3 floating

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

func makeLayouts(_ n: Int, seed: UInt64) -> ([LayoutFat], [LayoutPacked]) {
    var rng = RNG(s: seed)
    var fat: [LayoutFat] = []; fat.reserveCapacity(n)
    var packed: [LayoutPacked] = []; packed.reserveCapacity(n)
    for _ in 0..<n {
        let kindW = rng.int(4), kindH = rng.int(4)
        let vw = Double(rng.int(300)), vh = Double(rng.int(200))
        let m = Double(rng.int(16)), p = Double(rng.int(12))
        let al = UInt8(rng.int(3))

        func spec(_ k: Int, _ v: Double) -> SizeSpec {
            switch k { case 0: return .fixed(v); case 1: return .wrap; case 2: return .parent; default: return .floating }
        }
        fat.append(LayoutFat(width: spec(kindW, vw), height: spec(kindH, vh),
                             margins: Insets(l: m, t: m, r: m, b: m),
                             paddings: Insets(l: p, t: p, r: p, b: p),
                             alignment: al, flags: 0))
        packed.append(LayoutPacked(width: SizeSpecP(kind: UInt8(kindW), value: Float(vw)),
                                   height: SizeSpecP(kind: UInt8(kindH), value: Float(vh)),
                                   margins: InsetsP(l: Float(m), t: Float(m), r: Float(m), b: Float(m)),
                                   paddings: InsetsP(l: Float(p), t: Float(p), r: Float(p), b: Float(p)),
                                   alignment: al, flags: 0))
    }
    return (fat, packed)
}

// MARK: - Хранилища

/// AoS: всё про узел в одной структуре.
struct NodeFat {
    var parent: Int32 = -1
    var childStart: Int32 = 0
    var childCount: Int32 = 0
    var subtreeSize: Int32 = 0
    var kind: UInt8 = 0
    var layout = LayoutFat()
    var frame = FrameFat()
}

struct NodePacked {
    var parent: Int32 = -1
    var childStart: Int32 = 0
    var childCount: Int32 = 0
    var subtreeSize: Int32 = 0
    var kind: UInt8 = 0
    var layout = LayoutPacked()
    var frame = FramePacked()
}

/// SoA: отдельный массив на поле.
struct SoAFat {
    var parent: [Int32] = [], childStart: [Int32] = [], childCount: [Int32] = [], subtreeSize: [Int32] = []
    var kind: [UInt8] = []
    var layout: [LayoutFat] = []
    var frame: [FrameFat] = []
}

struct SoAPacked {
    var parent: [Int32] = [], childStart: [Int32] = [], childCount: [Int32] = [], subtreeSize: [Int32] = []
    var kind: [UInt8] = []
    var layout: [LayoutPacked] = []
    var frame: [FramePacked] = []
}

// MARK: - Проходы раскладки

// Одна и та же арифметика во всех вариантах — результат сверяется контрольной суммой.

@inline(__always)
func resolveFat(_ s: SizeSpec, content: Double, available: Double) -> Double {
    switch s {
    case .fixed(let v): return v
    case .wrap: return content
    case .parent: return available
    case .floating: return available * 0.5
    }
}

@inline(__always)
func resolvePacked(_ s: SizeSpecP, content: Float, available: Float) -> Float {
    switch s.kind {
    case 0: return s.value
    case 1: return content
    case 2: return available
    default: return available * 0.5
    }
}

// --- BFS + AoS (fat)

func layoutBFS_AoS_Fat(_ nodes: inout [NodeFat], root: Double) -> Double {
    let n = nodes.count
    nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw = 0.0, ch = 0.0
            var j = cs
            while j < cs + cc {
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r)
                ch += f.h + m.t + m.b
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveFat(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveFat(b[i].layout.height, content: ch + p.t + p.b, available: root)
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
                let m = b[j].layout.margins
                let al = b[j].layout.alignment
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
    for x in nodes { sum += x.frame.x + x.frame.y + x.frame.w + x.frame.h }
    return sum
}

// --- BFS + SoA (fat)

func layoutBFS_SoA_Fat(_ s: inout SoAFat, root: Double) -> Double {
    let n = s.parent.count
    s.frame.withUnsafeMutableBufferPointer { fr in
      s.layout.withUnsafeBufferPointer { lay in
        s.childStart.withUnsafeBufferPointer { csA in
          s.childCount.withUnsafeBufferPointer { ccA in
            var i = n - 1
            while i >= 0 {
                let cs = Int(csA[i]), cc = Int(ccA[i])
                var cw = 0.0, ch = 0.0
                var j = cs
                while j < cs + cc {
                    let f = fr[j], m = lay[j].margins
                    cw = max(cw, f.w + m.l + m.r)
                    ch += f.h + m.t + m.b
                    j += 1
                }
                let p = lay[i].paddings
                fr[i].w = resolveFat(lay[i].width, content: cw + p.l + p.r, available: root)
                fr[i].h = resolveFat(lay[i].height, content: ch + p.t + p.b, available: root)
                i -= 1
            }
            var k = 0
            while k < n {
                let cs = Int(csA[k]), cc = Int(ccA[k])
                let p = lay[k].paddings
                let ox = fr[k].x + p.l
                var oy = fr[k].y + p.t
                let inner = fr[k].w - p.l - p.r
                var j = cs
                while j < cs + cc {
                    let m = lay[j].margins
                    let al = lay[j].alignment
                    let free = inner - fr[j].w - m.l - m.r
                    fr[j].x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                    fr[j].y = oy + m.t
                    oy += fr[j].h + m.t + m.b
                    j += 1
                }
                k += 1
            }
          }
        }
      }
    }
    var sum = 0.0
    for x in s.frame { sum += x.x + x.y + x.w + x.h }
    return sum
}

// --- BFS + AoS (packed)

func layoutBFS_AoS_Packed(_ nodes: inout [NodePacked], root: Float) -> Double {
    let n = nodes.count
    nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r)
                ch += f.h + m.t + m.b
                j += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolvePacked(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolvePacked(b[i].layout.height, content: ch + p.t + p.b, available: root)
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
                let m = b[j].layout.margins
                let al = b[j].layout.alignment
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

// --- BFS + SoA (packed)

func layoutBFS_SoA_Packed(_ s: inout SoAPacked, root: Float) -> Double {
    let n = s.parent.count
    s.frame.withUnsafeMutableBufferPointer { fr in
      s.layout.withUnsafeBufferPointer { lay in
        s.childStart.withUnsafeBufferPointer { csA in
          s.childCount.withUnsafeBufferPointer { ccA in
            var i = n - 1
            while i >= 0 {
                let cs = Int(csA[i]), cc = Int(ccA[i])
                var cw: Float = 0, ch: Float = 0
                var j = cs
                while j < cs + cc {
                    let f = fr[j], m = lay[j].margins
                    cw = max(cw, f.w + m.l + m.r)
                    ch += f.h + m.t + m.b
                    j += 1
                }
                let p = lay[i].paddings
                fr[i].w = resolvePacked(lay[i].width, content: cw + p.l + p.r, available: root)
                fr[i].h = resolvePacked(lay[i].height, content: ch + p.t + p.b, available: root)
                i -= 1
            }
            var k = 0
            while k < n {
                let cs = Int(csA[k]), cc = Int(ccA[k])
                let p = lay[k].paddings
                let ox = fr[k].x + p.l
                var oy = fr[k].y + p.t
                let inner = fr[k].w - p.l - p.r
                var j = cs
                while j < cs + cc {
                    let m = lay[j].margins
                    let al = lay[j].alignment
                    let free = inner - fr[j].w - m.l - m.r
                    fr[j].x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                    fr[j].y = oy + m.t
                    oy += fr[j].h + m.t + m.b
                    j += 1
                }
                k += 1
            }
          }
        }
      }
    }
    var sum = 0.0
    for x in s.frame { sum += Double(x.x + x.y + x.w + x.h) }
    return sum
}

// --- DFS: дети не непрерывны, нужен боковой массив индексов

func layoutDFS_AoS_Fat(_ nodes: inout [NodeFat], childIdx: [Int32], root: Double) -> Double {
    let n = nodes.count
    childIdx.withUnsafeBufferPointer { ci in
      nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw = 0.0, ch = 0.0
            var t = cs
            while t < cs + cc {
                let j = Int(ci[t])
                let f = b[j].frame, m = b[j].layout.margins
                cw = max(cw, f.w + m.l + m.r)
                ch += f.h + m.t + m.b
                t += 1
            }
            let p = b[i].layout.paddings
            b[i].frame.w = resolveFat(b[i].layout.width, content: cw + p.l + p.r, available: root)
            b[i].frame.h = resolveFat(b[i].layout.height, content: ch + p.t + p.b, available: root)
            i -= 1
        }
        var k = 0
        while k < n {
            let cs = Int(b[k].childStart), cc = Int(b[k].childCount)
            let p = b[k].layout.paddings
            let ox = b[k].frame.x + p.l
            var oy = b[k].frame.y + p.t
            let inner = b[k].frame.w - p.l - p.r
            var t = cs
            while t < cs + cc {
                let j = Int(ci[t])
                let m = b[j].layout.margins
                let al = b[j].layout.alignment
                let free = inner - b[j].frame.w - m.l - m.r
                b[j].frame.x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                b[j].frame.y = oy + m.t
                oy += b[j].frame.h + m.t + m.b
                t += 1
            }
            k += 1
        }
      }
    }
    var sum = 0.0
    for x in nodes { sum += x.frame.x + x.frame.y + x.frame.w + x.frame.h }
    return sum
}

func layoutDFS_SoA_Fat(_ s: inout SoAFat, childIdx: [Int32], root: Double) -> Double {
    let n = s.parent.count
    childIdx.withUnsafeBufferPointer { ci in
      s.frame.withUnsafeMutableBufferPointer { fr in
        s.layout.withUnsafeBufferPointer { lay in
          s.childStart.withUnsafeBufferPointer { csA in
            s.childCount.withUnsafeBufferPointer { ccA in
              var i = n - 1
              while i >= 0 {
                  let cs = Int(csA[i]), cc = Int(ccA[i])
                  var cw = 0.0, ch = 0.0
                  var t = cs
                  while t < cs + cc {
                      let j = Int(ci[t])
                      let f = fr[j], m = lay[j].margins
                      cw = max(cw, f.w + m.l + m.r)
                      ch += f.h + m.t + m.b
                      t += 1
                  }
                  let p = lay[i].paddings
                  fr[i].w = resolveFat(lay[i].width, content: cw + p.l + p.r, available: root)
                  fr[i].h = resolveFat(lay[i].height, content: ch + p.t + p.b, available: root)
                  i -= 1
              }
              var k = 0
              while k < n {
                  let cs = Int(csA[k]), cc = Int(ccA[k])
                  let p = lay[k].paddings
                  let ox = fr[k].x + p.l
                  var oy = fr[k].y + p.t
                  let inner = fr[k].w - p.l - p.r
                  var t = cs
                  while t < cs + cc {
                      let j = Int(ci[t])
                      let m = lay[j].margins
                      let al = lay[j].alignment
                      let free = inner - fr[j].w - m.l - m.r
                      fr[j].x = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
                      fr[j].y = oy + m.t
                      oy += fr[j].h + m.t + m.b
                      t += 1
                  }
                  k += 1
              }
            }
          }
        }
      }
    }
    var sum = 0.0
    for x in s.frame { sum += x.x + x.y + x.w + x.h }
    return sum
}

// MARK: - Сборка вариантов

struct Variants {
    var aosFat: [NodeFat]
    var soaFat: SoAFat
    var aosPacked: [NodePacked]
    var soaPacked: SoAPacked
    var dfsAosFat: [NodeFat]
    var dfsSoaFat: SoAFat
    var dfsChildIdx: [Int32]
}

func buildAll(nodeCount: Int, seed: UInt64) -> Variants {

    let t = makeTree(nodeCount: nodeCount, seed: seed)
    let n = t.parent.count
    let (lf, lp) = makeLayouts(n, seed: seed &+ 77)

    // BFS-варианты
    var aosFat = [NodeFat](repeating: NodeFat(), count: n)
    var aosPacked = [NodePacked](repeating: NodePacked(), count: n)
    var soaFat = SoAFat(); var soaPacked = SoAPacked()
    soaFat.parent = t.parent; soaFat.childStart = t.childStart; soaFat.childCount = t.childCount
    soaFat.subtreeSize = [Int32](repeating: 0, count: n); soaFat.kind = [UInt8](repeating: 0, count: n)
    soaFat.layout = lf; soaFat.frame = [FrameFat](repeating: FrameFat(), count: n)
    soaPacked.parent = t.parent; soaPacked.childStart = t.childStart; soaPacked.childCount = t.childCount
    soaPacked.subtreeSize = [Int32](repeating: 0, count: n); soaPacked.kind = [UInt8](repeating: 0, count: n)
    soaPacked.layout = lp; soaPacked.frame = [FramePacked](repeating: FramePacked(), count: n)

    for i in 0..<n {
        aosFat[i].parent = t.parent[i]; aosFat[i].childStart = t.childStart[i]
        aosFat[i].childCount = t.childCount[i]; aosFat[i].layout = lf[i]
        aosPacked[i].parent = t.parent[i]; aosPacked[i].childStart = t.childStart[i]
        aosPacked[i].childCount = t.childCount[i]; aosPacked[i].layout = lp[i]
    }

    // DFS-перестановка: поддерево непрерывно, дети — нет.
    var order: [Int32] = []; order.reserveCapacity(n)
    var stack: [Int32] = [0]
    while let v = stack.popLast() {
        order.append(v)
        let cs = Int(t.childStart[Int(v)]), cc = Int(t.childCount[Int(v)])
        var j = cs + cc - 1
        while j >= cs { stack.append(Int32(j)); j -= 1 }
    }
    var newIndex = [Int32](repeating: -1, count: n)
    for (newI, oldI) in order.enumerated() { newIndex[Int(oldI)] = Int32(newI) }

    var dfsAos = [NodeFat](repeating: NodeFat(), count: n)
    var dfsSoa = SoAFat()
    dfsSoa.parent = [Int32](repeating: -1, count: n)
    dfsSoa.childStart = [Int32](repeating: 0, count: n)
    dfsSoa.childCount = [Int32](repeating: 0, count: n)
    dfsSoa.subtreeSize = [Int32](repeating: 0, count: n)
    dfsSoa.kind = [UInt8](repeating: 0, count: n)
    dfsSoa.layout = [LayoutFat](repeating: LayoutFat(), count: n)
    dfsSoa.frame = [FrameFat](repeating: FrameFat(), count: n)

    var childIdx: [Int32] = []; childIdx.reserveCapacity(n)
    for newI in 0..<n {
        let oldI = Int(order[newI])
        let start = Int32(childIdx.count)
        let cs = Int(t.childStart[oldI]), cc = Int(t.childCount[oldI])
        for j in cs..<(cs + cc) { childIdx.append(newIndex[j]) }
        dfsAos[newI].childStart = start; dfsAos[newI].childCount = Int32(cc)
        dfsAos[newI].parent = t.parent[oldI] < 0 ? -1 : newIndex[Int(t.parent[oldI])]
        dfsAos[newI].layout = lf[oldI]
        dfsSoa.childStart[newI] = start; dfsSoa.childCount[newI] = Int32(cc)
        dfsSoa.parent[newI] = dfsAos[newI].parent
        dfsSoa.layout[newI] = lf[oldI]
    }

    return Variants(aosFat: aosFat, soaFat: soaFat, aosPacked: aosPacked, soaPacked: soaPacked,
                    dfsAosFat: dfsAos, dfsSoaFat: dfsSoa, dfsChildIdx: childIdx)
}


// MARK: - Мерялка

// Горячий режим — одно дерево, кадр за кадром: всё живёт в кэше.
// Холодный режим — R деревьев по кругу; суммарно они не влезают в кэш, поэтому
// каждый кадр приходит к данным, вытесненным предыдущими. Искусственной
// прогулки по буферу нет: она стоила бы 90 мкс и утопила бы измерение.

func bench(_ name: String, iters: Int, _ body: (Int) -> Double) -> Double {
    var best = Double.infinity
    var checksum = 0.0
    for _ in 0..<7 {
        let t0 = DispatchTime.now().uptimeNanoseconds
        for it in 0..<iters { checksum = body(it) }
        let dt = Double(DispatchTime.now().uptimeNanoseconds - t0) / Double(iters)
        best = min(best, dt)
    }
    print(String(format: "  %-18@ %9.1f нс   (чек %.0f)", name as NSString, best, checksum))
    return best
}

print("Размеры структур:")
print("  NodeFat    \(MemoryLayout<NodeFat>.stride) Б   LayoutFat \(MemoryLayout<LayoutFat>.stride) Б   FrameFat \(MemoryLayout<FrameFat>.stride) Б")
print("  NodePacked \(MemoryLayout<NodePacked>.stride) Б   LayoutPacked \(MemoryLayout<LayoutPacked>.stride) Б   FramePacked \(MemoryLayout<FramePacked>.stride) Б")

for n in [200, 1200, 6000] {
    var one = buildAll(nodeCount: n, seed: 42)
    let real = one.aosFat.count
    let bytesAoS = real * MemoryLayout<NodeFat>.stride

    print("\n══════ узлов \(real) — AoS fat \(bytesAoS / 1024) КиБ ══════")

    // --- горячий
    let iters = max(50, 3_000_000 / real)
    print("\n[кэш горячий]")
    let ha = bench("BFS AoS  fat",    iters: iters) { _ in layoutBFS_AoS_Fat(&one.aosFat, root: 390) }
    let hb = bench("BFS SoA  fat",    iters: iters) { _ in layoutBFS_SoA_Fat(&one.soaFat, root: 390) }
    let hc = bench("BFS AoS  packed", iters: iters) { _ in layoutBFS_AoS_Packed(&one.aosPacked, root: 390) }
    let hd = bench("BFS SoA  packed", iters: iters) { _ in layoutBFS_SoA_Packed(&one.soaPacked, root: 390) }
    let he = bench("DFS AoS  fat",    iters: iters) { _ in layoutDFS_AoS_Fat(&one.dfsAosFat, childIdx: one.dfsChildIdx, root: 390) }
    let hf = bench("DFS SoA  fat",    iters: iters) { _ in layoutDFS_SoA_Fat(&one.dfsSoaFat, childIdx: one.dfsChildIdx, root: 390) }
    print(String(format: "  → SoA/AoS %.2f   packed/fat %.2f   DFS/BFS %.2f   SoA/AoS(packed) %.2f",
                 hb / ha, hc / ha, he / ha, hd / hc))

    // --- холодный: R деревьев по кругу, суммарно ~24 МиБ
    let R = max(2, min(48, (24 << 20) / max(bytesAoS, 1)))
    var many: [Variants] = []
    many.reserveCapacity(R)
    for r in 0..<R { many.append(buildAll(nodeCount: n, seed: UInt64(42 + r))) }

    print("[кэш вытеснен — \(R) деревьев по кругу, \(R * bytesAoS / 1024 / 1024) МиБ]")
    let citers = max(50, 400_000 / real)
    let ca = bench("BFS AoS  fat",    iters: citers) { i in layoutBFS_AoS_Fat(&many[i % R].aosFat, root: 390) }
    let cb = bench("BFS SoA  fat",    iters: citers) { i in layoutBFS_SoA_Fat(&many[i % R].soaFat, root: 390) }
    let cc = bench("BFS AoS  packed", iters: citers) { i in layoutBFS_AoS_Packed(&many[i % R].aosPacked, root: 390) }
    let cd = bench("BFS SoA  packed", iters: citers) { i in layoutBFS_SoA_Packed(&many[i % R].soaPacked, root: 390) }
    let ce = bench("DFS AoS  fat",    iters: citers) { i in layoutDFS_AoS_Fat(&many[i % R].dfsAosFat, childIdx: many[i % R].dfsChildIdx, root: 390) }
    let cf = bench("DFS SoA  fat",    iters: citers) { i in layoutDFS_SoA_Fat(&many[i % R].dfsSoaFat, childIdx: many[i % R].dfsChildIdx, root: 390) }
    print(String(format: "  → SoA/AoS %.2f   packed/fat %.2f   DFS/BFS %.2f   SoA/AoS(packed) %.2f",
                 cb / ca, cc / ca, ce / ca, cd / cc))
    _ = (hf, cf)
}
