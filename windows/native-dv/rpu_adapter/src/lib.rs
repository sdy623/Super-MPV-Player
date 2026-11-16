use dolby_vision::rpu::dovi_rpu::DoviRpu;
use dolby_vision::rpu::rpu_data_nlq::DoviELType;

fn convert(input: &[u8], escaped: bool) -> Result<(Vec<u8>,u8),String> {
    if input.len()<25 || input.len()>65536 {return Err("invalid RPU size".into());}
    let mut rpu=if escaped {DoviRpu::parse_unspec62_nalu(input)} else {DoviRpu::parse_rpu(input)}.map_err(|e|e.to_string())?;
    let source_profile=rpu.dovi_profile;
    match source_profile {
        7 if rpu.el_type==Some(DoviELType::MEL) => rpu.convert_with_mode(2u8).map_err(|e|e.to_string())?,
        8 => (),
        7 => return Err("FEL/unknown EL cannot be discarded; preserve original MPV dual-layer rendering".into()),
        _ => return Err("this adapter accepts only P7 MEL or P8 with caller-verified PQ BL".into()),
    }
    let mut nal=vec![0x7c,0x01];
    nal.extend(rpu.write_rpu().map_err(|e|e.to_string())?);
    Ok((nal,source_profile))
}

/// Versioned, bounded C ABI. Output is one de-escaped HEVC NAL62 including its
/// two-byte NAL header, for MFSampleExtension_ForwardedDecodeUnits.
/// Caller must supply valid pointer ranges and separately validate PQ BL/PTS.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn wdv_convert_rpu_v1(input:*const u8,len:usize,escaped:i32,output:*mut u8,capacity:usize,profile:*mut u8)->i32 {
    if input.is_null()||output.is_null()||len>65536||capacity>65536{return -1;}
    let result=convert(unsafe {std::slice::from_raw_parts(input,len)},escaped!=0);
    match result {
        Ok((bytes,p)) if bytes.len()<=capacity=>{
            unsafe {std::ptr::copy_nonoverlapping(bytes.as_ptr(),output,bytes.len());if !profile.is_null(){*profile=p;}}
            bytes.len() as i32
        },
        Ok(_)=>-2,
        Err(_)=>-3,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn real_fel_and_profile5_are_rejected_without_output() {
        let root=std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../dovi_tool/assets/tests");
        for name in ["fel_orig.bin", "profile5.bin"] {
            let data=std::fs::read(root.join(name)).unwrap();
            let rpu=DoviRpu::parse_unspec62_nalu(&data).unwrap();
            if name=="fel_orig.bin" {assert_eq!(rpu.el_type,Some(DoviELType::FEL));}
            let mut out=[0xA5u8;4096];let mut profile=0xFE;
            assert_eq!(unsafe {wdv_convert_rpu_v1(data.as_ptr(),data.len(),1,out.as_mut_ptr(),out.len(),&mut profile)},-3);
            assert_eq!(out,[0xA5;4096]);assert_eq!(profile,0xFE);
        }
    }
    #[test]
    #[ignore = "requires optional user-owned WDV_FIXTURE_ROOT samples"]
    fn real_f1_p81_pass_through_does_not_rewrite_metadata() {
        let root=std::path::PathBuf::from(std::env::var("WDV_FIXTURE_ROOT").unwrap());
        let data=std::fs::read(root.join("F1-P81-RPU.bin")).unwrap();
        for escaped in rpulist(&data) {
            let parsed=DoviRpu::parse_unspec62_nalu(escaped).unwrap();
            let (result,profile)=convert(escaped,true).unwrap();
            assert_eq!(profile,8);assert_eq!(&result[..2],&[0x7c,0x01]);
            assert_eq!(&result[2..],&parsed.write_rpu().unwrap());
        }
    }
    fn rpulist(data: &[u8]) -> Vec<&[u8]> {
        let marks:Vec<usize>=data.windows(4).enumerate().filter_map(|(i,b)| (b==[0,0,0,1]).then_some(i)).collect();
        marks.iter().enumerate().map(|(n,&p)| &data[p+4..marks.get(n+1).copied().unwrap_or(data.len())]).collect()
    }
    #[test]
    #[ignore = "requires optional user-owned WDV_FIXTURE_ROOT samples"]
    fn real_f1_mel_preserves_all_display_metadata_and_matches_mode2() {
        let root=std::path::PathBuf::from(std::env::var("WDV_FIXTURE_ROOT").expect("fixture root"));
        let a=std::fs::read(root.join("F1-P7-RPU.bin")).unwrap();let b=std::fs::read(root.join("F1-P81-RPU.bin")).unwrap();
        let orig=rpulist(&a);let expected=rpulist(&b);assert_eq!(orig.len(),233);assert_eq!(expected.len(),233);
        for (o,e) in orig.iter().zip(expected) {
            let parsed=DoviRpu::parse_unspec62_nalu(o).unwrap();assert_eq!(parsed.el_type,Some(DoviELType::MEL));
            let (converted,p)=convert(o,true).expect("valid MEL must convert");assert_eq!(p,7);
            let check=DoviRpu::parse_rpu(&converted).unwrap();let reference=DoviRpu::parse_unspec62_nalu(e).unwrap();
            assert_eq!(check.dovi_profile,8);assert_eq!(check.write_hevc_unspec62_nalu().unwrap(),reference.write_hevc_unspec62_nalu().unwrap());
            assert_eq!(format!("{:?}",check.vdr_dm_data),format!("{:?}",parsed.vdr_dm_data));
        }
    }
    #[test]
    fn corrupt_or_empty_metadata_is_rejected_not_relabelled() {
        assert!(convert(&[],true).is_err());assert!(convert(&[0;256],false).is_err());
    }
    #[test]
    #[ignore = "requires optional user-owned WDV_FIXTURE_ROOT samples"]
    fn decoded_raw_input_and_escaped_input_produce_same_payload() {
        let root=std::path::PathBuf::from(std::env::var("WDV_FIXTURE_ROOT").unwrap());let data=std::fs::read(root.join("F1-P7-RPU.bin")).unwrap();
        let list=rpulist(&data);let rpu=DoviRpu::parse_unspec62_nalu(list[0]).unwrap();
        assert_eq!(convert(list[0],true).unwrap(),convert(&rpu.write_rpu().unwrap(),false).unwrap());
    }
    #[test]
    #[ignore = "requires optional user-owned WDV_FIXTURE_ROOT samples"]
    fn c_abi_rejects_null_and_short_output_without_write() {
        let root=std::path::PathBuf::from(std::env::var("WDV_FIXTURE_ROOT").unwrap());let data=std::fs::read(root.join("F1-P7-RPU.bin")).unwrap();let list=rpulist(&data);
        let mut out=[0xA5u8;8];let mut p=0;
        assert_eq!(unsafe {wdv_convert_rpu_v1(list[0].as_ptr(),list[0].len(),1,out.as_mut_ptr(),out.len(),&mut p)},-2);assert_eq!(out,[0xA5;8]);assert_eq!(p,0);
        assert_eq!(unsafe {wdv_convert_rpu_v1(std::ptr::null(),0,1,out.as_mut_ptr(),out.len(),&mut p)},-1);
    }
}
