"""Redump-style .cue over the same synthetic tracks the GDI tests use (dreamcomp addition)."""
from dcdisc import open_image
from dcdisc.cue import CueImage
from dcdisc.iso9660 import Iso9660


def _write_cue(gdi_dir, pregap_frames=0):
    cue = gdi_dir / f"synthetic-{pregap_frames}.cue"
    idx = f"00:{pregap_frames // 75:02d}:{pregap_frames % 75:02d}"
    cue.write_text(
        "REM SINGLE-DENSITY AREA\n"
        'FILE "track01.raw" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n'
        'FILE "track02.bin" BINARY\n  TRACK 02 MODE1/2352\n'
        f"    INDEX 00 00:00:00\n    INDEX 01 {idx}\n"
        "REM HIGH-DENSITY AREA\n"
        'FILE "track03.bin" BINARY\n  TRACK 03 MODE1/2352\n    INDEX 01 00:00:00\n')
    return str(cue)


def test_cue_matches_gdi_layout(gdi_dir, synthetic_files):
    files, _ = synthetic_files
    with open_image(_write_cue(gdi_dir)) as img:
        assert isinstance(img, CueImage)
        assert [(t.number, t.lba, t.is_data) for t in img.tracks] == [
            (1, 0, False), (2, 300, True), (3, 45000, True)]
        fs = Iso9660(img)
        for p, data in files.items():
            assert fs.read_path(p) == data, p


def test_cue_index00_pregap_is_skipped(gdi_dir):
    with open_image(_write_cue(gdi_dir, pregap_frames=10)) as img:
        t2 = img.tracks[1]
        assert t2.lba == 310 and t2.sectors == 50
        # The first sector of the track proper is the 11th stored sector.
        assert img.read_raw_sector(310)[16] == 10
