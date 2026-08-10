#!/usr/bin/env python3
"""Проверка инвариантов спецификации ChupaLang.

Запуск: python3 tools/check-spec.py
Выход 0 — нарушений нет; 1 — нарушения перечислены на stderr.
"""
import json
import re
import sys
from pathlib import Path

SPEC = Path(__file__).resolve().parent.parent / "docs" / "spec"
ROOT = SPEC.parent.parent

# Конструкции, вырезанные из языка. Пара «путь от docs/spec, подстрока»:
# подстрока не должна в этом файле встречаться. Пополняется задачами 2—5.
REMOVED: list[tuple[str, str]] = [
    ("grammar/syntax.txt", "UpdateExpression"),
    ("grammar/syntax.txt", "ExponentiationExpression"),
    ("grammar/syntax.txt", "++"),
    ("grammar/syntax.txt", "**"),
    ("grammar/lexical.txt", "**"),
    ("C-divergences.md", "o.k++"),
    ("builtins.json", "**"),
    ("grammar/syntax.txt", "&&="),
    ("grammar/lexical.txt", "&&="),
    ("03-lexical.md", "??="),
    ("C-divergences.md", "??="),
    ("grammar/syntax.txt", "PropExpression"),
    ("grammar/syntax.txt", "Program"),
    ("01-scope.md", "PropExpression"),
    ("05-names.md", "PropExpression"),
]

# Файлы, названные в перечне разделов, но ещё не созданные. Ссылка на такой
# файл нарушением не считается. Запись, для которой файл уже существует, —
# нарушение: список обязан очищаться.
PLANNED: set[str] = set()

# Идентификаторы, вынесенные из спецификации в docs/impl/. Номер за
# утверждением закреплён: в docs/spec/ такой идентификатор не определяется и
# не упоминается.
RETIRED: set[str] = {
    "CS-EE-0016",
    "CS-EE-0017",
    "CS-EE-0018",
    "CS-CONF-0002",
    "CS-CONF-0003",
    "CS-CONF-0004",
    "CS-CONF-0005",
    "CS-CONF-0006",
    "CS-CONF-0007",
    "CS-CONF-0008",
}


def sources():
    for p in sorted(SPEC.rglob("*")):
        if p.is_file() and p.suffix in {".md", ".txt", ".json"}:
            yield p


def check_json(errors):
    for p in sorted(SPEC.rglob("*.json")):
        try:
            json.loads(p.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            errors.append(f"{p}: JSON не разбирается: {e}")


def check_planned(errors):
    for name in sorted(PLANNED):
        if (SPEC / name).exists():
            errors.append(f"{name}: файл создан, запись из PLANNED пора убрать")


def check_file_refs(errors):
    pat = re.compile(r"`([\w./-]+\.(?:md|json|txt))`")
    for p in sources():
        for name in sorted(set(pat.findall(p.read_text(encoding="utf-8")))):
            if name in PLANNED:
                continue
            if (SPEC / name).exists() or (SPEC / "grammar" / name).exists():
                continue
            if name.startswith("docs/") and (ROOT / name).exists():
                continue
            errors.append(f"{p}: ссылка на несуществующий файл {name}")


def _flush_table(p, block, errors):
    if len(block) < 2:
        return
    widths = {len(re.split(r"(?<!\\)\|", line.strip())) for _, line in block}
    if len(widths) > 1:
        errors.append(
            f"{p}:{block[0][0]}: в таблице разное число ячеек: {sorted(widths)}"
        )


def check_tables(errors):
    for p in sorted(SPEC.rglob("*.md")):
        block = []
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith("|"):
                block.append((n, line))
            else:
                _flush_table(p, block, errors)
                block = []
        _flush_table(p, block, errors)


def check_ids(errors):
    use = re.compile(r"\bCS-[A-Z]+-\d{4}\b")
    dfn_prose = re.compile(r"^\*\*(CS-[A-Z]+-\d{4})\.?\*\*", re.M)
    dfn_row = re.compile(r"^\|\s*(CS-[A-Z]+-\d{4})\s*\|", re.M)
    dfn_json = re.compile(r'"id":\s*"(CS-[A-Z]+-\d{4})"')

    defined, used = set(), {}
    for p in sources():
        text = p.read_text(encoding="utf-8")
        defined |= set(dfn_prose.findall(text))
        defined |= set(dfn_row.findall(text))
        defined |= set(dfn_json.findall(text))
        for i in use.findall(text):
            used.setdefault(i, p)
    for i in sorted(used):
        if i not in defined:
            errors.append(f"{used[i]}: ссылка на неопределённый идентификатор {i}")


SECTIONS = set(range(0, 12))


def check_section_refs(errors):
    pat = re.compile(r"\bраздел[а-яё]*\s+(\d+)")
    for p in sources():
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            for num in pat.findall(line):
                if int(num) not in SECTIONS:
                    errors.append(f"{p}:{n}: ссылка на несуществующий раздел {num}")


def check_removed(errors):
    for rel, needle in REMOVED:
        p = SPEC / rel
        if not p.exists():
            errors.append(f"{rel}: файл не найден, проверка вырезанного невозможна")
            continue
        if needle in p.read_text(encoding="utf-8"):
            errors.append(f"{p}: осталось упоминание вырезанного: {needle}")


def check_retired(errors):
    pat = re.compile(r"\bCS-[A-Z]+-\d{4}\b")
    for p in sources():
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            for m in pat.findall(line):
                if m in RETIRED:
                    errors.append(f"{p}:{n}: упоминание вынесенного идентификатора {m}")


CHECKS = (
    check_json,
    check_planned,
    check_file_refs,
    check_tables,
    check_ids,
    check_section_refs,
    check_removed,
    check_retired,
)


def main():
    errors: list[str] = []
    for check in CHECKS:
        check(errors)
    if errors:
        for e in errors:
            print(e, file=sys.stderr)
        print(f"\nнарушений: {len(errors)}", file=sys.stderr)
        return 1
    print("нарушений нет")
    return 0


if __name__ == "__main__":
    sys.exit(main())
