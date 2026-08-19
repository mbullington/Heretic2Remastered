#!/usr/bin/env python3
"""PPM(P6) -> PNG converter + side-by-side + amplified diff, no numpy/PIL."""
import struct, zlib, sys

def read_ppm(path):
    b = open(path, 'rb').read()
    parts = b.split(None, 4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4]

def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)

def x3(rgb, w, h):
    return b''.join(bytes((min(255, rgb[i]*3), min(255, rgb[i+1]*3), min(255, rgb[i+2]*3)))
                    for i in range(0, w*h*3, 3))

def diff_img(a, b, w, h, scale=16.0):
    out = bytearray(w*h*3)
    for i in range(0, w*h*3, 3):
        d = max(abs(a[i]-b[i]), abs(a[i+1]-b[i+1]), abs(a[i+2]-b[i+2]))
        v = min(255, int(d*scale))
        out[i], out[i+1], out[i+2] = v, v, v
    return bytes(out)

def side_by_side(a, b, w, h):
    out = bytearray(w*2*h*3)
    for y in range(h):
        row = y*2*w*3
        out[row:row+w*3] = a[y*w*3:(y+1)*w*3]
        out[row+w*3:row+2*w*3] = b[y*w*3:(y+1)*w*3]
    return bytes(out)

if __name__ == '__main__':
    # usage: ppm2png.py MODE out.png a.ppm [b.ppm]
    mode, outp = sys.argv[1], sys.argv[2]
    w, h, a = read_ppm(sys.argv[3])
    if mode == 'one':
        write_png(outp, w, h, a)
    elif mode == 'pair':
        _, _, b = read_ppm(sys.argv[4])
        write_png(outp, w*2, h, side_by_side(a, b, w, h))
    elif mode == 'pair3':
        _, _, b = read_ppm(sys.argv[4])
        write_png(outp, w, h, b)
    elif mode == 'diff':
        scale = float(sys.argv[5]) if len(sys.argv) > 5 else 16.0
        _, _, b = read_ppm(sys.argv[4])
        write_png(outp, w, h, diff_img(x3(a, w, h), x3(b, w, h), w, h, scale))
    print("wrote", outp)
