#!/usr/bin/env python3
"""The post-processing shaders of the game window as a C++ header.

  python3 tools/shaders.py        # runtime/shaders/*.frag.hlsl -> runtime/src/shaders_embedded.h

Each shader is compiled for the two APIs of SDL's GPU renderer: SPIR-V (Vulkan) and DXIL
(Direct3D 12). The header is kept in the repository: the game compiles on the player's PC
from the SDK, with no shader compiler there.

SPIR-V comes from glslc (shaderc, also in the Vulkan SDK) or from dxc; DXIL only from dxc
(github.com/microsoft/DirectXShaderCompiler, release package, which signs the DXIL itself).
dxc is looked for in $DXC, in PATH and in ~/tools/dxc-*/bin/. Without it, the header gets
no DXIL and the game turns the passes off under Direct3D 12 (it says so in the log).
"""

import glob
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ROOT / "runtime" / "shaders"
OUT = ROOT / "runtime" / "src" / "shaders_embedded.h"


def find_dxc() -> str | None:
    if os.environ.get("DXC"):
        return os.environ["DXC"]
    found = shutil.which("dxc")
    if found:
        return found
    candidates = sorted(glob.glob(str(pathlib.Path.home() / "tools" / "dxc-*" / "bin" / "dxc")))
    return candidates[-1] if candidates else None


def compile_spirv(src: pathlib.Path, out: pathlib.Path, dxc: str | None) -> None:
    glslc = shutil.which("glslc")
    if glslc:
        cmd = [glslc, "-fshader-stage=frag", "-x", "hlsl", "-fentry-point=main", "-O", str(src), "-o", str(out)]
    elif dxc:
        cmd = [dxc, "-spirv", "-T", "ps_6_0", "-E", "main", "-O3", str(src), "-Fo", str(out)]
    else:
        sys.exit("shaders.py: neither glslc nor dxc found")
    subprocess.run(cmd, check=True)


def compile_dxil(src: pathlib.Path, out: pathlib.Path, dxc: str) -> None:
    subprocess.run([dxc, "-T", "ps_6_0", "-E", "main", "-O3", str(src), "-Fo", str(out)], check=True)


def array(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 24):
        rows.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    return f"inline const unsigned char {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def ident(stem: str) -> str:
    return "".join(part.capitalize() for part in stem.replace(".", "_").split("_"))


def main() -> None:
    dxc = find_dxc()
    if not dxc:
        print("shaders.py: dxc not found; the header gets no DXIL (Direct3D 12)")
    parts = [
        "// Made by tools/shaders.py from runtime/shaders/. Do not edit.\n",
        "#pragma once\n\n",
        "#include <cstddef>\n\n",
        "struct EmbeddedShader {\n",
        "    const unsigned char* spirv;\n",
        "    size_t spirv_size;\n",
        "    const unsigned char* dxil;  // null without dxc when the header was made\n",
        "    size_t dxil_size;\n",
        "};\n\n",
    ]
    with tempfile.TemporaryDirectory() as tmp:
        for src in sorted(SOURCES.glob("*.frag.hlsl")):
            name = ident(src.name.removesuffix(".frag.hlsl"))
            spv = pathlib.Path(tmp) / (name + ".spv")
            compile_spirv(src, spv, dxc)
            parts.append(array(f"k{name}Spirv", spv.read_bytes()))
            dxil_ref = "nullptr, 0"
            if dxc:
                dxil = pathlib.Path(tmp) / (name + ".dxil")
                compile_dxil(src, dxil, dxc)
                parts.append(array(f"k{name}Dxil", dxil.read_bytes()))
                dxil_ref = f"k{name}Dxil, sizeof k{name}Dxil"
            parts.append(f"inline constexpr EmbeddedShader k{name}Shader = {{k{name}Spirv, sizeof k{name}Spirv, {dxil_ref}}};\n\n")
            print(f"{src.name}: {name}")
    OUT.write_text("".join(parts).rstrip("\n") + "\n")
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
