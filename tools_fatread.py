import struct, sys, os
f = open(sys.argv[1],'rb'); b = f.read(512)
bps = struct.unpack('<H',b[11:13])[0]; spc = b[13]
rsvd = struct.unpack('<H',b[14:16])[0]; nfat = b[16]
rootent = struct.unpack('<H',b[17:19])[0]; spf = struct.unpack('<H',b[22:24])[0]
fat_off  = rsvd*bps
root_off = fat_off + nfat*spf*bps
data_off = root_off + rootent*32
f.seek(fat_off); fat = f.read(spf*bps)
def chain(c):
    out=[]
    while 2 <= c < 0xFFF8:
        out.append(c); c = struct.unpack('<H', fat[c*2:c*2+2])[0]
    return out
def read(c, size=None):
    d=b''
    for cl in chain(c):
        f.seek(data_off + (cl-2)*spc*bps); d += f.read(spc*bps)
    return d[:size] if size else d
def entries(data):
    out=[]; lfn=''
    for i in range(0,len(data),32):
        e=data[i:i+32]
        if e[0]==0: break
        if e[0]==0xE5: lfn=''; continue
        if e[11]==0x0F:
            part=b''.join([e[1:11],e[14:26],e[28:32]]).decode('utf-16-le','replace')
            lfn=part.split('￿')[0].split('\x00')[0]+lfn; continue
        name=lfn if lfn else (e[0:8].decode('ascii','replace').strip()+
              ('.'+e[8:11].decode('ascii','replace').strip() if e[8:11].strip() else ''))
        lfn=''
        out.append((name, e[11], struct.unpack('<H',e[26:28])[0], struct.unpack('<I',e[28:32])[0]))
    return out
def walk(data, path, depth=0):
    for name,attr,clu,size in entries(data):
        if name in ('.','..'): continue
        if attr & 0x08 and not attr & 0x10: continue
        if attr & 0x10:
            print('  '*depth + f'[{name}]/')
            walk(read(clu), path+'/'+name, depth+1)
        else:
            print('  '*depth + f'{name:<24}{size:>9}')
            if len(sys.argv)>2:
                d=os.path.join(sys.argv[2], path.lstrip('/')); os.makedirs(d,exist_ok=True)
                open(os.path.join(d,name),'wb').write(read(clu,size))
f.seek(root_off); walk(f.read(rootent*32), '')
