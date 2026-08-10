# Переборка спецификации ChupaLang: фаза 0 (сокращения языка)

> **Исполнено 2026-08-08**, коммиты `930c68d..09f3df8`. Фаза 1 из этого плана
> отменена и переписана заново — см. `2026-08-08-spec-restructure-phase-1.md`:
> решение вынести реализацию из спецификации изменило структуру разделов.

<!-- прежний заголовок: фазы 0 и 1 -->

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Применить пять решённых сокращений языка и переложить спецификацию в новую нумерацию разделов, не меняя содержания, — так, чтобы фаза 2 начиналась с документа, который нигде себе не противоречит.

**Architecture:** Работа механическая и проверяемая. Первой задачей ставится `tools/check-spec.py` — набор инвариантов спецификации (JSON разбирается, ссылки на файлы разрешаются, таблицы не разъехались, всякий упомянутый идентификатор определён, вырезанное не осталось). Дальше каждая задача сначала добавляет в проверяльщик условие, которое обязано упасть, потом правит документы, потом убеждается, что упавшее поднялось. Фаза 2 — написание разделов о выражениях и о Statement — этим планом не покрыта.

**Tech Stack:** Markdown, Python 3 (только стандартная библиотека), git. Сборки нет, зависимостей нет.

## Global Constraints

Требования действуют для всякой задачи и в тексте задач не повторяются.

- Ветка `spec/restructure`. На `main` не коммитить.
- Основание решений — `docs/superpowers/specs/2026-08-08-spec-restructure-design.md`. При расхождении плана с ним прав документ решения.
- Всякое сокращение языка — **сужение**: единица исполнения ChupaLang остаётся корректным текстом JavaScript (`CS-CONF-0001`). Ни одно сокращение не меняет значения принимаемого текста.
- Идентификаторы `CS-*-nnnn` не переиспользуются и не меняются. Изъятое правило помечается изъятым, номер за ним остаётся.
- Внутри обратных кавычек — только английский: имена продукций, средств, параметров, ключевых слов. Проза — русская: «получатель», а не «receiver».
- Нормативно тело. Блоки `> Пояснение` и примеры кода ненормативны.
- Фаза 1 содержания не меняет. Единственные допустимые изменения смысла в фазе 1 — те, что прямо названы в задачах 6 и 10.
- После каждой задачи `python3 tools/check-spec.py` печатает `нарушений нет` и возвращает 0.
- Файлы спецификации — в `docs/spec/`. Пути в задачах даны от корня репозитория.

---

## Файловая структура

**Создаётся:**

| Файл | За что отвечает |
|---|---|
| `tools/check-spec.py` | инварианты спецификации, единственный исполняемый артефакт плана |
| `docs/spec/02-conformance.md` | требования к реализации (`CS-CONF-`), выделены из `01-scope.md` §1.4 |
| `docs/spec/03-notation.md` | нотация, конвенции, термины, префиксы идентификаторов |
| `docs/spec/07-expressions.md` | заглушка: выражения |
| `docs/spec/08-statements.md` | заглушка: `Statement` и объявления |
| `docs/spec/09-functions.md` | заглушка: функции |
| `docs/spec/11-errors.md` | заглушка: ошибки и аварийное завершение |
| `docs/spec/12-tests.md` | заглушка: устройство корпуса |
| `docs/spec/A-grammar.md` | заглушка: сводка грамматики |

**Переименовывается:**

| Было | Стало |
|---|---|
| `docs/spec/04-types.md` | `docs/spec/05-values.md` |
| `docs/spec/07-builtins.md` | `docs/spec/10-library.md` |
| `docs/spec/grammar/notes.md` | `docs/spec/04-lexical.md` |
| `docs/spec/grammar/narrowings.md` | `docs/spec/B-narrowings.md` |
| `docs/spec/B-divergences.md` | `docs/spec/C-divergences.md` |
| `docs/spec/C-inherited.md` | `docs/spec/D-inherited.md` |
| `docs/spec/embedding.md` | `docs/spec/E-embedding.md` |

**Остаётся на месте:** `docs/spec/README.md`, `docs/spec/06-names.md`, `docs/spec/rules.json`, `docs/spec/builtins.json`, `docs/spec/grammar/lexical.txt`, `docs/spec/grammar/syntax.txt`.

Грамматика в фазе 1 не разбирается по разделам: это работа фазы 2. `rules.json` и `builtins.json` удаляются в фазе 2, когда их содержимое разойдётся по разделам.

---

## Соответствие ссылок на разделы

Таблица применяется в задаче 10. До неё ссылки чинятся только в тех файлах, которых задача касается.

| Было | Стало |
|---|---|
| раздел 2 (лексика) | раздел 4 |
| раздел 3 (грамматика) | `grammar/syntax.txt` — временно, до фазы 2 |
| раздел 4 (типы) | раздел 5 |
| раздел 5 (тесты) | раздел 12 |
| раздел 6 (правила) | раздел 6 |
| раздел 7 (библиотека) | раздел 10 |
| раздел 8 (ошибки) | раздел 11 |
| раздел 9 (соответствие) | раздел 2 |
| раздел 1.4 | раздел 2 |
| приложение A | приложение B |
| приложение B | приложение C |
| приложение C | приложение D |

---

# Фаза 0. Сокращения языка

### Task 1: Инструмент проверки

**Files:**
- Create: `tools/check-spec.py`

**Interfaces:**
- Produces: исполняемый `python3 tools/check-spec.py`, код возврата 0 при чистоте и 1 при нарушениях; списки `REMOVED` и `PLANNED` в его начале, которые пополняют последующие задачи.

- [ ] **Step 1: Написать проверяльщик**

Создать `tools/check-spec.py` со следующим содержимым:

