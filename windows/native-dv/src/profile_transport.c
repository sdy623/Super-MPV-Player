/* Raw P5 transport; LGPL-2.1-or-later. No profile conversion or pixel reshaping. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <libdovi/rpu_parser.h>
__declspec(dllexport) int wdv_profile_v11(const uint8_t*data,size_t len,int escaped){
 if(!data||len<25||len>65536)return 0;
 DoviRpuOpaque*r=escaped?dovi_parse_unspec62_nalu(data,len):dovi_parse_rpu(data,len);
 if(!r)return 0;
 const DoviRpuDataHeader*h=dovi_rpu_get_header(r);int profile=h?h->guessed_profile:0;
 if(h)dovi_rpu_free_header(h);dovi_rpu_free(r);return profile;
}
__declspec(dllexport) int wdv_forward_p5_v11(const uint8_t*data,size_t len,int escaped,uint8_t*out,size_t cap,uint8_t*profile){
 if(!data||!out||len<25||len>65536||cap>65536)return -1;
 DoviRpuOpaque*r=escaped?dovi_parse_unspec62_nalu(data,len):dovi_parse_rpu(data,len);
 if(!r)return -3;
 const DoviRpuDataHeader*h=dovi_rpu_get_header(r);int valid=h&&h->guessed_profile==5;
 if(h)dovi_rpu_free_header(h);
 const DoviData*d=valid?dovi_write_rpu(r):NULL;int result=-3;
 if(d&&d->len+2<=cap){out[0]=0x7c;out[1]=1;memcpy(out+2,d->data,d->len);if(profile)*profile=5;result=(int)d->len+2;}
 if(d)dovi_data_free(d);dovi_rpu_free(r);return result;
}
