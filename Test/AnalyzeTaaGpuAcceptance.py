import json,struct,csv,sys
from pathlib import Path
import numpy as np
from PIL import Image
root=Path(sys.argv[1])
pairs={}
observations={int(r['index']):r for r in csv.DictReader((root/'observations.csv').open())}
def Decode(path,fmt,w,h):
 b=path.read_bytes()
 if fmt==26:
  packed=np.frombuffer(b,dtype='<u4',offset=len(b)-w*h*4).reshape(h,w)
  out=[]
  for shift,bits in [(0,6),(11,6),(22,5)]:
   v=(packed>>shift)&((1<<(bits+5))-1)
   m=v&((1<<bits)-1)
   e=v>>bits
   out.append(np.where(e==0,m*2.**(-14-bits),(1.+m/2.**bits)*2.**(e.astype(float)-15)))
  return np.stack(out,axis=-1)
 if fmt==10:return np.frombuffer(b,dtype='<f2',offset=len(b)-w*h*8).reshape(h,w,4)[...,:3].astype(float)
 raise ValueError(fmt)
for mp in root.rglob('capture.json'):
 m=json.loads(mp.read_text())
 assert m['state']=='completed' and len(m['frames'])==1
 f=m['frames'][0]
 p=mp.parent/f['file']
 index=int(p.name.split('_')[0])
 kind='resolved' if '_resolved_' in p.name else 'current'
 arr=Decode(p,m['format'],m['width'],m['height'])
 assert np.isfinite(arr).all()
 assert f['engine_frame']==int(observations[index]['engine_frame'])
 pairs.setdefault(index,{})[kind]=arr
results={}
invalid=['first','toggle','cut','gap_restart','resize','resize_back','fov','rotation']
for i,p in sorted(pairs.items()):
 a=p['current']
 b=p['resolved']
 d=b-a
 phase=observations[i]['phase']
 results[i]={'phase':phase,'max_rgb_error':float(abs(d).max()),
  'rms_rgb_error':float(np.sqrt(np.mean(d*d))),'identical':bool(np.array_equal(a,b))}
 if phase in invalid or phase.startswith('factor0'):
  results[i]['current_only_pass']=bool(np.array_equal(a,b))
 for kind in ['current','resolved']:
  if i in [0,1,32,64,65,66,70,78]:
   Image.fromarray((np.clip(p[kind],0,1)*255).astype('uint8')).save(root/f'{i}_{phase}_{kind}.png')
ids=list(range(1,65))
frames=[int(observations[i]['engine_frame']) for i in ids]
edge_mask=abs(pairs[0]['current'][...,0]-.45)<.36
cur=np.stack([pairs[i]['current'] for i in ids])
res=np.stack([pairs[i]['resolved'] for i in ids])

input_errors=[]
for i,pair in sorted(pairs.items()):
 row=observations[i]
 h,w=pair['current'].shape[:2]
 y,x=np.mgrid[:h,:w]
 edge=(x+.5-float(row['jitter_x'])*w)-.61*(y+.5-float(row['jitter_y'])*h)-w*.27
 expected=np.clip(edge+.5,0,1)*.7+.1
 if i>=65:expected=1-expected
 expected=np.floor(expected*255)/255
 input_errors.append(float(np.max(np.abs(pair['current']-expected[...,None]))))
summary={'input_fixture_max_error':max(input_errors),'input_fixture_valid':max(input_errors)<.016,
 'pairs':len(pairs),'frames_contiguous_static':bool(np.all(np.diff(frames)==1)),
 'unique_jitters_static':len(set((observations[i]['jitter_x'],observations[i]['jitter_y']) for i in ids)),
 'invalid_current_only':all(results[i]['identical'] for i in results if results[i]['phase'] in invalid),
 'factor0_current_only':all(results[i]['identical'] for i in results if results[i]['phase'].startswith('factor0')),
 'static_current_adjacent_rms':float(np.sqrt(np.mean(np.diff(cur,axis=0)**2))),
 'static_resolved_adjacent_rms':float(np.sqrt(np.mean(np.diff(res,axis=0)**2))),
 'static_current_mean':float(cur.mean()),'static_resolved_mean':float(res.mean()),'cases':results}