```python
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

# Конструкции, вырезанные из языка. Пара «путь от docs/spec, подстрока»:
# подстрока не должна в этом файле встречаться. Пополняется задачами 2—5.
REMOVED: list[tuple[str, str]] = []

# Файлы, названные в перечне разделов, но ещё не созданные. Ссылка на такой
# файл нарушением не считается. Запись, для которой файл уже существует, —
# нарушение: список обязан очищаться.
PLANNED: set[str] = {"08-errors.md", "09-conformance.md"}


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


def check_removed(errors):
    for rel, needle in REMOVED:
        p = SPEC / rel
        if not p.exists():
            errors.append(f"{rel}: файл не найден, проверка вырезанного невозможна")
            continue
        if needle in p.read_text(encoding="utf-8"):
            errors.append(f"{p}: осталось упоминание вырезанного: {needle}")


CHECKS = (
    check_json,
    check_planned,
    check_file_refs,
    check_tables,
    check_ids,
    check_removed,
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
```

- [ ] **Step 2: Убедиться, что на нынешней спецификации чисто**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`, код возврата 0.

Если печатает нарушения — не править документы под проверяльщик, а разобраться: либо в спецификации настоящий дефект, либо проверка написана неверно.

- [ ] **Step 3: Убедиться, что проверяльщик вообще что-то ловит**

Подсадить дефект и увидеть его:

```bash
printf '\nСсылка на `CS-ERR-0004` и на `99-nowhere.md`.\n' >> docs/spec/04-types.md
python3 tools/check-spec.py; echo "код возврата: $?"
```

Ожидается: две строки — про неопределённый идентификатор `CS-ERR-0004` и про несуществующий файл `99-nowhere.md`; код возврата 1.

- [ ] **Step 4: Убрать подсаженный дефект**

```bash
git checkout docs/spec/04-types.md
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 5: Коммит**

```bash
git add tools/check-spec.py
git commit -m "chore: проверяльщик инвариантов спецификации"
```

---

### Task 2: Убрать `++`, `--` и `**`

Три оператора вырезаются одной задачей: `ExponentiationExpression` имеет левым операндом `UpdateExpression`, поэтому по отдельности продукции не разнимаются.

**Files:**
- Modify: `docs/spec/grammar/syntax.txt` — разделы 2.7, 2.8, 5
- Modify: `docs/spec/grammar/lexical.txt:204-209` — `OtherPunctuator`
- Modify: `docs/spec/grammar/notes.md:34-38` — пример к `CS-LEX-0003`
- Modify: `docs/spec/B-divergences.md` — таблица составного присваивания
- Modify: `docs/spec/grammar/narrowings.md` — разделы 1 и 3
- Modify: `tools/check-spec.py` — список `REMOVED`

**Interfaces:**
- Consumes: `tools/check-spec.py` из задачи 1.
- Produces: `REMOVED` пополнен четырьмя парами.

- [ ] **Step 1: Добавить условия в проверяльщик**

В `tools/check-spec.py` заменить

```python
REMOVED: list[tuple[str, str]] = []
```

на

```python
REMOVED: list[tuple[str, str]] = [
    ("grammar/syntax.txt", "UpdateExpression"),
    ("grammar/syntax.txt", "ExponentiationExpression"),
    ("grammar/syntax.txt", "++"),
    ("grammar/syntax.txt", "**"),
    ("grammar/lexical.txt", "++"),
    ("grammar/lexical.txt", "**"),
    ("B-divergences.md", "o.k++"),
]
```

- [ ] **Step 2: Убедиться, что проверка падает**

```bash
python3 tools/check-spec.py; echo "код возврата: $?"
```

Ожидается: семь строк вида `осталось упоминание вырезанного`, код возврата 1.

- [ ] **Step 3: Переписать разделы 2.7 и 2.8 синтаксической грамматики**

В `docs/spec/grammar/syntax.txt` заменить блок от `# --- 2.7.` до продукции `MultiplicativeExpression` включительно на:

```
# --- 2.7. Унарные операторы -------------------------------------------------

UnaryExpression :
    LeftHandSideExpression
    + UnaryExpression
    - UnaryExpression
    ! UnaryExpression
    typeof UnaryExpression

# delete, void и побитовое ~ отсутствуют.
#
# Операторов инкремента и декремента нет ни в префиксной, ни в постфиксной
# форме. Увеличение записывается присваиванием: `i = i + 1`. Вместе с ними
# исчезает продукция UpdateExpression: она существовала только ради них.


# --- 2.8. Бинарные операторы ------------------------------------------------

MultiplicativeExpression :
    UnaryExpression
    MultiplicativeExpression MultiplicativeOperator UnaryExpression

MultiplicativeOperator : one of
    * / %

# Оператор ** отсутствует; возведение в степень выражается Math.pow (раздел 7).
# Вместе с ним отпало ограничение, по которому `-a ** b` в JavaScript — ошибка
# разбора: без оператора запрещать нечего.
```

Продукции `AdditiveExpression`, `RelationalExpression`, `EqualityExpression` и всё ниже не трогать.

- [ ] **Step 4: Убрать пунктуаторы**

В `docs/spec/grammar/lexical.txt` заменить

```
OtherPunctuator :: one of
    {    (    )    [    ]    .    ;    ,    :    ?    =>
    <    >    <=   >=   ==   !=   ===  !==
    +    -    *    /    %    **
    ++   --   !    &&   ||   ??
    =    +=   -=   *=   /=   %=   &&=  ||=  ??=
```

на

```
OtherPunctuator :: one of
    {    (    )    [    ]    .    ;    ,    :    ?    =>
    <    >    <=   >=   ==   !=   ===  !==
    +    -    *    /    %    !    &&   ||   ??
    =    +=   -=   *=   /=   %=   &&=  ||=  ??=
```

- [ ] **Step 5: Поправить таблицу ограничений на перевод строки**

В `docs/spec/grammar/syntax.txt`, раздел 5, заменить «в этих трёх местах» на «в этих двух местах» и убрать из таблицы две строки про `UpdateExpression`. Таблица должна остаться такой:

```
# | Продукция          | Между чем         | Что было бы иначе                    |
# |--------------------|-------------------|--------------------------------------|
# | ReturnStatement    | return и Expression | `return \n x;` в JavaScript —      |
# |                    |                   | `return; x;`, здесь стало бы         |
# |                    |                   | возвратом значения x                 |
# | ArrowFunction      | параметры и =>    | `x \n => x` JavaScript отвергает,    |
# |                    |                   | здесь было бы принято                |
```

- [ ] **Step 6: Поправить комментарий об отсутствующих присваиваниях**

