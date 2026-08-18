// Сравнение: нынешний подход OKBDUI против плоского AoS.
//
// Вариант A воспроизводит горячий путь `_WidgetStateRef.sizeThatFits` как он
// написан сегодня, по исходникам OKBDUI:
//   * узлы — классы, дети держатся сильными ссылками в ChildSlots (ARC);
//   * стратегия раскладки — экзистенциал `any ILayoutStrategy` с обобщённым
//     методом, то есть вызов через таблицу свидетелей без специализации;
//   * `layoutChilds` начинается с `state as? ContainerState` — динамический
//     каст на каждый узел на каждый проход;
//   * слот детей достаётся `childSlots.children(of: "items")` — поиск в
//     словаре по строке на каждый узел на каждый проход;
//   * `visible` дёргает `associatedState as? ContentDrivenVisibility` —
//     второй динамический каст на каждого ребёнка;
//   * обход рекурсивный.
//
// Вариант B — те же классы и та же рекурсия, но стратегия конкретного типа и
// без кастов со словарём. Нужен, чтобы разделить вклад «объекты и рекурсия»
// от вклада «стирание типов».
//
// Вариант C — плоский AoS с упакованными полями и циклами (из perf.swift).
//
// Арифметика во всех трёх одна и та же, результат сверяется контрольной суммой.

import Foundation

// MARK: - Общие поля раскладки

enum SizeType: Hashable {
    case fixed(Double)
    case wrapContent(compressionPriority: Int)
    case floating
    case parent

    var fixedSize: Bool { if case .fixed = self { return true }; return false }
}

struct Paddings { var l = 0.0, t = 0.0, r = 0.0, b = 0.0 }
struct Margins { var l = 0.0, t = 0.0, r = 0.0, b = 0.0 }

struct WidgetLayout {
    let width: SizeType
    let height: SizeType
    let paddings: Paddings
    let margins: Margins
    let alignment: Int
}

@inline(__always)
func resolve(_ s: SizeType, content: Double, available: Double) -> Double {
    switch s {
    case .fixed(let v): return v
    case .wrapContent: return content
    case .parent: return available
    case .floating: return available * 0.5
    }
}

// MARK: - ChildSlots (как в OKBDUI)

struct ChildSlots<Element>: Sequence {
    struct Slot: Sequence {
        enum Storage {
            case single(Element)
            case array(Slice<ContiguousArray<Element>>)
            case empty
        }
        let storage: Storage

        struct Iterator: IteratorProtocol {
            private let storage: Storage
            private var index = 0
            private var sliceIterator: Slice<ContiguousArray<Element>>.Iterator?
            init(storage: Storage) {
                self.storage = storage
                if case let .array(slice) = storage { self.sliceIterator = slice.makeIterator() }
            }
            mutating func next() -> Element? {
                switch storage {
                case let .single(e): if index == 0 { index += 1; return e }; return nil
                case .array: return sliceIterator?.next()
                case .empty: return nil
                }
            }
        }
        func makeIterator() -> Iterator { Iterator(storage: storage) }
    }

    private let elements: ContiguousArray<Element>
    let ranges: [String: Range<Int>]

    init(elements: [Element], ranges: [String: Range<Int>]) {
        self.elements = ContiguousArray(elements)
        self.ranges = ranges
    }

    func children(of slot: String) -> Slot {
        guard let range = ranges[slot] else { return Slot(storage: .empty) }
        let slice = Slice(base: elements, bounds: range)
        if range.count == 1 { return Slot(storage: .single(slice[slice.startIndex])) }
        return Slot(storage: .array(slice))
    }

    func forEach(_ body: (Element) throws -> Void) rethrows { try elements.forEach(body) }
    var count: Int { elements.count }
    typealias Iterator = ContiguousArray<Element>.Iterator
    func makeIterator() -> Iterator { elements.makeIterator() }
}

// MARK: - Вариант A: как в OKBDUI

protocol IWidgetState: AnyObject {}
protocol ContentDrivenVisibility { var hasRenderableContent: Bool { get } }

protocol ElementCanvas: AnyObject {
    var layout: WidgetLayout { get }
    var visible: Bool { get }
    var frameW: Double { get set }
    var frameH: Double { get set }
    var frameX: Double { get set }
    var frameY: Double { get set }
    func sizeThatFits(_ available: Double) -> (Double, Double)
}

protocol ILayoutStrategy {
    func layoutChilds<T: ElementCanvas>(
        state: any IWidgetState,
        available: Double,
        childSlots: ChildSlots<T>
    ) -> (Double, Double)

