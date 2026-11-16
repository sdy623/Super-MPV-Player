from pathlib import Path
import re,subprocess,sys
w=Path(__file__).parent.parent;o=Path(sys.argv[1]);o.mkdir(parents=True,exist_ok=True);s=(w/'core/mpv/video/filter/vf_windows_dv.c').read_text()
m=re.search(r'static bool hold_seek_preroll\(.*?\n}\n',s,re.S)
assert m, 'missing pre-render seek-preroll guard; old bridge renders all GOP pre-roll'
c='''#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#define MP_NOPTS_VALUE (-1e20)
struct mp_image {double pts;int rpu,el;};
struct priv {double seek_pts;struct mp_image *seek_tail;unsigned long long seek_skipped;};
static int freed;
void mp_image_unrefp(struct mp_image **p){if(*p){free(*p);*p=0;freed++;}}
static struct mp_image* frame(double t){struct mp_image*p=malloc(sizeof(*p));*p=(struct mp_image){t,7,1};return p;}
'''+m.group()+'''
int main(){
struct priv p={.seek_pts=74.995};
for(int i=0;i<240;i++)assert(hold_seek_preroll(&p,frame(71+i/60.0)));
assert(p.seek_skipped==240 && p.seek_tail && freed==239);
struct mp_image*t=frame(75);assert(!hold_seek_preroll(&p,t));
assert(!p.seek_tail && p.seek_pts==MP_NOPTS_VALUE && t->rpu==7 && t->el==1 && freed==240);mp_image_unrefp(&t);
p.seek_pts=100;assert(hold_seek_preroll(&p,frame(99)));assert(p.seek_tail->pts==99);mp_image_unrefp(&p.seek_tail);
p.seek_pts=10;t=frame(MP_NOPTS_VALUE);assert(!hold_seek_preroll(&p,t));assert(p.seek_pts==MP_NOPTS_VALUE);mp_image_unrefp(&t);
p.seek_pts=MP_NOPTS_VALUE;t=frame(1);assert(!hold_seek_preroll(&p,t));mp_image_unrefp(&t);
}
'''
p=o/'test-preroll.c';p.write_text(c);subprocess.run(['gcc',str(p),'-o',str(o/'test-preroll.exe')],check=True);subprocess.run([str(o/'test-preroll.exe')],check=True)
play=(w/'core/mpv/player/playloop.c').read_text();assert 'hrseek_framedrop && mpctx->play_dir > 0' in play
assert 'mp_image_unrefp(&p->seek_tail)' in s[s.index('static void reset('):]
assert 'p->pending[p->count++]=p->seek_tail' in s
print('PASS: bounded pre-roll retention, original RPU/EL, missing PTS, disabled policy, reset and EOF tail')