В `docs/spec/grammar/syntax.txt`, раздел 2.10, заменить строку

```
# **= и составные побитовые отсутствуют.
```

на

```
# Составных присваиваний с возведением в степень и с побитовыми операторами
# нет, потому что нет самих операторов.
```

- [ ] **Step 7: Поправить пример к `CS-LEX-0003`**

В `docs/spec/grammar/notes.md` заменить

```
> Пояснение. Многострочный комментарий переводы строк не скрывает: в записи
> `a /* \n */ ++ b` перевод строки между `a` и `++` присутствует. На это правило
```

на

```
> Пояснение. Многострочный комментарий переводы строк не скрывает: в записи
> `return /* \n */ x;` перевод строки между `return` и `x` присутствует. На это
> правило
```

- [ ] **Step 8: Убрать обновление из приложения B**

В `docs/spec/B-divergences.md` заменить заголовок `#### Составное присваивание и обновление` на `#### Составное присваивание`, убрать из таблицы строки `++o.k` и `o.k++`, заменить абзац

```
Остальные составные операторы (`-=`, `*=`, `/=`, `%=`) преобразуются как `+=`,
`--` — как `++`.
```

на

```
Остальные составные операторы (`-=`, `*=`, `/=`, `%=`) преобразуются как `+=`.
```

и удалить целиком следующий за ним абзац, начинающийся с «Унарный `+` в двух последних строках выражает ToNumber».

Вводный абзац подраздела («Здесь свойство и читается, и записывается…») оставить без изменений: он верен и для оставшихся форм.

- [ ] **Step 9: Внести сужения в перечень**

В `docs/spec/grammar/narrowings.md`, раздел 1 «Лексика», заменить строку

```
| `**=` и побитовые операторы не входят в `Punctuator` | `**=`, `&`, `\|`, `^`, `~`, `<<`, `>>`, `>>>` и составные присваивания с ними |
```

на

```
| `**`, `**=` и побитовые операторы не входят в `Punctuator` | `**`, `**=`, `&`, `\|`, `^`, `~`, `<<`, `>>`, `>>>` и составные присваивания с ними |
| `++` и `--` не входят в `Punctuator` | инкремент и декремент в любой форме: `i++`, `++i`, `i--`, `--i` |
```

В разделе 3 «Выражения» добавить строку после строки про `UnaryExpression`:

```
| `UpdateExpression` отсутствует | `i++`, `++i`, `i--`, `--i`; увеличение записывается как `i = i + 1` |
| `ExponentiationExpression` отсутствует | `a ** b`; возведение в степень — `Math.pow(a, b)` |
```

- [ ] **Step 10: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

Если проверяльщик всё ещё находит `**` или `++`, найти оставшееся и решить по существу: либо это законное упоминание отсутствующего оператора в перечне сужений (тогда файл не должен стоять в `REMOVED`), либо недоделанная правка.

- [ ] **Step 11: Проверить, что грамматика осталась связной**

```bash
grep -n "UnaryExpression\|MultiplicativeExpression\|LeftHandSideExpression" docs/spec/grammar/syntax.txt
```

Ожидается: `UnaryExpression` определён и используется в `MultiplicativeExpression`; ни одна продукция не ссылается на `UpdateExpression` или `ExponentiationExpression`.

- [ ] **Step 12: Коммит**

```bash
git add docs/spec tools/check-spec.py
git commit -m "spec: убрать ++, -- и **"
```

---

### Task 3: Убрать `&&=`, `||=`, `??=`

**Files:**
- Modify: `docs/spec/grammar/syntax.txt` — `AssignmentExpression`
- Modify: `docs/spec/grammar/lexical.txt` — `OtherPunctuator`
- Modify: `docs/spec/grammar/notes.md` — пример к `CS-LEX-0002`
- Modify: `docs/spec/B-divergences.md` — таблица составного присваивания
- Modify: `docs/spec/grammar/narrowings.md` — разделы 1 и 3
- Modify: `tools/check-spec.py` — список `REMOVED`

**Interfaces:**
- Consumes: `REMOVED` из задачи 2.
- Produces: `REMOVED` пополнен четырьмя парами.

- [ ] **Step 1: Добавить условия в проверяльщик**

В `tools/check-spec.py` дописать в конец списка `REMOVED`:

```python
    ("grammar/syntax.txt", "&&="),
    ("grammar/lexical.txt", "&&="),
    ("grammar/notes.md", "??="),
    ("B-divergences.md", "??="),
```

- [ ] **Step 2: Убедиться, что проверка падает**

```bash
python3 tools/check-spec.py; echo "код возврата: $?"
```

Ожидается: четыре новых строки `осталось упоминание вырезанного`, код возврата 1.

- [ ] **Step 3: Убрать альтернативы из `AssignmentExpression`**

В `docs/spec/grammar/syntax.txt` заменить

```
AssignmentExpression :
    ConditionalExpression
    ArrowFunction
    LeftHandSideExpression = AssignmentExpression
    LeftHandSideExpression AssignmentOperator AssignmentExpression
    LeftHandSideExpression &&= AssignmentExpression
    LeftHandSideExpression ||= AssignmentExpression
    LeftHandSideExpression ??= AssignmentExpression
```

на

```
AssignmentExpression :
    ConditionalExpression
    ArrowFunction
    LeftHandSideExpression = AssignmentExpression
    LeftHandSideExpression AssignmentOperator AssignmentExpression
```

- [ ] **Step 4: Убрать пунктуаторы**

В `docs/spec/grammar/lexical.txt` заменить

```
    =    +=   -=   *=   /=   %=   &&=  ||=  ??=
```

на

```
    =    +=   -=   *=   /=   %=
```

- [ ] **Step 5: Поправить пример к `CS-LEX-0002`**

В `docs/spec/grammar/notes.md` заменить

```
> Пояснение. Без этого правила `??=` разбирался бы как `??` и `=`, `===` — как
```

на

```
> Пояснение. Без этого правила `+=` разбирался бы как `+` и `=`, `===` — как
```

- [ ] **Step 6: Убрать три строки из приложения B**

В `docs/spec/B-divergences.md` удалить из таблицы составного присваивания строки `o.k &&= v`, `o.k \|\|= v` и `o.k ??= v`.

