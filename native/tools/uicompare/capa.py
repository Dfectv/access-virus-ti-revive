import st,subprocess,struct,sys,time,os
from PIL import ImageGrab
S=os.path.dirname(os.path.abspath(__file__))
def run(tag, sets, extra_recs=None, tab=(626,54)):
    comp,rest=st.load(os.path.expanduser('~/')+'.wine/drive_c/base.state'); recs,tail=st.records(comp)
    open(S+'/p0.syx','wb').write(recs[2])
    subprocess.run([os.path.expanduser('~/')+'virus-ti-native/build/bin/Release/vtimodel','setdump',S+'/p0.syx',S+'/p0x.syx']+sets,check=True,capture_output=True)
    recs[2]=open(S+'/p0x.syx','rb').read()
    if extra_recs: recs += extra_recs
    c=st.build(recs,tail)
    open(os.path.expanduser('~/')+'.wine/drive_c/x.state','wb').write(b'VTISTATE'+struct.pack('<I',len(c))+c+rest)
    open(S+'/x.comp','wb').write(c)
    env=dict(os.environ,DISPLAY=':77',VTI_LOADSTATE='C:\\x.state',VTI_EDITOR='C:\\shot.bmp')
    p=subprocess.Popen(['wine','C:\\vtihost.exe','C:\\Program Files\\Common Files\\VST3\\Virus TI (TI Revive).vst3','22','C:\\out.wav'],cwd=os.path.expanduser('~/')+'.wine/drive_c',env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    time.sleep(14)
    subprocess.run([os.path.expanduser('~/')+'virus-ti-native/work/bin/xclick',str(tab[0]),str(tab[1]),'1'],env=env)
    time.sleep(3)
    ImageGrab.grab(xdisplay=':77').crop((4,30,911,543)).save(S+'/orig-%s.png'%tag)
    p.wait(timeout=60)
if __name__=='__main__':
    run('arp1',['Arpeggiator/Mode=1','Arpeggiator/Pattern=0'])
