#!/usr/bin/env python3
"""Mede as armas do kit (.glb): perfil de altura ao longo do cano e o desvio lateral da linha da mira em
relação ao centro da caixa (usado em createWeapon, c.sight.x). Uso: python3 tools/medir_mira_armas.py shaders/kit/armas/*.glb"""
import json, struct, sys, numpy as np
def load(p):
    d=open(p,'rb').read()
    l=struct.unpack('<I',d[12:16])[0]; js=json.loads(d[20:20+l]); off=20+l
    bl=struct.unpack('<I',d[off:off+4])[0]; bin_=d[off+8:off+8+bl]
    def acc(i):
        a=js['accessors'][i]; bv=js['bufferViews'][a['bufferView']]
        o=bv.get('byteOffset',0)+a.get('byteOffset',0); n=a['count']
        st=bv.get('byteStride',12)
        return np.array([struct.unpack_from('<3f',bin_,o+k*st) for k in range(n)])
    # node transforms
    def mat(n):
        M=np.eye(4)
        if 'matrix' in n: M=np.array(n['matrix']).reshape(4,4).T
        else:
            T=np.eye(4); T[:3,3]=n.get('translation',[0,0,0])
            x,y,z,w=n.get('rotation',[0,0,0,1])
            R=np.eye(4); R[:3,:3]=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
            S=np.diag(list(n.get('scale',[1,1,1]))+[1]); M=T@R@S
        return M
    pts=[]
    def walk(i,P):
        n=js['nodes'][i]; M=P@mat(n)
        if 'mesh' in n:
            for pr in js['meshes'][n['mesh']]['primitives']:
                v=acc(pr['attributes']['POSITION']); v=(M@np.c_[v,np.ones(len(v))].T).T[:,:3]; pts.append(v)
        for c in n.get('children',[]): walk(c,M)
    for r in js['scenes'][js.get('scene',0)]['nodes']: walk(r,np.eye(4))
    return np.concatenate(pts)
for p in sys.argv[1:]:
    v=load(p); mn,mx=v.min(0),v.max(0); ext=mx-mn
    print(p, 'ext',np.round(ext,3))
    ax=int(np.argmax(ext)); up=[i for i in range(3) if i!=ax]
    # assume vertical = the one with larger extent among remaining? print profile for both
    for u in up:
        s=''
        for k in range(10):
            a=mn[ax]+ext[ax]*k/10; b=a+ext[ax]/10
            m=(v[:,ax]>=a)&(v[:,ax]<b)
            s+=' %.2f'%((v[m,u].max()-mn[u])/ext[u]) if m.any() else '  -- '
        print('  eixo',ax,'perfil alto no eixo',u,':',s)

print('--- linha da mira (x dos pontos mais altos) ---')
for p in sys.argv[1:]:
    v=load(p); mn,mx=v.min(0),v.max(0); ext=mx-mn
    top=v[v[:,1]>mx[1]-ext[1]*0.04]
    s=0.95/ext[2] if 'm4colt' not in p else 0.95/ext[2]
    print(p,'centro x %.4f · alto x %.4f · desvio %.1f mm'%((mn[0]+mx[0])/2, np.median(top[:,0]), (np.median(top[:,0])-(mn[0]+mx[0])/2)*s*1000))