- [ ] **Step 7: Внести сужение в перечень**

В `docs/spec/grammar/narrowings.md`, раздел 1 «Лексика», добавить строку:

```
| логические присваивания не входят в `Punctuator` | `&&=`, `\|\|=`, `??=` |
```

В разделе 3 «Выражения» добавить строку:

```
| `AssignmentExpression` не содержит логических присваиваний | `a &&= b`, `a \|\|= b`, `a ??= b`; записывается условным оператором или `if` |
```

- [ ] **Step 8: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 9: Убедиться, что `??`, `&&` и `||` уцелели**

```bash
grep -n "CoalesceExpression\|LogicalANDExpression\|LogicalORExpression" docs/spec/grammar/syntax.txt
grep -n "&&   ||   ??" docs/spec/grammar/lexical.txt
```

Ожидается: продукции на месте, пунктуаторы `&&`, `||`, `??` в `OtherPunctuator` присутствуют. Вырезаны только формы с `=`.

- [ ] **Step 10: Коммит**

```bash
git add docs/spec tools/check-spec.py
git commit -m "spec: убрать логические присваивания &&=, ||=, ??="
```

---

### Task 4: Убрать завершающие запятые

Семь продукций в `docs/spec/grammar/syntax.txt`: `ArrayLiteral`, `ObjectLiteral`, `Arguments`, `FormalParameters`, `ArrowParameters`, `ObjectPattern`, `ArrayPattern`.

**Files:**
- Modify: `docs/spec/grammar/syntax.txt` — семь продукций и комментарий к `ArrayLiteral`
- Modify: `docs/spec/grammar/narrowings.md` — разделы 3 и 4

**Interfaces:**
- Consumes: проверяльщик из задачи 1.
- Produces: ничего для последующих задач.

- [ ] **Step 1: Убрать альтернативу из `ArrayLiteral` и поправить комментарий**

В `docs/spec/grammar/syntax.txt` заменить

```
ArrayLiteral :
    [ ]
    [ ElementList ]
    [ ElementList , ]
```

на

```
ArrayLiteral :
    [ ]
    [ ElementList ]
```

и заменить комментарий

```
# Пропуски (`[1, , 3]`) не допускаются: ElementList требует выражение между
# запятыми. Завершающая запятая разрешена и элемента не добавляет.
```

на

```
# Пропуски (`[1, , 3]`) не допускаются: ElementList требует выражение между
# запятыми. Завершающая запятая не допускается.
```

- [ ] **Step 2: Убрать альтернативу из `ObjectLiteral`**

Заменить

```
ObjectLiteral :
    { }
    { PropertyDefinitionList }
    { PropertyDefinitionList , }
```

на

```
ObjectLiteral :
    { }
    { PropertyDefinitionList }
```

- [ ] **Step 3: Убрать альтернативу из `Arguments`**

Заменить

```
Arguments :
    ( )
    ( ArgumentList )
    ( ArgumentList , )
```

на

```
Arguments :
    ( )
    ( ArgumentList )
```

- [ ] **Step 4: Убрать альтернативу из `ObjectPattern` и `ArrayPattern`**

Заменить

```
ObjectPattern :
    { }
    { BindingPropertyList }
    { BindingPropertyList , }
```

на

```
ObjectPattern :
    { }
    { BindingPropertyList }
```

и заменить

```
ArrayPattern :
    [ ]
    [ BindingElementList ]
    [ BindingElementList , ]
```

на

```
ArrayPattern :
    [ ]
    [ BindingElementList ]
```

- [ ] **Step 5: Упростить `FormalParameters`**

`FormalParameters` существовала только ради завершающей запятой. Заменить

```
FormalParameters :
    FormalParameterList
    FormalParameterList ,

FormalParameterList :
    FormalParameter
    FormalParameterList , FormalParameter
```

на

```
FormalParameterList :
    FormalParameter
    FormalParameterList , FormalParameter
```

и в продукции `FunctionDeclaration` заменить `FormalParameters_opt` на `FormalParameterList_opt`:

```
FunctionDeclaration :
    function Identifier ( FormalParameterList_opt ) { FunctionBody }
```

- [ ] **Step 6: Убрать альтернативу из `ArrowParameters`**

Заменить

```
ArrowParameters :
    Identifier
    ( )
    ( ArrowParameterList )
    ( ArrowParameterList , )
```

на

```
ArrowParameters :
    Identifier
    ( )
    ( ArrowParameterList )
```

- [ ] **Step 7: Внести сужение в перечень**

В `docs/spec/grammar/narrowings.md`, раздел 3 «Выражения», добавить строку:

```
| завершающая запятая не допускается нигде | `[1, 2, ]`, `{ a: 1, }`, `f(x, )` |
```

В разделе 4 «Функции и образцы» добавить строку:

```
| завершающая запятая не допускается нигде | `function f(a, ) {}`, `(a, ) => a`, `let { a, } = o;`, `let [a, ] = xs;` |
```

- [ ] **Step 8: Убедиться, что `FormalParameters` больше не упоминается**

```bash
grep -rn "FormalParameters" docs/spec/
```

Ожидается: ни одного совпадения. Если находится — поправить оставшуюся ссылку на `FormalParameterList`.

- [ ] **Step 9: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 10: Коммит**

```bash
git add docs/spec
git commit -m "spec: убрать завершающие запятые"
```

---

### Task 5: Значения по умолчанию у параметров — только литерал

`FormalParameter` допускает произвольный инициализатор, из-за чего список параметров становится областью со своим порядком инициализации и отношением к TDZ. Ограничение литералом упраздняет вопрос.

**Files:**
- Modify: `docs/spec/grammar/syntax.txt` — `FormalParameter` и комментарий к нему
- Modify: `docs/spec/grammar/narrowings.md:65` — раздел 4

**Interfaces:**
- Consumes: `FormalParameterList` из задачи 4.
- Produces: ничего для последующих задач.

- [ ] **Step 1: Ограничить `FormalParameter`**

В `docs/spec/grammar/syntax.txt` заменить

```
FormalParameter :
    Identifier
    Identifier Initializer
```

на

