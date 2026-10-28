from pathlib import Path
p=Path(__file__).parent
data=(p/"index.html").read_bytes()
with (p/"assets.inc").open("w") as f:
    f.write("static const unsigned char ui_page[] = {\n")
    for i in range(0,len(data),24):
        f.write("    "+", ".join("0x%02x"%b for b in data[i:i+24])+",\n")
    f.write("};\n#define UI_PAGE_LEN (sizeof(ui_page))\n")
print("Embedded",len(data),"bytes")
