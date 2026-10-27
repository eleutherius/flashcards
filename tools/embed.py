#!/usr/bin/env python3
"""Вбудовує файл у C-заголовок: embed.py <input> <output.h> <symbol>"""
import sys

src, dst, sym = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
with open(dst, "w") as f:
    f.write("/* згенеровано tools/embed.py з %s — не редагувати */\n" % src)
    f.write("static const unsigned char %s[] = {\n" % sym)
    for i in range(0, len(data), 16):
        f.write("  " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",\n")
    f.write("};\n")
    f.write("static const unsigned int %s_len = %d;\n" % (sym, len(data)))
