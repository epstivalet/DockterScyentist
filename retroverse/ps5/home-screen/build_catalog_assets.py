#!/usr/bin/env python3
"""Bundle native offline indexes with approved, hash-pinned official homebrew sources.

A title appearing in Libretro's artwork index does not imply it is downloadable.
The native HOSTS list contains ONLY explicit verified_sources.json entries.
"""
from pathlib import Path
import importlib.util
import sys

src=Path(__file__).resolve().parent.parent/"full"
out=Path(sys.argv[1])/"catalog"
out.mkdir(parents=True,exist_ok=True)

def load(name):
    spec=importlib.util.spec_from_file_location(name,str(src/(name+".py")))
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

lib=load("catalog_build")
official=load("verified_source_build")
rows=official.validated()
for platform,system in lib.SYSTEMS.items():
    names=lib.fetch(platform,system)
    clean=sorted(set(x[:-4] for x in names if x.lower().endswith(".png")),key=str.casefold)
    (out/(platform+".txt")).write_text("\n".join(clean)+"\n",encoding="utf-8")
    print("catalog",platform,len(clean),flush=True)
for platform in lib.SYSTEMS:
    names=sorted((r["filename"] for r in rows if r["platform"]==platform),key=str.casefold)
    (out/("HOST_"+platform+".txt")).write_text(
        "\n".join(names)+("\n" if names else ""),encoding="utf-8")
    print("verified official downloads",platform,len(names),flush=True)
print("Catalog assets complete",sum(p.stat().st_size for p in out.glob("*.txt")),"bytes")
