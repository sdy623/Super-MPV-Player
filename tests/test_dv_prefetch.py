from pathlib import Path
import subprocess,sys
w=Path(__file__).parent.parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
s=(w/'core/mpv/video/filter/vf_windows_dv.c').read_text();s=s[s.index('static void process(struct mp_filter *f)'):s.index('static void reset(struct mp_filter *f)')]
head=r"""#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#define BATCH 6
#define READY_CAP 12
#define MPMIN(a,b) ((a)<(b)?(a):(b))
#define MP_NOPTS_VALUE -1
#define MP_FRAME_VIDEO 1
#define MP_FRAME_EOF 2
#define PL_COLOR_TRC_HLG 1
#define MAKE_FRAME(t,p) ((struct mp_frame){t,p})
#define MP_EOF_FRAME ((struct mp_frame){MP_FRAME_EOF,0})
#define INFINITE -1
struct mp_image {struct {int transfer_orig;} params;};
struct mp_frame {int type;void*data;};
struct priv {int ready_count,ready_pos,job_count,count,done,processed_frames;bool job_gpu,job_pending,eof;void*worker,*gpu_session,*hlg_session;struct mp_image*ready[12],*pending[6],*seek_tail;double seek_pts;};
struct mp_filter {struct priv*priv;void*ppins[2];};
static int jobs,available,got,eof_at;
static long InterlockedCompareExchange(int*p,int a,int b){return *p;}
static void WaitForSingleObject(void*p,int n){} static void CloseHandle(void*p){}
static void finish_batch(struct mp_filter*f){}
static bool mp_pin_in_needs_data(void*p){return false;}
static void mp_pin_in_write(void*p,struct mp_frame f){}
static bool mp_pin_out_request_data(void*p){return got<available;}
static struct mp_frame mp_pin_out_read(void*p){static struct mp_image img;got++;return (struct mp_frame){eof_at&&got==eof_at?MP_FRAME_EOF:MP_FRAME_VIDEO,&img};}
static void mp_frame_unref(struct mp_frame*f){}
static bool hold_seek_preroll(struct priv*p,struct mp_image*i){return false;}
static void begin_batch(struct mp_filter*f){jobs++;assert(f->priv->count+f->priv->ready_count-f->priv->ready_pos<=READY_CAP);}
"""
body=r"""
int main(){
 for(int buffered=0;buffered<=12;buffered++){
  struct priv p={.ready_count=buffered,.gpu_session=(void*)1,.job_gpu=true,.processed_frames=1};struct mp_filter f={.priv=&p};jobs=got=eof_at=0;available=100;process(&f);
  assert(jobs==(buffered<12));if(jobs)assert(p.count==1);
 }
 // A re-prime/format transition may request a full batch, but never exceed free capacity.
 for(int buffered=0;buffered<=12;buffered++){
  struct priv p={.ready_count=buffered,.gpu_session=(void*)1,.job_gpu=true};struct mp_filter f={.priv=&p};jobs=got=eof_at=0;available=100;process(&f);
  assert(jobs==(buffered<12));if(jobs)assert(p.count==MPMIN(6,12-buffered));
 }
 struct priv p={.ready_count=11,.gpu_session=(void*)1,.job_gpu=true};struct mp_filter f={.priv=&p};jobs=got=0;available=1;eof_at=1;process(&f);assert(p.eof&&!jobs);
 return 0;
}
"""
(out/'test-process.c').write_text(head+s+body)
gcc='gcc'
subprocess.run([str(gcc),'-std=c11',str(out/'test-process.c'),'-o',str(out/'test-process.exe')],check=True)
r=subprocess.run([str(out/'test-process.exe')]);sys.exit(r.returncode)
