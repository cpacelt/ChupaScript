#!/usr/bin/env python3
"""Генератор синтетики диспетчеризации: таблица указателей против switch.

Пишет dispatch.swift рядом с собой. Сборка и запуск:

    python3 dispatch-gen.py && swiftc -O -wmo dispatch.swift -o dispatch && ./dispatch

Генерируется, а не пишется руками, потому что нужен switch на 256 ветвей: в
этом и смысл замера — сравнить огромное тело функции с крошечным телом цикла,
вызывающим отдельные функции. Обработчики намеренно различаются константами,
иначе оптимизатор слил бы их в один.
"""

N = 256

handlers = "\n".join(f"""@inline(never)
func h{i}(_ s: inout St, _ op: UInt8) {{
    s.a = s.a &+ UInt64(op) &* {i + 1}
    s.b = s.b ^ (s.a >> {(i % 13) + 1})
    s.c = s.c &+ UInt64({i % 7 + 1})
}}""" for i in range(N))

table = ("let table: [(inout St, UInt8) -> Void] = [\n"
         + ",\n".join(f"    h{i}" for i in range(N)) + "\n]")

switch_call = ("@inline(never)\nfunc dispatchSwitchCall(_ s: inout St, _ op: UInt8) {\n    switch op {\n"
               + "".join(f"    case {i}: h{i}(&s, op)\n" for i in range(N))
               + "    default: break\n    }\n}")

switch_inline = ("@inline(never)\nfunc dispatchSwitchInline(_ s: inout St, _ op: UInt8) {\n    switch op {\n"
                 + "".join(f"    case {i}:\n"
                           f"        s.a = s.a &+ UInt64(op) &* {i + 1}\n"
                           f"        s.b = s.b ^ (s.a >> {(i % 13) + 1})\n"
                           f"        s.c = s.c &+ UInt64({i % 7 + 1})\n" for i in range(N))
                 + "    default: break\n    }\n}")

SRC = f"""// Диспетчеризация: таблица указателей против switch. Сгенерировано dispatch-gen.py.
import Foundation

struct St {{ var a: UInt64 = 1, b: UInt64 = 2, c: UInt64 = 3 }}

{handlers}

{table}

// Тот же набор обработчиков, но в словаре: ключ — опкод.
let dict: [UInt8: (inout St, UInt8) -> Void] = {{
    var d = [UInt8: (inout St, UInt8) -> Void](minimumCapacity: {N})
    for i in 0..<{N} {{ d[UInt8(i)] = table[i] }}
    return d
}}()

{switch_call}

{switch_inline}

@inline(never)
func runTable(_ ops: [UInt8], _ s: inout St) {{
    table.withUnsafeBufferPointer {{ t in
        for op in ops {{ t[Int(op)](&s, op) }}
    }}
}}

@inline(never)
func runDict(_ ops: [UInt8], _ s: inout St) {{
    for op in ops {{ dict[op]!(&s, op) }}
}}

@inline(never)
func runSwitchCall(_ ops: [UInt8], _ s: inout St) {{
    for op in ops {{ dispatchSwitchCall(&s, op) }}
}}

@inline(never)
func runSwitchInline(_ ops: [UInt8], _ s: inout St) {{
    for op in ops {{ dispatchSwitchInline(&s, op) }}
}}

// Два потока опкодов: случайный (плохо предсказуемый, как байткод шрифта) и
// короткий цикл из 16 (предсказуемый, как небольшой набор видов виджетов).
func stream(_ n: Int, random: Bool) -> [UInt8] {{
    var seed: UInt64 = 12345
    var out = [UInt8](); out.reserveCapacity(n)
    for i in 0..<n {{
        if random {{
            seed = seed &* 6364136223846793005 &+ 1442695040888963407
            out.append(UInt8(truncatingIfNeeded: seed >> 33))
        }} else {{
            out.append(UInt8((i % 16) &* 7 % 256))
        }}
    }}
    return out
}}

func bench(_ name: String, _ ops: [UInt8], _ body: ([UInt8], inout St) -> Void) {{
    var best = Double.infinity
    var st = St()
    for _ in 0..<9 {{
        let t0 = DispatchTime.now().uptimeNanoseconds
        body(ops, &st)
        best = min(best, Double(DispatchTime.now().uptimeNanoseconds - t0) / Double(ops.count))
    }}
    print(String(format: "  %-16@ %6.3f нс/опкод   (ст %llu)", name as NSString, best, st.a &+ st.b &+ st.c))
}}

let n = 4_000_000
for random in [true, false] {{
    print(random ? "\\n=== случайный поток опкодов ===" : "\\n=== предсказуемый поток (цикл из 16) ===")
    let ops = stream(n, random: random)
    bench("таблица", ops, runTable)
    bench("switch+вызов", ops, runSwitchCall)
    bench("switch inline", ops, runSwitchInline)
    bench("Dictionary", ops, runDict)
}}
"""

import os
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dispatch.swift")
with open(path, "w") as f:
    f.write(SRC)
print(f"написано {path}, {len(SRC)} байт")
