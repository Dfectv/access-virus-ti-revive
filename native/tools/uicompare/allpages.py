import st,subprocess,struct,time,os
from PIL import ImageGrab, Image
S=os.path.dirname(os.path.abspath(__file__))
tabs=[('easy',244),('osc',370),('filter',433),('lfo',496),('matrix',559),('arp',622),('fx1',685),('fx2',748),('common',811)]
comp,rest=st.load(os.path.expanduser('~/')+'.wine/drive_c/base.state')
open(os.path.expanduser('~/')+'.wine/drive_c/x.state','wb').write(b'VTISTATE'+struct.pack('<I',len(comp))+comp+rest)
open(S+'/base.comp','wb').write(comp)
env=dict(os.environ,DISPLAY=':77',VTI_LOADSTATE='C:\\x.state',VTI_EDITOR='C:\\shot.bmp')
if os.environ.get("ORIG"): p=subprocess.Popen(['wine','C:\\vtihost.exe','C:\\Program Files\\Common Files\\VST3\\Virus TI (TI Revive).vst3','45','C:\\out.wav'],cwd=os.path.expanduser('~/')+'.wine/drive_c',env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
if os.environ.get("ORIG"): time.sleep(14)
if os.environ.get("ORIG"):
  for name,x in tabs:
    subprocess.run([os.path.expanduser('~/')+'virus-ti-native/work/bin/xclick',str(x+4),'54','1'],env=env); time.sleep(2.5)
    ImageGrab.grab(xdisplay=':77').crop((4,30,911,543)).save(S+'/o-%s.png'%name)
  p.wait(timeout=90)
args=[]
for i,(name,x) in enumerate(tabs):
    args += ['--click','%d,24@%.1f'%(x,2+i*1.2),'--shot','u-%s.ppm@%.1f'%(name,2.6+i*1.2)]
subprocess.run(['bwrap','--dev-bind','/','/','--tmpfs','/tmp','--bind','/tmp/.X11-unix','/tmp/.X11-unix','--bind','/tmp/claude-1000','/tmp/claude-1000','bash','-c',
  'cd %s && DISPLAY=:77 timeout 60 ~/virus-ti-native/build/bin/Release/nativehost ~/virus-ti-native/build/VST3/Release/TI\ Revive\ Virus\ Control.vst3 --state base.comp --editor %s --seconds %d'%(S,' '.join(args),4+len(tabs)*1.2)],capture_output=True)
for name,x in tabs:
    o=Image.open(S+'/o-%s.png'%name).convert('RGB'); u=Image.open(S+'/u-%s.ppm'%name).convert('RGB')
    # ignore the part strip column + LCD (sync text) - compare the page area
    box=(192,40,907,465)
    d=[(x,y) for x in range(box[0],box[2],1) for y in range(box[1],box[3]) if max(abs(a-b) for a,b in zip(o.getpixel((x,y)),u.getpixel((x,y))))>24]
    tot=(box[2]-box[0])*(box[3]-box[1])
    print('%-7s %5.2f %% clearly different'%(name,100*len(d)/tot), (min(p[0] for p in d),min(p[1] for p in d),max(p[0] for p in d),max(p[1] for p in d)) if d else '')
