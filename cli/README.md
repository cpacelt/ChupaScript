# chupa — the interactive shell

`chupa` is a small read-eval-print loop for ChupaScript. It holds a single
`CS::Context` for the session and lets you put data into it, evaluate
expressions against it, and run scripts that mutate it — without writing a
host program or a test file. It is meant for poking at the language and its
error messages, not for production embedding: it wraps the same public
interface (`core/include`) any host would use.

## Building and running

```
cmake -B build && cmake --build build -j
./build/cli/chupa -repl
```

Running `chupa` without `-repl` prints usage and exits; it does not start
the shell:

```
$ ./build/cli/chupa
chupa 0.1.0

usage:
  chupa -repl    start the interactive shell
```

Once started, the shell greets you and waits for lines on standard input:

```
chupa 0.1.0, :help for commands
>
```

There is no line editor: input is read with `std::getline`, so there is no
history and no arrow-key recall — only whatever the terminal itself provides.

## Commands

```
> :help
  expr: <expression>   evaluate an expression
  script: <statements> run a script
  :set <name> = <literal>  put a variable into the context
  :vars                    list the context
  :reset                   start with an empty context
  :help                    this text
  :quit                    leave
```

That is the complete list — there is nothing else to discover.

## The two modes

Every non-command line must start with `expr:` or `script:`; the shell does
not guess which one you mean.

**`expr:`** evaluates one expression and prints its result as a language
literal:

```
> expr: 1 + 2
3
> :set user = {'name': 'Vasya'}
> expr: user.name
'Vasya'
```

**`script:`** runs statements against the context. On success it prints
nothing at all — the effect is only visible through `:vars`:

```
> :set state = {'count': 0}
> script: state.count = 42;
> :vars
state = {'count': 42}
```

Both modes report failures the same way: a caret line pointing at the
offending column, then the message, with the column counted in UTF-8
characters rather than bytes:

```
> expr: 1 +
           ^
error: expected an expression
```

## Two rough edges

**`:set` only accepts a literal, never an expression.** This is the shell
surfacing a language-level rule: data is put into the context by
`setVariable`, which parses the text as a literal and rejects anything else
with a `Data` error. The very first thing most sessions try —
`:set n = 1 + 1`, expecting arithmetic — hits this:

```
> :set n = 1 + 1
error: expression is not allowed in data
note: data is set from a literal, not an expression
```

To compute a value, evaluate an expression and set the literal result by
hand, or shape it as a literal from the start (`:set n = 2`).

**The context only grows.** Nothing in it is freed one piece at a time, so a
long session — many `:set`, `expr:`, and `script:` lines in a row — keeps
accumulating storage for as long as the shell runs. `:reset` is the only way
to reclaim it, and it is all or nothing: it tears down the whole context and
builds an empty one, taking every variable you had set along with the
garbage. There is no way to keep your data and free just the garbage:

```
> :set n = 1
> :vars
n = 1
> :reset
the context is empty
> :vars
the context is empty
```
