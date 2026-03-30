"""
Generate a C device decoder from a proto_compiler protocol module.

Usage:
    python proto_compiler/build.py <proto_module.py> <output.c>
"""

from __future__ import annotations

import importlib.util
import inspect
import sys
from pathlib import Path

# Allow running directly from the repo root without installing the package.
sys.path.insert(0, str(Path(__file__).parent.parent))

from proto_compiler.compiler import compile as compile_protocol  # noqa: E402
from proto_compiler.dsl import Protocol, Variant  # noqa: E402


def find_root_protocols(module) -> list[type]:
    """Return all compilable root Protocol subclasses in *module*.

    Excludes:
    - sub-protocols referenced inside Repeat[] fields
    - abstract base classes that are superclasses of other protocols in the
      same module (e.g. a shared base with no preamble)
    """
    protocols = [
        obj
        for obj in vars(module).values()
        if inspect.isclass(obj) and issubclass(obj, Protocol) and obj not in (Protocol, Variant)
    ]

    sub_protocols = {
        ref
        for proto in protocols
        for ref in proto.referenced_protocols()
    }
    delegated_protocols = {
        ref
        for proto in protocols
        for ref in proto.delegated_protocols()
    }

    protocol_set = set(protocols)
    base_classes = {
        base
        for proto in protocols
        for base in proto.__mro__[1:]
        if base in protocol_set
    }

    return [
        p
        for p in protocols
        if p not in sub_protocols and p not in delegated_protocols and p not in base_classes
    ]


def find_root_protocol(module) -> type:
    """Return the single root Protocol in *module* (raises if not exactly one)."""
    roots = find_root_protocols(module)
    if len(roots) != 1:
        names = [p.__name__ for p in roots]
        raise RuntimeError(
            f"Expected exactly one root Protocol in {module.__file__!r}, "
            f"found {len(roots)}: {names}"
        )
    return roots[0]


def generate(proto_path: Path, out_path: Path) -> None:
    spec = importlib.util.spec_from_file_location(proto_path.stem, proto_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    root = find_root_protocol(module)
    module_doc = inspect.getdoc(module)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    c_source = compile_protocol(root, source_path=str(proto_path), module_doc=module_doc)
    out_path.write_text(c_source)



def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <proto_module.py> <output.c>", file=sys.stderr)
        sys.exit(1)

    generate(Path(sys.argv[1]), Path(sys.argv[2]))


if __name__ == "__main__":
    main()