```
FormalParameter :
    Identifier
    Identifier = Literal
```

- [ ] **Step 2: Дописать обоснование**

В `docs/spec/grammar/syntax.txt`, в комментарий после `ArrowFunction` (тот, что начинается «Функциональных выражений нет»), добавить абзацем:

```
# Значением по умолчанию служит Literal, а не произвольное выражение. При
# произвольном выражении список параметров стал бы областью: инициализатор в
# JavaScript видит предыдущие параметры (`function f(a, b = a + 1)`), а
# обращение к последующим даёт ошибку. Литерал этот вопрос упраздняет —
# вычислять нечего, значение известно на разборе. Ссылка на другой параметр
# записывается в теле функции.
```

- [ ] **Step 3: Поправить перечень сужений**

В `docs/spec/grammar/narrowings.md`, раздел 4, заменить строку

```
| `FormalParameter` содержит только `Identifier` и `Identifier Initializer` | образцы и параметр-остаток в параметрах объявленной функции |
```

на

```
| `FormalParameter` содержит только `Identifier` и `Identifier = Literal` | образцы и параметр-остаток в параметрах объявленной функции; значение по умолчанию, не являющееся литералом: `function f(a, b = a + 1) {}` |
```

- [ ] **Step 4: Убедиться, что `Initializer` уцелел там, где нужен**

```bash
grep -n "Initializer" docs/spec/grammar/syntax.txt
```

Ожидается: `Initializer` определён и используется в `LexicalBinding` (обе альтернативы) и больше нигде. В `FormalParameter` его нет.

- [ ] **Step 5: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 6: Коммит**

```bash
git add docs/spec
git commit -m "spec: значение по умолчанию у параметра — только литерал"
```

---

# Фаза 1. Каркас

### Task 6: Выделить `03-notation.md` из README

README совмещает landing репозитория, нормативные конвенции и обзор принципов. Конвенции уезжают в раздел 3. Здесь же — единственное содержательное изменение фазы 1: принцип о роли корпуса тестов переформулируется, потому что семантика переезжает в тело документа.

**Files:**
- Create: `docs/spec/03-notation.md`
- Modify: `docs/spec/README.md` — убрать раздел «Конвенции», переписать принцип о тестах

**Interfaces:**
- Produces: `03-notation.md` — место, куда последующие задачи и фаза 2 складывают конвенции.

- [ ] **Step 1: Создать `docs/spec/03-notation.md`**

Перенести в него раздел «Конвенции» из README **дословно**, начиная с абзаца «**Нормативность.**» и до конца таблицы префиксов включительно. Файл начать заголовком:

```markdown
# 3. Нотация и конвенции

Раздел задаёт, как читать остальные: что нормативно, как называются правила,
какие термины не переводятся.

## 3.1. Нормативность и идентификаторы
```

- [ ] **Step 2: Дописать семь конвенций качества**

В конец `docs/spec/03-notation.md` добавить:

```markdown
## 3.2. Конвенции изложения

**Код — по-английски, проза — по-русски.** Внутри обратных кавычек: имена
продукций, средств, параметров, ключевых слов. Русских идентификаторов не бывает
нигде. Снаружи — русский текст: «получатель», а не «receiver».

**Ссылка на утверждение — по идентификатору, а не по номеру раздела.** Номер
раздела допустим только там, где речь о разделе целиком. Идентификатор не
меняется никогда, номер раздела меняется при всякой перестройке.

**Раздел о конструкции имеет одну и ту же форму:** продукция, статические
правила, семантика, сужение относительно JavaScript, примеры.

**Неверный пример — дефект.** Примеры ненормативны, но обязаны быть верными:
они извлекаются из документа и прогоняются на V8.

**Всякая сводка справочна.** Нормативно утверждение на своём месте, рядом со
своей конструкцией. Сводка — приложение A, приложение B, приложение D —
существует, чтобы увидеть целое; её расхождение с телом есть устаревание копии,
а не дефект корректности.

**Непереводимые термины** — закрытый список. Он составляется в фазе 2 вместе с
разделом; до тех пор действует правило из 3.1: термины ECMA-262 приводятся в
оригинале.
```

Седьмая конвенция — о роли корпуса тестов — вносится не сюда, а в README
следующим шагом: это принцип, а не правило изложения. Конвенция о нормативности
тела уже перенесена шагом 1 в составе абзаца «**Нормативность.**».

- [ ] **Step 3: Убрать «Конвенции» из README**

Удалить из `docs/spec/README.md` раздел `## Конвенции` целиком, вместе с таблицей префиксов, и поставить на его место:

```markdown
## Конвенции

Нормативность, обязательность, термины, идентификаторы правил и их префиксы —
раздел 3, `03-notation.md`.
```

- [ ] **Step 4: Переформулировать принцип о корпусе тестов**

В `docs/spec/README.md` заменить раздел `## Принцип: семантику несёт корпус тестов` целиком на:

```markdown
## Принцип: проза задаёт правило, тест закрепляет значение

Всякое нормативное утверждение — абзац со стабильным идентификатором. Утверждение
задаёт правило: что вычисляется, в каком порядке, что происходит при отказе.

Точное значение закрепляет **нормативный корпус тестов**: пара «вход —
наблюдаемое поведение» на каждый идентификатор. Прозой значение фиксируется
хуже, чем примером. Что `[NaN].includes(NaN)` истинно, а `[NaN].indexOf(NaN)`
даёт −1; что `Object.keys({ b: 1, 2: 2, a: 3, 1: 4 })` даёт
`["1", "2", "b", "a"]`; что `String(1e21)` — это `"1e+21"`, а
`String(0.1 + 0.2)` — `"0.30000000000000004"`. Каждое из этих утверждений в виде
теста однозначно, а в виде абзаца — повод для спора о том, что имелось в виду.

У всякого `CS-RT-` и `CS-LIB-` есть хотя бы один тест; тест назван
идентификатором правила; примеры из документа — подмножество корпуса.

Тест поэтому не производен от спецификации, а входит в неё. Корпус исполняется
и интерпретатором, и V8: совпадение результатов есть практическая проверка
CS-CONF-0010.
```

- [ ] **Step 5: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 6: Убедиться, что префиксы не потерялись**

