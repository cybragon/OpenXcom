import re
src=open('/workspace/oxce/src/src/Engine/Scalers/xbrz.cpp').read()
types=['blendLineShallow','blendLineSteep','blendLineSteepAndShallow','blendLineDiagonal','blendCorner']
ops=[]; rng=[]
for k in range(2,7):
    a=src.index('struct Scaler%dx'%k); b=src.index('struct Scaler%dx'%(k+1)) if k<6 else src.index('struct ColorDistanceRGB')
    body=src[a:b]
    for t in types:
        m=re.search(r'static void %s\(.*?\n    \{(.*?)\n    \}'%t, body, re.S)
        blk=m.group(1)
        start=len(ops)
        for line in blk.split('\n'):
            line=line.split('//')[0].strip()
            if not line: continue
            m1=re.match(r'alphaGrad<\s*(\d+),\s*(\d+)>\(out\.template ref<([^,]+),([^>]+)>\(\), col\);',line)
            m2=re.match(r'out\.template ref<([^,]+),([^>]+)>\(\) = col;',line)
            sc=k
            if m1:
                M,N=int(m1.group(1)),int(m1.group(2)); I=eval(m1.group(3).replace('scale',str(sc))); J=eval(m1.group(4).replace('scale',str(sc)))
            elif m2:
                M,N=0,0; I=eval(m2.group(1).replace('scale',str(sc))); J=eval(m2.group(2).replace('scale',str(sc)))
            else:
                raise Exception('unparsed: '+line)
            ops.append((I,J,M,N))
        rng.append((start,len(ops)-start))
print(len(ops),'ops')
o=',\n'.join('\tivec4(%d,%d,%d,%d)'%x for x in ops)
r=', '.join('ivec2(%d,%d)'%x for x in rng)
open('ops.glsl','w').write('const ivec4 OPS[%d] = ivec4[%d](\n%s);\nconst ivec2 OPR[%d] = ivec2[%d](%s);\n'%(len(ops),len(ops),o,len(rng),len(rng),r))
