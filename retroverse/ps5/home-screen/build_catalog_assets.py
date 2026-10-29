#!/usr/bin/env python3
"""Build offline native text indexes from current public thumbnail/file listings.

The foreground PS5 app can browse catalogs even without a running helper.
No game binaries are redistributed; only filenames from source indexes.
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
archives=load("archive_catalog_build")
for platform,system in lib.SYSTEMS.items():
    names=lib.fetch(platform,system)
    clean=sorted(set(x[:-4] for x in names if x.lower().endswith(".png")),key=str.casefold)
    (out/(platform+".txt")).write_text("\n".join(clean)+"\n",encoding="utf-8")
    print("catalog",platform,len(clean),flush=True)
for platform,name in archives.COLLECTIONS.items():
    names=archives.raw_listing(platform,name)
    # The index is a hint, not proof every file can be transferred.
    (out/("HOST_"+platform+".txt")).write_text("\n".join(names)+"\n",encoding="utf-8")
    print("host",platform,len(names),flush=True)
print("Catalog assets complete",sum(p.stat().st_size for p in out.glob("*.txt")),"bytes")