```bash
grep -n "CS-CONF-\|CS-LEX-\|CS-EE-\|CS-RT-\|CS-LIB-\|CS-DIV-" docs/spec/03-notation.md
```

Ожидается: таблица префиксов присутствует, все шесть строк на месте.

- [ ] **Step 7: Коммит**

```bash
git add docs/spec
git commit -m "spec: выделить раздел 3 «Нотация и конвенции» из README"
```

---

### Task 7: Выделить `02-conformance.md` из `01-scope.md`

**Files:**
- Create: `docs/spec/02-conformance.md`
- Modify: `docs/spec/01-scope.md` — убрать §1.4, поправить ссылки

**Interfaces:**
- Consumes: `03-notation.md` из задачи 6.
- Produces: `02-conformance.md` с правилами `CS-CONF-0002`—`CS-CONF-0008`.

- [ ] **Step 1: Создать `docs/spec/02-conformance.md`**

Перенести §1.4 «Соответствие реализации» из `01-scope.md` **дословно** — от абзаца «Реализация соответствует настоящей спецификации» до абзаца «Реализации разрешено» включительно. Файл начать заголовком:

```markdown
# 2. Соответствие

Раздел задаёт, что значит «реализация соответствует настоящей спецификации».
Инвариант, относительно которого соответствие определено, — раздел 1.
```

Подзаголовок `## 1.4. Соответствие реализации` при переносе снять: он становится телом раздела 2.

- [ ] **Step 2: Убрать §1.4 из `01-scope.md`**

Удалить перенесённое. Разделы 1.5 «Нормативные ссылки» и 1.6 «Термины» **не перенумеровывать**: их номера станут 1.4 и 1.5 в фазе 2, когда раздел 1 будет переписываться целиком. В фазе 1 содержание не меняется.

Вместо удалённого поставить:

```markdown
## 1.4. Соответствие

Требования к реализации — раздел 2, `02-conformance.md`.
```

- [ ] **Step 3: Поправить ссылки на раздел 9**

```bash
grep -rn "09-conformance\|раздел 9\|разделе 9" docs/spec/
```

В `docs/spec/README.md` из перечня разделов удалить строку про раздел 9: он отменён, его содержание — раздел 2. В `tools/check-spec.py` убрать `"09-conformance.md"` из `PLANNED`.

- [ ] **Step 4: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 5: Убедиться, что правила `CS-CONF-` не потерялись**

```bash
grep -c "CS-CONF-000" docs/spec/02-conformance.md
```

Ожидается: семь определений — `CS-CONF-0002` … `CS-CONF-0008`. `CS-CONF-0001` и `CS-CONF-0010` остаются в разделе 1: они задают инвариант, а не требование к реализации.

- [ ] **Step 6: Коммит**

```bash
git add docs/spec tools/check-spec.py
git commit -m "spec: выделить раздел 2 «Соответствие» из раздела 1"
```

---

### Task 8: Переименовать разделы

**Files:**
- Rename: `docs/spec/04-types.md` → `docs/spec/05-values.md`
- Rename: `docs/spec/07-builtins.md` → `docs/spec/10-library.md`
- Rename: `docs/spec/grammar/notes.md` → `docs/spec/04-lexical.md`
- Modify: все файлы, ссылающиеся на переименованные

**Interfaces:**
- Consumes: проверяльщик из задачи 1.
- Produces: новые имена, на которые ссылаются задачи 9 и 10.

- [ ] **Step 1: Переименовать через git**

```bash
git mv docs/spec/04-types.md docs/spec/05-values.md
git mv docs/spec/07-builtins.md docs/spec/10-library.md
git mv docs/spec/grammar/notes.md docs/spec/04-lexical.md
```

- [ ] **Step 2: Поправить заголовки внутри переименованных**

В `docs/spec/05-values.md` заменить заголовок `# 4. Типы, значения и преобразования` на `# 5. Типы, значения и преобразования`, а номера подразделов `## 4.1.` … `## 4.8.` — на `## 5.1.` … `## 5.8.`.

В `docs/spec/10-library.md` заменить `# 7. Встроенные средства` на `# 10. Встроенные средства`, а `## 7.1.` … `## 7.9.` — на `## 10.1.` … `## 10.9.`.

В `docs/spec/04-lexical.md` заменить `# Лексические правила` на `# 4. Лексика`, а нумерацию подразделов `## 1.` … `## 6.` — на `## 4.1.` … `## 4.6.`. Первый абзац «Нормативное дополнение к `grammar/lexical.txt`» оставить: продукции переедут сюда в фазе 2.

- [ ] **Step 3: Обновить `REMOVED` в проверяльщике**

Проверка вырезанного привязана к путям. Проверить, не съехал ли какой-то путь, и поправить:

```bash
python3 tools/check-spec.py; echo "код возврата: $?"
```

Если печатает `файл не найден, проверка вырезанного невозможна` — поправить соответствующую пару в `REMOVED`. Пути `grammar/syntax.txt`, `grammar/lexical.txt` и `B-divergences.md` этой задачей не затрагиваются, но `grammar/notes.md` затронут: заменить его на `04-lexical.md`.

- [ ] **Step 4: Починить ссылки**

```bash
grep -rn "04-types\.md\|07-builtins\.md\|grammar/notes\.md\|notes\.md" docs/spec/
```

Каждое совпадение заменить на новое имя. Ожидаемые места: `README.md` (перечень разделов), `grammar/lexical.txt` (шапка ссылается на `notes.md`), `grammar/narrowings.md`.

- [ ] **Step 5: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 6: Коммит**

```bash
git add -A docs/spec tools/check-spec.py
git commit -m "spec: переименовать разделы 4, 7 и лексические правила"
```

---

### Task 9: Переименовать приложения

**Files:**
- Rename: `docs/spec/grammar/narrowings.md` → `docs/spec/B-narrowings.md`
- Rename: `docs/spec/B-divergences.md` → `docs/spec/C-divergences.md`
- Rename: `docs/spec/C-inherited.md` → `docs/spec/D-inherited.md`
- Rename: `docs/spec/embedding.md` → `docs/spec/E-embedding.md`
- Modify: все файлы, ссылающиеся на переименованные