    func placeChilds<T: ElementCanvas>(
        state: any IWidgetState,
        node: T,
        childSlots: ChildSlots<T>
    )
}

final class ContainerState: IWidgetState, ContentDrivenVisibility {
    let props: WidgetLayout
    var hasRenderableContent: Bool { true }
    init(props: WidgetLayout) { self.props = props }
}

struct ContainerLayoutStrategy: ILayoutStrategy {

    func layoutChilds<T: ElementCanvas>(
        state: any IWidgetState,
        available: Double,
        childSlots: ChildSlots<T>
    ) -> (Double, Double) {
        #if !NO_CAST
        guard let containerState = state as? ContainerState else { return (0, 0) }
        _ = containerState
        #endif
        #if NO_DICT
        let childElements = childSlots
        #else
        let childElements = childSlots.children(of: "items")
        #endif

        var cw = 0.0, ch = 0.0
        for child in childElements {
            let (w, h) = child.sizeThatFits(available)
            if child.visible {
                let m = child.layout.margins
                cw = max(cw, w + m.l + m.r)
                ch += h + m.t + m.b
            }
        }
        return (cw, ch)
    }

    func placeChilds<T: ElementCanvas>(
        state: any IWidgetState,
        node: T,
        childSlots: ChildSlots<T>
    ) {
        #if !NO_CAST
        guard let containerState = state as? ContainerState else { return }
        _ = containerState
        #endif
        #if NO_DICT
        let childElements = childSlots
        #else
        let childElements = childSlots.children(of: "items")
        #endif

        let p = node.layout.paddings
        let ox = node.frameX + p.l
        var oy = node.frameY + p.t
        let inner = node.frameW - p.l - p.r
        for child in childElements {
            let m = child.layout.margins
            let al = child.layout.alignment
            let free = inner - child.frameW - m.l - m.r
            child.frameX = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
            child.frameY = oy + m.t
            oy += child.frameH + m.t + m.b
        }
    }
}

final class WidgetStateRefA: ElementCanvas {
    let layout: WidgetLayout
    #if NO_EXIST
    let layoutStrategy = ContainerLayoutStrategy()
    #else
    let layoutStrategy: any ILayoutStrategy
    #endif
    let associatedState: any IWidgetState
    var childSlots: ChildSlots<WidgetStateRefA>
    weak var parent: WidgetStateRefA?

    var frameX = 0.0, frameY = 0.0, frameW = 0.0, frameH = 0.0

    var visible: Bool {
        #if NO_VIS_CAST
        return true
        #else
        if let c = associatedState as? ContentDrivenVisibility { return c.hasRenderableContent }
        return true
        #endif
    }

    init(layout: WidgetLayout, children: [WidgetStateRefA]) {
        self.layout = layout
        #if !NO_EXIST
        self.layoutStrategy = ContainerLayoutStrategy()
        #endif
        self.associatedState = ContainerState(props: layout)
        self.childSlots = ChildSlots(
            elements: children,
            ranges: children.isEmpty ? [:] : ["items": 0..<children.count]
        )
        for c in children { c.parent = self }
    }

    func sizeThatFits(_ available: Double) -> (Double, Double) {
        let (cw, ch) = layoutStrategy.layoutChilds(
            state: associatedState, available: available, childSlots: childSlots
        )
        let p = layout.paddings
        frameW = resolve(layout.width, content: cw + p.l + p.r, available: available)
        frameH = resolve(layout.height, content: ch + p.t + p.b, available: available)
        return (frameW, frameH)
    }

    func place() {
        layoutStrategy.placeChilds(state: associatedState, node: self, childSlots: childSlots)
        childSlots.forEach { $0.place() }
    }
}

// MARK: - Вариант B: классы и рекурсия, но без стирания типов

final class WidgetStateRefB {
    let layout: WidgetLayout
    var children: ContiguousArray<WidgetStateRefB>
    weak var parent: WidgetStateRefB?
    var frameX = 0.0, frameY = 0.0, frameW = 0.0, frameH = 0.0
    let visible = true

    init(layout: WidgetLayout, children: [WidgetStateRefB]) {
        self.layout = layout
        self.children = ContiguousArray(children)
        for c in children { c.parent = self }
    }

