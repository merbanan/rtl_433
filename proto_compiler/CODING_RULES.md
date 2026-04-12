# Proto-Compiler Coding Rules

## Structured data: use frozen dataclasses or NamedTuples, not dicts

Structured records passed between functions must be defined as frozen dataclasses or `NamedTuple`s, not plain dicts. Dict access (`entry["key"]`) is untyped, unrefactorable, and hides the contract between caller and callee. If a set of fields travel together, they belong in a named, immutable type.

Prefer `@dataclass(frozen=True)` for records with heterogeneous field types. Prefer `NamedTuple` for simple flat records where positional construction is natural. Mutability is a footgun — if a record does not need to be mutated after construction, make it immutable.

**Wrong:**
```python
entry = {"kind": "validate", "params": params, "fn": fn}
kind = entry["kind"]
```

**Right:**
```python
@dataclass(frozen=True)
class CallableEntry:
    kind: str
    params: tuple
    fn: Any
    ...

entry = CallableEntry(kind="validate", params=params, fn=fn)
kind = entry.kind
```

## Impossible states: assert, don't handle

If a condition cannot occur given correct upstream behavior, do not write defensive code for it. Unnecessary guards:
- obscure which states are actually possible
- imply the code is less reliable than it is
- create dead branches that rot silently

If a state is truly impossible (because the validation layer already caught it), assert or just let it crash. If it is possible, fix the layer that should have caught it.

**Wrong:**
```python
if fn is None:  # "just in case"
    return snippet
try:
    result = thing_that_cannot_raise()
except Exception:
    pass
```

**Right:** don't write the check at all. If `fn` can be `None`, fix the code that produces it.

## dsl.py is the validation layer; codegen.py trusts it

`dsl.py` is responsible for validating and normalizing all user-supplied protocol definitions. By the time `codegen.py` receives a `CallableEntry` or `_fields` tuple, those values are guaranteed correct. `codegen.py` must not re-validate or defensively re-check things that `dsl.py` already enforced.

Examples of what dsl.py must enforce so codegen.py need not:
- All non-external callables have a non-`None` `expr_tree`
- All callable parameters have a resolved type
- `validate_*` return type is always `bool`
- `fn` is never `None` on a `CallableEntry`

## Type inference over silent defaults

When a parameter has no explicit annotation, look it up — don't silently default to a magic value without trying. For parameters referencing DSL fields:
1. Check top-level fields of the class
2. Check fields of nested Repeatables (via `Rows`/`Repeat` specs)
3. Check return types of other callables with that name
4. Fall back to `int` (all DSL bit-level primitives are integers)

## Use list comprehensions for straightforward transformations

When building a list by mapping a function over an iterable, use a list comprehension.

**Wrong:**
```python
param_parts = []
for p in params:
    c_t = param_c_types.get(p, "int")
    param_parts.append(f"{c_t} {p}")
```

**Right:**
```python
param_types_and_names = [f"{_c_type(param_types[p])} {p}" for p in params]
```

## No unused imports or arguments

Remove imports that are not referenced in the file. Remove function parameters that are not used in the body. Dead imports clutter the namespace and create false dependency signals. Dead parameters mislead callers about what the function actually needs.

## No spurious f-strings

Don't use an f-string if it performs no interpolation. And `print(f"x {y}")` is more clearly written as `print(x, y)` — let `print` handle the spacing.

**Wrong:**
```python
msg = f"something went wrong"
print(f"count {n}")
```

**Right:**
```python
msg = "something went wrong"
print("count", n)
```

## No dead code

Delete unused imports, unreferenced aliases, and methods that nothing calls. Dead code misleads readers into thinking it's load-bearing, drifts silently when the live code changes, and makes the file longer for no reason. If it's needed later, git has it.

## Extract rather than copy-paste

If the same logic appears in more than one place, extract it into a function. Duplicated code means duplicated bugs: a fix in one copy is a regression in the copy you forgot. When two blocks share the same structure but differ in a few parameters, those parameters become the function's arguments.

**Signal:** if you're about to write a loop body that already exists elsewhere in the file, stop and extract.

## One implementation per concept

If the DSL exposes a method that computes X, codegen must not reimplement X. Parallel implementations drift: one gets fixed, the other doesn't. If the DSL's version doesn't quite fit codegen's needs, extend the DSL's version — don't write a second one.

## Don't silently accept invalid input

If a value should always be wrapped/normalized by an earlier layer, don't add a fallback that silently handles the unwrapped case. That hides bugs in the earlier layer. Raise instead, so the real problem surfaces.

## Name variables for what they are

Variable names should reflect what the value is, not how it is computed or where it is going.

**Wrong:** `param_parts` (describes shape, not content)

**Right:** `param_types_and_names` (describes content)
