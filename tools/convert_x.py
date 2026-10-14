"""Converts binary / MSZIP-compressed DirectX .x files to the text format.

The Wii U port's .x loader (runtime/blitz3d/loader_x.cpp) only reads text .x files. SCP:CB 1.3.11 has one
compressed binary model (GFX/items/Battery/Battery.x). The build runs this over the content folder :

    python tools/convert_x.py <folder>      converts every binary .x file under <folder> in place

Binary format : DirectX "xof 0303bin 0032" token stream; "bzip" files are MSZIP compressed (a 4 byte total size,
then blocks of [uncompressed size u16][compressed size u16]["CK"][raw deflate], each block using the previous
block's output as its dictionary).
"""
import os
import struct
import sys
import zlib

TOKEN_NAME, TOKEN_STRING, TOKEN_INTEGER, TOKEN_GUID = 1, 2, 3, 5
TOKEN_INTEGER_LIST, TOKEN_FLOAT_LIST = 6, 7
SIMPLE = {10: '{', 11: '}', 12: '(', 13: ')', 14: '[', 15: ']', 16: '<', 17: '>', 18: '.', 19: ',', 20: ';',
          31: 'template', 40: 'WORD', 41: 'DWORD', 42: 'FLOAT', 43: 'DOUBLE', 44: 'CHAR', 45: 'UCHAR',
          46: 'SWORD', 47: 'SDWORD', 48: 'void', 49: 'string', 50: 'unicode', 51: 'cstring', 52: 'array'}


def decompress(data):
    total = struct.unpack_from('<I', data, 16)[0]
    pos = 20
    out = bytearray()
    prev = b''
    while pos < len(data) and len(out) < total - 16:
        usize, csize = struct.unpack_from('<HH', data, pos)
        pos += 4
        if data[pos:pos + 2] != b'CK':
            raise ValueError('bad MSZIP block')
        block = data[pos + 2:pos + csize]
        pos += csize
        d = zlib.decompressobj(-15, zdict=prev) if prev else zlib.decompressobj(-15)
        chunk = d.decompress(block) + d.flush()
        out += chunk
        prev = chunk[-32768:]
    return bytes(out)


def to_text(body, float_size):
    out = []
    pos = 0
    n = len(body)
    while pos + 2 <= n:
        tok = struct.unpack_from('<H', body, pos)[0]
        pos += 2
        if tok == TOKEN_NAME:
            cnt = struct.unpack_from('<I', body, pos)[0]
            out.append(body[pos + 4:pos + 4 + cnt].decode('latin-1'))
            pos += 4 + cnt
        elif tok == TOKEN_STRING:
            cnt = struct.unpack_from('<I', body, pos)[0]
            s = body[pos + 4:pos + 4 + cnt].decode('latin-1')
            pos += 4 + cnt
            term = struct.unpack_from('<H', body, pos)[0]
            pos += 2
            out.append('"%s"%s' % (s, SIMPLE.get(term, ';')))
        elif tok == TOKEN_INTEGER:
            out.append('%d;' % struct.unpack_from('<I', body, pos)[0])
            pos += 4
        elif tok == TOKEN_GUID:
            g = body[pos:pos + 16]
            pos += 16
            a, b, c = struct.unpack_from('<IHH', g, 0)
            out.append('<%08X-%04X-%04X-%s-%s>' % (a, b, c, g[8:10].hex().upper(), g[10:16].hex().upper()))
        elif tok == TOKEN_INTEGER_LIST:
            cnt = struct.unpack_from('<I', body, pos)[0]
            vals = struct.unpack_from('<%dI' % cnt, body, pos + 4)
            pos += 4 + 4 * cnt
            out.append(''.join('%d;' % v for v in vals))
        elif tok == TOKEN_FLOAT_LIST:
            cnt = struct.unpack_from('<I', body, pos)[0]
            fmt = '<%d%s' % (cnt, 'd' if float_size == 64 else 'f')
            vals = struct.unpack_from(fmt, body, pos + 4)
            pos += 4 + cnt * (8 if float_size == 64 else 4)
            out.append(''.join('%.9g;' % v for v in vals))
        elif tok in SIMPLE:
            out.append(SIMPLE[tok])
        else:
            raise ValueError('unknown token %d at %d' % (tok, pos - 2))
        out.append('\n' if tok in (10, 11) else ' ')
    return ''.join(out)


def convert(path):
    data = open(path, 'rb').read()
    if data[:4] != b'xof ' or data[8:12] == b'txt ':
        return False
    fmt = data[8:12]
    float_size = int(data[12:16])
    if fmt == b'bzip':
        body = decompress(data)
    elif fmt == b'bin ':
        body = data[16:]
    else:
        raise ValueError('unsupported .x format %r' % fmt)
    text = 'xof 0303txt 0032\n' + to_text(body, float_size)
    with open(path, 'w', newline='\n') as f:
        f.write(text)
    return True


def main():
    root = sys.argv[1]
    n = 0
    for dirpath, _, files in os.walk(root):
        for name in files:
            if name.lower().endswith('.x'):
                p = os.path.join(dirpath, name)
                if convert(p):
                    print('convert_x: converted', p)
                    n += 1
    print('convert_x: %d binary .x files converted' % n)


if __name__ == '__main__':
    main()
