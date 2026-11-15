# Lists LIGHTS objects with SECTR1/SECTR2 in an S-57 .000 file (minimal ISO 8211 reader).
import sys, struct
def records(data):
    p = 0
    while p + 24 <= len(data):
        L = int(data[p:p+5]); lead = data[p:p+24]
        base = int(lead[12:17]); sl, sp, st = int(lead[20:21]), int(lead[21:22]), int(lead[23:24])
        d = p + 24; fields = []
        while data[d] != 0x1e:
            tag = data[d:d+st].decode(); ln = int(data[d+st:d+st+sl]); off = int(data[d+st+sl:d+st+sl+sp])
            fields.append((tag, data[p+base+off:p+base+off+ln])); d += st+sl+sp
        yield fields; p += L
def main(fn):
    data = open(fn, 'rb').read(); recs = list(records(data))[1:]
    comf = 10000000; nodes = {}; lights = []; global NL; NL = [0]
    for f in recs:
        tags = dict(f)
        if 'DSPM' in tags: comf = struct.unpack('<I', tags['DSPM'][27:31])[0] if len(tags['DSPM'])>=31 else comf
        if 'VRID' in tags:
            v = tags['VRID']; key = (v[0], struct.unpack('<I', v[1:5])[0])
            if 'SG2D' in tags:
                y, x = struct.unpack('<ii', tags['SG2D'][:8]); nodes[key] = (y, x)
        if 'FRID' in tags:
            objl = struct.unpack('<H', tags['FRID'][7:9])[0]
            if objl != 75: continue
            NL[0] += 1
            att = {}
            a = tags.get('ATTF', b'')[:-1]; i = 0
            while i + 2 <= len(a):
                code = struct.unpack('<H', a[i:i+2])[0]; j = a.index(b'\x1f', i+2)
                att[code] = a[i+2:j].decode('latin1'); i = j + 1
            if 136 in att:
                sp = tags.get('FSPT', b'')[:5]
                lights.append((sp[0], struct.unpack('<I', sp[1:5])[0], att.get(136), att.get(137)))
    for rcnm, rcid, s1, s2 in lights:
        y, x = nodes.get((rcnm, rcid), (0, 0))
        print(f"{fn.split('/')[-1]} {y/comf:.5f} {x/comf:.5f} sector {s1}-{s2}")
main(sys.argv[1]); print("LIGHTS", NL[0], file=sys.stderr)
