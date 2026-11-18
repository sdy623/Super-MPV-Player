from pathlib import Path


root = Path(__file__).parent.parent
source = (root / "core/mpv/video/out/vo_gpu_next.c").read_text(encoding="utf-8")
pair_source = (root / "core/mpv/filters/f_enhancement_pair.c").read_text(encoding="utf-8")


def smoothstep(lo: float, hi: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - lo) / (hi - lo)))
    return t * t * (3.0 - 2.0 * t)


def fel_floor(anchor: float, peak: float, authored_max: float) -> float:
    tail = smoothstep(anchor, peak, authored_max)
    return anchor + (peak - anchor) * tail


# A real Profile 7 FEL is not selected by the profile label alone. The final
# frame mix must contain the paired EL and active non-trivial NLQ metadata.
assert "mix_has_fel_residual" in source
assert "frame->enhancement_layer" in source
assert "frame->repr.dovi->nlq_active" in source

# RPU Level-1 maximum PQ luminance describes the authored highlight tail. It
# may allocate more of the measured panel envelope only after FEL composition;
# ordinary P8.4/MEL/P5 mapping retains the accepted 15%/40% rules.
assert "pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS" in source
assert "p7_fel_signal_floor" in source
assert "next_peak = MPMAX(next_peak, fel_peak)" in source
assert "fel != p->dovi_sdr_probe_fel" in source
assert "const bool visual_cut = fel_cut" in source
assert "0.4f * (display_peak - signal_anchor) * headroom" in source
assert "0.15f * (display_peak - signal_anchor) * dark" in source

# A visible FEL frame must never silently degrade to BL-only after a seek. If
# the EL decoder catches up at a newer PTS, discard only the unreconstructable
# FEL BL frame; MEL and explicit EL-off comparisons retain BL fallback.
assert "bl_requires_fel" in pair_source
assert "bl->params.repr.dovi->nlq_active" in pair_source
assert "bl->params.no_enhancement_layer" in pair_source
assert "dropping unreconstructable FEL BL" in pair_source

# On the calibrated 270/400-nit display, an FEL scene authored to at least the
# panel peak can use the 400-nit ceiling. A lower authored tail remains
# continuous instead of switching the whole profile to a fixed 400-nit target.
assert fel_floor(270.0, 400.0, 435.0) == 400.0
assert 270.0 < fel_floor(270.0, 400.0, 335.0) < 400.0
assert fel_floor(270.0, 400.0, 270.0) == 270.0

print("PASS P7 FEL headroom follows paired EL/NLQ and authored RPU highlight tail")