    func sizeThatFits(_ available: Double) -> (Double, Double) {
        var cw = 0.0, ch = 0.0
        for child in children {
            let (w, h) = child.sizeThatFits(available)
            if child.visible {
                let m = child.layout.margins
                cw = max(cw, w + m.l + m.r)
                ch += h + m.t + m.b
            }
        }
        let p = layout.paddings
        frameW = resolve(layout.width, content: cw + p.l + p.r, available: available)
        frameH = resolve(layout.height, content: ch + p.t + p.b, available: available)
        return (frameW, frameH)
    }

    func place() {
        let p = layout.paddings
        let ox = frameX + p.l
        var oy = frameY + p.t
        let inner = frameW - p.l - p.r
        for child in children {
            let m = child.layout.margins
            let al = child.layout.alignment
            let free = inner - child.frameW - m.l - m.r
            child.frameX = ox + m.l + (al == 1 ? free * 0.5 : (al == 2 ? free : 0))
            child.frameY = oy + m.t
            oy += child.frameH + m.t + m.b
        }
        for child in children { child.place() }
    }
}

// MARK: - Вариант C: плоский AoS, упакованные поля, циклы

struct SizeSpecP { var kind: UInt8 = 1; var value: Float = 0 }
struct InsetsP { var l: Float = 0, t: Float = 0, r: Float = 0, b: Float = 0 }

struct LayoutPacked {
    var width = SizeSpecP()
    var height = SizeSpecP()
    var margins = InsetsP()
    var paddings = InsetsP()
    var alignment: UInt8 = 0
    var visible: UInt8 = 1
}

struct FramePacked { var x: Float = 0, y: Float = 0, w: Float = 0, h: Float = 0 }