(root/'analysis.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='cases'},indent=2))
print('failed current-only cases:', [i for i,v in results.items() if v.get('current_only_pass') is False])

# Compare production GPU resolves against both jitter signs using the previous GPU history.
# This reproduces the existing filter/clamp only as an independent numerical oracle.
def Bilinear(a,u,v):
 h,w=a.shape[:2]
 xx=u*w-.5
 yy=v*h-.5
 ix=np.floor(xx).astype(int)
 iy=np.floor(yy).astype(int)
 fx=xx-ix
 fy=yy-iy
 def Tex(dx,dy):return a[np.clip(iy+dy,0,h-1),np.clip(ix+dx,0,w-1)]
 return ((Tex(0,0)*(1-fx)[...,None]+Tex(1,0)*fx[...,None])*(1-fy)[...,None]+
         (Tex(0,1)*(1-fx)[...,None]+Tex(1,1)*fx[...,None])*fy[...,None])
def Bicubic(a,u,v):
 h,w=a.shape[:2]
 sx=u*w
 sy=v*h
 tx=np.floor(sx-.5)+.5
 ty=np.floor(sy-.5)+.5
 def Weights(f):
  return [-.5*f**3+f**2-.5*f,1.5*f**3-2.5*f**2+1,-1.5*f**3+2*f**2+.5*f,.5*f**3-.5*f**2]
 wx=Weights(sx-tx)
 wy=Weights(sy-ty)
 x12=wx[1]+wx[2]
 y12=wy[1]+wy[2]
 cx=(tx+wx[2]/x12)/w
 cy=(ty+wy[2]/y12)/h
 terms=[(cx,(ty-1)/h,x12*wy[0]),((tx-1)/w,cy,wx[0]*y12),(cx,cy,x12*y12),((tx+2)/w,cy,wx[3]*y12),(cx,(ty+2)/h,x12*wy[3])]
 total=sum(weight for _,_,weight in terms)
 return sum(Bilinear(a,uu,vv)*weight[...,None] for uu,vv,weight in terms)/total[...,None]
def Mitchell(x):
 a=.3333
 return (a+2)*x**3-(a+3)*x**2+1 if x<1 else a*x**3-5*a*x**2+8*a*x-4*a
correct=[]
opposite=[]
weight_sum=0
for i in range(1,65):
 a=pairs[i]['current']
 hist=pairs[i-1]['resolved']
 actual=pairs[i]['resolved']
 h,w=a.shape[:2]
 y,x=np.mgrid[:h,:w]
 u=(x+.5)/w
 v=(y+.5)/h
 samples=[]
 filtered=np.zeros_like(a)
 weight_sum=0
 for dy in [-1,0,1]:
  for dx in [-1,0,1]:
   c=a[np.clip(y+dy,0,h-1),np.clip(x+dx,0,w-1)]
   samples.append(c)
   weight=Mitchell(np.hypot(dx,dy))
   weight_sum+=weight
   filtered+=c*weight
 samples=np.stack(samples)
 filtered /= weight_sum
 mean=samples.mean(axis=0)
 std=np.sqrt(np.abs((samples*samples).mean(axis=0)-mean*mean))
 low=np.maximum(samples.min(axis=0),mean-std)
 high=np.minimum(samples.max(axis=0),mean+std)
 center=.5*(low+high)
 extent=.5*(high-low)+1e-7
 dx=float(observations[i-1]['jitter_x'])-float(observations[i]['jitter_x'])
 dy=float(observations[i-1]['jitter_y'])-float(observations[i]['jitter_y'])
 roi=(x>5)&(x<w-5)&(y>5)&(y<h-5)&(a[...,0]>.11)&(a[...,0]<.78)
 for sign,errors in [(1,correct),(-1,opposite)]:
  b=Bicubic(hist,u+dx*sign,v+dy*sign)
  uniform=np.all(samples==samples[0],axis=(0,3))
  guard_low=low.copy()
  guard_high=high.copy()
  coverage=np.zeros((h,w),dtype=int)
  for oy in range(-2,3):
   for ox in range(-2,3):
    if abs(ox)!=2 and abs(oy)!=2:continue
    c=a[np.clip(y+oy,0,h-1),np.clip(x+ox,0,w-1)]
    coverage+=np.any(c!=samples[0],axis=-1)
    guard_low=np.minimum(guard_low,c)
    guard_high=np.maximum(guard_high,c)
  expand=uniform & (coverage>0) & (coverage<=4) & np.any((b<low)|(b>high),axis=-1)
  local_low=np.where(expand[...,None],guard_low,low)
  local_high=np.where(expand[...,None],guard_high,high)
  center=.5*(local_low+local_high)
  extent=.5*(local_high-local_low)+1e-7
  delta=b-center
  scale=np.maximum(1,np.max(np.abs(delta/extent),axis=-1))
  clipped=center+delta/scale[...,None]
  predicted=filtered*.05+clipped*.95
  errors.append(float(np.mean((predicted[roi]-actual[roi])**2)))
summary['jitter_reference_correct_rms']=float(np.sqrt(np.mean(correct)))
summary['jitter_reference_opposite_rms']=float(np.sqrt(np.mean(opposite)))
summary['center_filter_weight_sum']=weight_sum
summary['jitter_reference_pass']=(summary['jitter_reference_correct_rms'] < .008 and
 summary['jitter_reference_correct_rms'] < summary['jitter_reference_opposite_rms'] * .1)
flat_errors=[]
for i in range(1,65):
 a=pairs[i]['current']
 b=pairs[i]['resolved']
 # Keep the flat-color ROI away from diagonal-edge filtering and bicubic support.
 h,w=a.shape[:2]
 base=a[5:-5,5:-5]
 flat=np.ones(base.shape[:2],dtype=bool)
 for dy in range(-5,6):
  for dx in range(-5,6):
   flat &= np.all(base == a[5+dy:h-5+dy,5+dx:w-5+dx],axis=-1)
 flat_errors.append(float(np.max(np.abs(b[5:-5,5:-5][flat]-base[flat]))))
summary['flat_region_max_error']=max(flat_errors)
summary['flat_region_pass']=summary['flat_region_max_error'] <= .015625
(root/'analysis.json').write_text(json.dumps(summary,indent=2))
print('Jitter reference:',summary['jitter_reference_correct_rms'],summary['jitter_reference_opposite_rms'],
      'filter weight sum',weight_sum)

if not (summary["input_fixture_valid"] and summary["invalid_current_only"] and summary["factor0_current_only"] and
        summary["flat_region_pass"] and summary["jitter_reference_pass"] and summary["frames_contiguous_static"] and
        summary["unique_jitters_static"] == 64 and summary["pairs"] == 85):
    sys.exit(1)