**Interfaces:**
- Consumes: новые имена из задачи 8.
- Produces: буквенные имена приложений, на которые ссылается задача 10.

- [ ] **Step 1: Переименовать через git**

```bash
git mv docs/spec/grammar/narrowings.md docs/spec/B-narrowings.md
git mv docs/spec/B-divergences.md docs/spec/C-divergences.md
git mv docs/spec/C-inherited.md docs/spec/D-inherited.md
git mv docs/spec/embedding.md docs/spec/E-embedding.md
```

- [ ] **Step 2: Обновить `REMOVED`**

В `tools/check-spec.py` заменить пары `("B-divergences.md", …)` на `("C-divergences.md", …)` — их две: `"o.k++"` и `"??="`.

- [ ] **Step 3: Поправить заголовки и самоназвания**

В `docs/spec/B-narrowings.md` заменить заголовок `# Сужения относительно JavaScript` на `# Приложение B. Сужения относительно JavaScript`.

В `docs/spec/C-divergences.md` заменить `# Приложение B. Расхождения с ECMA-262` на `# Приложение C. Расхождения с ECMA-262`.

В `docs/spec/D-inherited.md` заменить `# Приложение C. Наследуемое поведение, которое хотелось бы изменить` на `# Приложение D. Наследуемое поведение, которое хотелось бы изменить`.

В `docs/spec/E-embedding.md` заменить `# Модель встраивания` на `# Приложение E. Модель встраивания`.

- [ ] **Step 4: Перенумеровать перекрёстные упоминания приложений**

```bash
grep -rn "приложени" docs/spec/
```

Заменить по таблице соответствия: «приложение A» → «приложение B», «приложение B» → «приложение C», «приложение C» → «приложение D».

Заменять **от конца к началу** — сначала C→D, потом B→C, потом A→B, — иначе первая замена испортит вход второй.

- [ ] **Step 5: Починить ссылки на файлы**

```bash
grep -rn "narrowings\.md\|B-divergences\.md\|C-inherited\.md\|embedding\.md" docs/spec/
```

Каждое совпадение заменить на новое имя.

- [ ] **Step 6: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 7: Проверить, что `CS-DIV-0001` не потерялся**

```bash
grep -rn "CS-DIV-0001" docs/spec/
```

Ожидается: определение в `C-divergences.md`, ссылки в `05-values.md`, `10-library.md`, `06-names.md`. Ни одна ссылка не указывает на «приложение B».

- [ ] **Step 8: Коммит**

```bash
git add -A docs/spec tools/check-spec.py
git commit -m "spec: перенумеровать приложения в B, C, D, E"
```

---

### Task 10: Заглушки и перечень разделов

**Files:**
- Create: `docs/spec/07-expressions.md`, `docs/spec/08-statements.md`, `docs/spec/09-functions.md`, `docs/spec/11-errors.md`, `docs/spec/12-tests.md`, `docs/spec/A-grammar.md`
- Modify: `docs/spec/README.md` — перечень разделов
- Modify: `tools/check-spec.py` — `PLANNED`, новая проверка ссылок на разделы
- Modify: все файлы — ссылки на номера разделов

**Interfaces:**
- Consumes: имена из задач 6—9.
- Produces: полный каркас, с которого начинается фаза 2.

- [ ] **Step 1: Создать шесть заглушек**

Каждая — по одному образцу. Для `07-expressions.md`:

```markdown
# 7. Выражения

> **Статус: план.** Раздел не написан. Продукции выражений — в
> `grammar/syntax.txt`, раздел 2; статические правила — в `rules.json`. Фаза 2
> переносит их сюда вместе с семантикой вычисления.

Раздел определит порядок вычисления, приоритеты, короткое замыкание, обращение к
свойству и необязательные цепочки, приведения при операторах.
```

Для `08-statements.md`:

```markdown
# 8. Statement и объявления

> **Статус: план.** Раздел не написан. Продукции — в `grammar/syntax.txt`,
> раздел 3; статические правила — в `rules.json`. Фаза 2 переносит их сюда
> вместе с семантикой вычисления.

Раздел определит `Block`, `IfStatement`, `ForOfStatement`, `BreakStatement`,
`ContinueStatement`, `ReturnStatement`, `LexicalDeclaration` и образцы.
```

Для `09-functions.md`:

```markdown
# 9. Функции

> **Статус: план.** Раздел не написан. Продукции — в `grammar/syntax.txt`,
> раздел 4. Фаза 2 переносит их сюда.

Раздел определит `FunctionDeclaration`, `ArrowFunction`, передачу аргументов и
возврат значения. Граф вызовов и его ацикличность определены в разделе 6 и сюда
не переезжают.
```

Для `11-errors.md`:

```markdown
# 11. Ошибки и аварийное завершение

> **Статус: план.** Раздел не написан.

Раздел определит классификацию ошибок, форму аварийного завершения, атомарность
единицы исполнения и форму ограничений ресурсов.

Открыто: собственного префикса идентификаторов у раздела нет. В перечне
раздела 3 есть `CS-CONF-`, `CS-LEX-`, `CS-EE-`, `CS-RT-`, `CS-LIB-`, `CS-DIV-`,
и ни один не обозначает класс ошибки времени исполнения. Префикс утверждается
вместе с разделом.

Открыто: ограничения ресурсов. Единица исполнения, исчерпавшая лимит, снимается —
на Web такого снятия не происходит, там действуют ограничения браузера. Считать
ли это расхождением или отнести к области, где наблюдаемое поведение не
определено, решается здесь.
```

Для `12-tests.md`:

```markdown
# 12. Корпус тестов

> **Статус: план.** Раздел не написан, корпуса нет.

Раздел определит устройство корпуса: форму записи «вход — наблюдаемое
поведение», именование тестов по идентификатору правила, порядок исполнения
интерпретатором и V8.

Роль корпуса задана принципом в `README.md`: проза задаёт правило, тест
закрепляет точное значение.
```

Для `A-grammar.md`:

```markdown
# Приложение A. Сводка грамматики

> **Статус: план.** Приложение не собрано.

Приложение справочно. Нормативны продукции в разделах 4, 7, 8, 9; здесь они
будут сведены в один перечень, чтобы цепочку приоритетов и состав продукций
можно было увидеть целиком.

До фазы 2 нормативны `grammar/lexical.txt` и `grammar/syntax.txt`.
```

