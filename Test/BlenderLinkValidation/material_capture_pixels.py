"""Decode the material suite's PNG and uncompressed DXGI captures without applying tone mapping."""
import numpy as np
from PIL import Image


def LoadFrame(path, manifest):
    width,height=int(manifest['width']),int(manifest['height'])
    if path.suffix.lower()=='.png':
        data=np.asarray(Image.open(path).convert('RGB'),dtype=np.float32)/255
        if data.shape[:2]!=(height,width):
            raise ValueError('PNG dimensions disagree with capture manifest')
        return data
    raw=path.read_bytes()
    if raw[:4]!=b'DDS ':
        raise ValueError('Invalid DDS header')
    fmt=int(manifest['format'])
    if fmt==26:
        values=np.frombuffer(raw,dtype='<u4',offset=len(raw)-width*height*4).reshape(height,width)
        channels=[]
        for shift,bits in ((0,6),(11,6),(22,5)):
            packed=(values>>shift)&((1<<(bits+5))-1)
            mantissa,exponent=packed&((1<<bits)-1),packed>>bits
            channel=np.where(exponent==0,mantissa*2.0**(-14-bits),
                             (1+mantissa/2.0**bits)*2.0**(exponent.astype(float)-15))
            channel=np.where(exponent==31,np.inf,channel)
            channels.append(channel)
        return np.stack(channels,axis=-1).astype(np.float32)
    if fmt in (2,10,28,29,87):
        dtype,size=('<f4',16) if fmt==2 else ('<f2',8) if fmt==10 else ('u1',4)
        data=np.frombuffer(raw,dtype=dtype,offset=len(raw)-width*height*size).reshape(height,width,4).astype(np.float32)
        if fmt in (28,29,87):
            data/=255
        return data[...,[2,1,0,3]] if fmt==87 else data
    raise ValueError(f'Unsupported material capture DXGI format {fmt}')
