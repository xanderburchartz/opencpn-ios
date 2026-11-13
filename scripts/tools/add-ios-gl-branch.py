"""Voegt in OpenCPN-bronbestanden een __OCPN__IOS__-tak toe na elke Android-GL-includetak.

Zoekt blokken die beginnen met `#if defined(__OCPN__ANDROID__)`, `#if defined(__ANDROID__)`,
`#ifdef __OCPN__ANDROID__` of `#ifdef __ANDROID__` en die qopengl.h of GLES2/gl2.h includen. Voor de eerstvolgende
`#elif`/`#else` op hetzelfde niveau komt:
    #elif defined(__OCPN__IOS__)  // opencpn-ios
     #include <qopengl.h>
     #include <GL/gl_private.h>
Idempotent: slaat blokken over die al een __OCPN__IOS__-tak hebben.
Gebruik: python3 -I add-ios-gl-branch.py <bestand> ...   (drukt per bestand af wat er gebeurde)
"""
import re
import sys

START = re.compile(r'^\s*#\s*(if\s+defined\s*\(\s*__(OCPN__)?ANDROID__\s*\)|ifdef\s+__(OCPN__)?ANDROID__)\s*(//.*)?$')
IF = re.compile(r'^\s*#\s*if')
ELSE = re.compile(r'^\s*#\s*(elif|else)\b')
ENDIF = re.compile(r'^\s*#\s*endif\b')
BRANCH = ['#elif defined(__OCPN__IOS__)  // opencpn-ios: GL via Qt (OpenGL ES)\n',
          ' #include <qopengl.h>\n',
          ' #include <GL/gl_private.h>\n']

def patch(path):
    lines = open(path, encoding='utf-8', errors='surrogateescape').readlines()
    out, i, n = [], 0, 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        if START.match(line):
            depth, j, block, end = 0, i + 1, [], None
            while j < len(lines):
                l = lines[j]
                if IF.match(l) and not ELSE.match(l):
                    depth += 1
                elif ENDIF.match(l):
                    if depth == 0:
                        end = j
                        break
                    depth -= 1
                elif ELSE.match(l) and depth == 0:
                    end = j
                    break
                block.append(l)
                j += 1
            text = ''.join(block)
            already = any('__OCPN__IOS__' in l for l in lines[j:j + 2])
            if end is not None and ('qopengl.h' in text or 'GLES2/gl2.h' in text) and not already:
                out.extend(block)
                if ENDIF.match(lines[end]):
                    out.extend(BRANCH)
                else:
                    out.extend(BRANCH)
                i = end
                n += 1
                continue
        i += 1
    if n:
        open(path, 'w', encoding='utf-8', errors='surrogateescape').writelines(out)
    print(f'{n} tak(ken) toegevoegd: {path}')

for p in sys.argv[1:]:
    patch(p)
