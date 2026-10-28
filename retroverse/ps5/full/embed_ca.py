from pathlib import Path
import os
p=Path(__file__).parent
trust=Path("/etc/ssl/certs/ca-certificates.crt")
if not trust.exists():raise RuntimeError("Runner CA bundle is missing")
data=trust.read_bytes()
if len(data)<15000 or b"-----BEGIN CERTIFICATE-----" not in data:raise RuntimeError("Invalid CA roots")
with (p/"ca_data.inc").open("w") as out:
    out.write("/* Embedded public CA trust bundle, generated from ubuntu ca-certificates. */\n")
    out.write("static const unsigned char rv_ca_roots[] = {\n")
    for off in range(0,len(data),32):
        out.write("    "+",".join("0x%02x"%b for b in data[off:off+32])+",\n")
    out.write("};\n#define RV_CA_ROOTS_LEN (sizeof(rv_ca_roots))\n")
print("Embedded",len(data),"bytes of CA roots",flush=True)
