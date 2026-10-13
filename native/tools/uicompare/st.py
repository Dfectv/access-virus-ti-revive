import struct,sys
def load(p):
    b=open(p,'rb').read(); assert b[:8]==b'VTISTATE'
    n=struct.unpack('<I',b[8:12])[0]; comp=b[12:12+n]; rest=b[12+n:]
    return comp,rest
def records(comp):
    assert comp[:4]==b'MIDI'; L=struct.unpack('>I',comp[4:8])[0]; i=8; out=[]
    while i<8+L and i<len(comp):
        l=struct.unpack('>I',comp[i:i+4])[0]; out.append(comp[i+4:i+4+l]); i+=4+l
    return out, comp[8+L:] if 8+L<len(comp) else b''
def build(recs, tail):
    body=b''.join(struct.pack('>I',len(r))+r for r in recs)
    return b'MIDI'+struct.pack('>I',len(body))+body+tail
if __name__=='__main__':
    comp,rest=load(sys.argv[1]); recs,tail=records(comp)
    print(len(comp),len(rest),len(recs),len(tail))
    for i,r in enumerate(recs[:20]): print(i,len(r),r[:10].hex())
