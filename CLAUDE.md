# rtl_433

C99 radio decoder framework. The main codebase is standard C; the interesting work in this branch is the proto-compiler.

## proto_compiler

A Python DSL that compiles radio protocol definitions (Python classes) into C source files for rtl_433.

Key documents — read these before writing any code in `proto_compiler/`:

- **`proto_compiler/SEMANTICS.md`** — authoritative spec for the DSL and the C it generates. The ground truth for what the compiler should do.
- **`proto_compiler/CODING_RULES.md`** — coding style rules for this codebase. Mandatory reading before writing any code here.
- **`proto_compiler/COMPILER_BUGS.md`** — known deviations from the spec. Check this before reporting a bug or before fixing behavior that might be intentional.

Entry points:
- `proto_compiler/dsl.py` — DSL class definitions; the validation layer. All user input is validated here.
- `proto_compiler/codegen.py` — code generator; trusts `dsl.py` output, no re-validation.
- `proto_compiler/protocols/` — protocol definitions using the DSL.

## Tests

```
python3 -m pytest proto_compiler/test/
```

All tests must pass before committing.