struct NodeC {
    var childStart: Int32 = 0
    var childCount: Int32 = 0
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

func layoutFlat(_ nodes: inout [NodeC], root: Float) {
    let n = nodes.count
    nodes.withUnsafeMutableBufferPointer { b in
        var i = n - 1
        while i >= 0 {
            let cs = Int(b[i].childStart), cc = Int(b[i].childCount)
            var cw: Float = 0, ch: Float = 0
            var j = cs
            while j < cs + cc {
                if b[j].layout.visible != 0 {
                    let f = b[j].frame, m = b[j].layout.margins
                    cw = max(cw, f.w + m.l + m.r); ch += f.h + m.t + m.b
                }
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
}

// MARK: - Построение одного и того же дерева в трёх видах

struct RNG {
    var s: UInt64
    mutating func next() -> UInt64 { s = s &* 6364136223846793005 &+ 1442695040888963407; return s >> 33 }
    mutating func int(_ n: Int) -> Int { Int(next() % UInt64(n)) }
}

struct Shape {
    var parent: [Int32] = [-1]
    var childStart: [Int32] = [0]
    var childCount: [Int32] = [0]
    var depth: [Int32] = [0]
    var layouts: [WidgetLayout] = []
}

func makeShape(_ nodeCount: Int, seed: UInt64) -> Shape {
    var rng = RNG(s: seed)
    var sh = Shape()
    var head = 0
    while sh.parent.count < nodeCount && head < sh.parent.count {
        let d = sh.depth[head]
        let k = d >= 8 ? 0 : max(0, min(1 + rng.int(4), nodeCount - sh.parent.count))
        if k > 0 {
            sh.childStart[head] = Int32(sh.parent.count)
            sh.childCount[head] = Int32(k)
            for _ in 0..<k {
                sh.parent.append(Int32(head)); sh.childStart.append(0)
                sh.childCount.append(0); sh.depth.append(d + 1)
            }
        }
        head += 1
    }
    let n = sh.parent.count
    sh.layouts.reserveCapacity(n)
    for _ in 0..<n {
        let kw = rng.int(4), kh = rng.int(4)
        let vw = Double(rng.int(300)), vh = Double(rng.int(200))
        let m = Double(rng.int(16)), p = Double(rng.int(12))
        func spec(_ k: Int, _ v: Double) -> SizeType {
            switch k {
            case 0: return .fixed(v)
            case 1: return .wrapContent(compressionPriority: 0)
            case 2: return .parent
            default: return .floating
            }
        }
        sh.layouts.append(WidgetLayout(
            width: spec(kw, vw), height: spec(kh, vh),
            paddings: Paddings(l: p, t: p, r: p, b: p),
            margins: Margins(l: m, t: m, r: m, b: m),
            alignment: rng.int(3)))
    }
    return sh
}

func buildA(_ sh: Shape) -> WidgetStateRefA {
    var built = [WidgetStateRefA?](repeating: nil, count: sh.parent.count)
    for i in stride(from: sh.parent.count - 1, through: 0, by: -1) {
        let cs = Int(sh.childStart[i]), cc = Int(sh.childCount[i])
        let kids = (cs..<(cs + cc)).map { built[$0]! }
        built[i] = WidgetStateRefA(layout: sh.layouts[i], children: kids)
    }
    return built[0]!
}

func buildB(_ sh: Shape) -> WidgetStateRefB {
    var built = [WidgetStateRefB?](repeating: nil, count: sh.parent.count)
    for i in stride(from: sh.parent.count - 1, through: 0, by: -1) {
        let cs = Int(sh.childStart[i]), cc = Int(sh.childCount[i])
        let kids = (cs..<(cs + cc)).map { built[$0]! }
        built[i] = WidgetStateRefB(layout: sh.layouts[i], children: kids)
    }
    return built[0]!
}

func buildC(_ sh: Shape) -> [NodeC] {
    var nodes = [NodeC](repeating: NodeC(), count: sh.parent.count)
    for i in 0..<sh.parent.count {
        nodes[i].childStart = sh.childStart[i]
        nodes[i].childCount = sh.childCount[i]
        let l = sh.layouts[i]
        func spec(_ s: SizeType) -> SizeSpecP {
            switch s {
            case .fixed(let v): return SizeSpecP(kind: 0, value: Float(v))
            case .wrapContent: return SizeSpecP(kind: 1, value: 0)
            case .parent: return SizeSpecP(kind: 2, value: 0)
            case .floating: return SizeSpecP(kind: 3, value: 0)
            }
        }
        nodes[i].layout = LayoutPacked(
            width: spec(l.width), height: spec(l.height),
            margins: InsetsP(l: Float(l.margins.l), t: Float(l.margins.t),
                             r: Float(l.margins.r), b: Float(l.margins.b)),
            paddings: InsetsP(l: Float(l.paddings.l), t: Float(l.paddings.t),
                              r: Float(l.paddings.r), b: Float(l.paddings.b)),
            alignment: UInt8(l.alignment), visible: 1)
    }
    return nodes
}

// MARK: - Сверка и прогон

func checksumA(_ root: WidgetStateRefA) -> Double {
    var sum = 0.0
    var stack: [WidgetStateRefA] = [root]
    while let v = stack.popLast() {
        sum += v.frameX + v.frameY + v.frameW + v.frameH
        v.childSlots.forEach { stack.append($0) }
    }
    return sum
}

func checksumB(_ root: WidgetStateRefB) -> Double {
    var sum = 0.0
    var stack: [WidgetStateRefB] = [root]
    while let v = stack.popLast() {
        sum += v.frameX + v.frameY + v.frameW + v.frameH
        for c in v.children { stack.append(c) }
    }
    return sum
}

func bench(_ name: String, iters: Int, _ body: () -> Void) -> Double {
    var best = Double.infinity
    for _ in 0..<9 {
        let t0 = DispatchTime.now().uptimeNanoseconds
        for _ in 0..<iters { body() }
        best = min(best, Double(DispatchTime.now().uptimeNanoseconds - t0) / Double(iters))
    }
    print(String(format: "  %-34@ %10.1f нс", name as NSString, best))
    return best
}

for n in [1200] {
    let sh = makeShape(n, seed: 42)
    let real = sh.parent.count
    let rootA = buildA(sh)
    let rootB = buildB(sh)
    var nodesC = buildC(sh)

    // одна раскладка каждым способом — сверить результат
    _ = rootA.sizeThatFits(390); rootA.place()
    _ = rootB.sizeThatFits(390); rootB.place()
    layoutFlat(&nodesC, root: 390)

    let ca = checksumA(rootA), cb = checksumB(rootB)
    var cc = 0.0
    for x in nodesC { cc += Double(x.frame.x + x.frame.y + x.frame.w + x.frame.h) }

    print("\n══════ узлов \(real) ══════")
    print(String(format: "  контрольные суммы: A %.0f   B %.0f   C %.0f%@",
                 ca, cb, cc, (abs(ca - cb) < 1 && abs(ca - cc) < max(1, abs(ca) * 1e-4)) ? "  ✓" : "  ✗ РАСХОЖДЕНИЕ"))

    let iters = max(20, 2_000_000 / real)
    let ta = bench("A: OKBDUI (классы+экзистенциалы)", iters: iters) {
        _ = rootA.sizeThatFits(390); rootA.place()
    }
    _ = ta
}
