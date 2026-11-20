#!/usr/bin/env python3
"""Write fixture JSON into a C++ translation unit as raw string literals."""

import pathlib
import sys


def main() -> None:
    if len(sys.argv) < 2 or (len(sys.argv) - 2) % 2 != 0:
        raise SystemExit("usage: embed_fixtures.py OUT.cpp VAR path [VAR path ...]")
    out_path = pathlib.Path(sys.argv[1])
    pairs = sys.argv[2:]
    chunks = ['#include "fixture_data.hpp"\n', "namespace xdcam {\n"]
    for index in range(0, len(pairs), 2):
        var = pairs[index]
        text = pathlib.Path(pairs[index + 1]).read_text(encoding="utf-8")
        if ")XDCAM" in text:
            raise SystemExit(f"fixture delimiter collision in {pairs[index + 1]}")
        chunks.append(f'const char {var}[] = R"XDCAM({text})XDCAM";\n')
    chunks.append("}  // namespace xdcam\n")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("".join(chunks), encoding="utf-8")


if __name__ == "__main__":
    main()