- [ ] **Step 2: Убрать `PLANNED`**

В `tools/check-spec.py` заменить

```python
PLANNED: set[str] = {"08-errors.md", "09-conformance.md"}
```

на

```python
PLANNED: set[str] = set()
```

`08-errors.md` заменён на `11-errors.md`, `09-conformance.md` отменён задачей 7.

- [ ] **Step 3: Переписать перечень разделов в README**

В `docs/spec/README.md` заменить таблицу разделов на:

```markdown
| № | Артефакт | Содержание | Статус |
|---|---|---|---|
| 0 | `README.md` | структура, принципы, статусы | готов |
| 1 | `01-scope.md` | область применения, отношение к JavaScript | готов |
| 2 | `02-conformance.md` | требования к реализации | готов |
| 3 | `03-notation.md` | нотация, конвенции, термины | готов |
| 4 | `04-lexical.md` | исходный текст, токены, литералы | ведётся |
| 5 | `05-values.md` | типы, значения, преобразования | готов |
| 6 | `06-names.md` | области, связывание, граф вызовов | готов |
| 7 | `07-expressions.md` | выражения | план |
| 8 | `08-statements.md` | `Statement` и объявления | план |
| 9 | `09-functions.md` | функции | план |
| 10 | `10-library.md` | встроенные средства | готов |
| 11 | `11-errors.md` | ошибки, аварийное завершение | план |
| 12 | `12-tests.md` | устройство корпуса | план |
| A | `A-grammar.md` | сводка грамматики (справочное) | план |
| B | `B-narrowings.md` | сужения относительно JavaScript (справочное) | готов |
| C | `C-divergences.md` | расхождения с ECMA-262 | готов |
| D | `D-inherited.md` | наследуемое поведение (справочное) | ведётся |
| E | `E-embedding.md` | модель встраивания | ведётся |

Нормативны также `grammar/lexical.txt` и `grammar/syntax.txt` — до фазы 2,
которая переносит продукции в разделы 4, 7, 8, 9 и собирает из них приложение A.
```

- [ ] **Step 4: Добавить проверку ссылок на разделы**

В `tools/check-spec.py` добавить функцию и включить её в `CHECKS` перед `check_removed`:

```python
SECTIONS = set(range(0, 13))


def check_section_refs(errors):
    pat = re.compile(r"\bраздел[аеу]?\s+(\d+)")
    for p in sources():
        for n, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
            for num in pat.findall(line):
                if int(num) not in SECTIONS:
                    errors.append(f"{p}:{n}: ссылка на несуществующий раздел {num}")
```

- [ ] **Step 5: Убедиться, что проверка падает**

```bash
python3 tools/check-spec.py; echo "код возврата: $?"
```

Ожидается код возврата 0: проверка ловит только выход за диапазон 0—12, а все нынешние ссылки — «раздел 2» (лексика), «раздел 4» (типы), «раздел 7» (библиотека), «раздел 8» (ошибки) — в диапазон попадают, хотя указывают не туда.

Это ограничение осознанное: отличить «раздел 4» верный от «раздела 4» устаревшего машина не может, потому что оба существуют. Проверка страхует только от грубой ошибки — ссылки на раздел 14. Содержательную перенумерацию делает следующий шаг вручную, а проверяет её шаг 8 чтением.

- [ ] **Step 6: Перенумеровать ссылки на разделы**

По таблице соответствия из шапки плана. Порядок замен важен: идти **от больших номеров к меньшим**, иначе замена испортит вход следующей.

```bash
grep -rn "раздел 2\|раздела 2\|разделе 2" docs/spec/    # лексика   → 4
grep -rn "раздел 3\|раздела 3\|разделе 3" docs/spec/    # грамматика → grammar/syntax.txt
grep -rn "раздел 4\|раздела 4\|разделе 4" docs/spec/    # типы      → 5
grep -rn "раздел 5\|раздела 5\|разделе 5" docs/spec/    # тесты     → 12
grep -rn "раздел 7\|раздела 7\|разделе 7" docs/spec/    # библиотека → 10
grep -rn "раздел 8\|раздела 8\|разделе 8" docs/spec/    # ошибки    → 11
```

Каждое совпадение читать в контексте: «раздел 6» остаётся разделом 6, «раздел 1» — разделом 1. Ссылки на подразделы вида «раздел 4.7» перенумеровывать вместе с разделом: «раздел 5.7».

Ссылки на «раздел 3» заменять не на номер, а на `` `grammar/syntax.txt` ``: раздела 3 в смысле грамматики больше нет, а раздел 3 теперь — нотация.

- [ ] **Step 7: Проверить**

```bash
python3 tools/check-spec.py
```

Ожидается: `нарушений нет`.

- [ ] **Step 8: Прочитать глазами два самых связных файла**

Автоматика ловит битые ссылки, но не ловит ссылку, которая указывает на существующий, но не тот раздел. Прочитать целиком `docs/spec/README.md` и `docs/spec/01-scope.md` и убедиться, что каждая ссылка ведёт туда, куда должна.

- [ ] **Step 9: Коммит**

```bash
git add -A docs/spec tools/check-spec.py
git commit -m "spec: каркас новой структуры — заглушки и перенумерация"
```

---

## Что этот план не делает

Фаза 2 планированию не поддаётся и здесь не расписана. В ней:

- продукции переезжают из `grammar/*.txt` в разделы 4, 7, 8, 9;
- правила переезжают из `rules.json` в свои разделы, файл удаляется;
- библиотека переезжает из `builtins.json` в раздел 10 по устройству из документа решения (арность в записи вызова, `mutates` и `callback` — нормативными утверждениями, таблица в три столбца, пять длинных — подразделами), файл удаляется;
- пишутся разделы 7 и 8 — семантика выражений и `Statement`, которой сейчас нет нигде;
- собирается приложение A;
- решаются шесть открытых вопросов по языку и три по встраиванию из раздела 5 документа решения.

По каждому разделу фазы 2 — отдельное решение, как заведено.
