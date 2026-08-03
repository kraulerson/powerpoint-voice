#!/usr/bin/env python3
"""Generate synthetic .pptx fixtures for the deck-loader tests.

These are hand-built minimal OOXML packages — NOT exports of any real deck. The
real presentation is Confidential and never enters this repo (Manifesto §2.4,
intake §5.1.1). Run `python3 generate_fixtures.py` to (re)produce the .pptx files
committed alongside it; the C++ tests consume the .pptx, not this script, so CI
needs no Python.
"""
import struct
import zlib
import zipfile
from pathlib import Path

HERE = Path(__file__).parent

A = "http://schemas.openxmlformats.org/drawingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
P = "http://schemas.openxmlformats.org/presentationml/2006/main"
CT = "http://schemas.openxmlformats.org/package/2006/content-types"
PR = "http://schemas.openxmlformats.org/package/2006/relationships"

XML_DECL = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'


def content_types(slide_count, has_png=False):
    overrides = "".join(
        f'<Override PartName="/ppt/slides/slide{i}.xml" '
        f'ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>'
        for i in range(1, slide_count + 1)
    )
    png_default = '<Default Extension="png" ContentType="image/png"/>' if has_png else ""
    return (
        XML_DECL
        + f'<Types xmlns="{CT}">'
        + '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        + '<Default Extension="xml" ContentType="application/xml"/>'
        + png_default
        + '<Override PartName="/ppt/presentation.xml" '
        + 'ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>'
        + overrides
        + "</Types>"
    )


def root_rels():
    return (
        XML_DECL
        + f'<Relationships xmlns="{PR}">'
        + f'<Relationship Id="rId1" Type="{R}/officeDocument" Target="ppt/presentation.xml"/>'
        + "</Relationships>"
    )


def presentation_xml(slide_count, cx=12192000, cy=6858000):
    sld_ids = "".join(
        f'<p:sldId id="{256 + i}" r:id="rId{i + 1}"/>' for i in range(slide_count)
    )
    return (
        XML_DECL
        + f'<p:presentation xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:sldIdLst>{sld_ids}</p:sldIdLst>"
        + f'<p:sldSz cx="{cx}" cy="{cy}"/>'
        + "</p:presentation>"
    )


def presentation_rels(slide_count):
    rels = "".join(
        f'<Relationship Id="rId{i + 1}" '
        f'Type="{R}/slide" Target="slides/slide{i + 1}.xml"/>'
        for i in range(slide_count)
    )
    return XML_DECL + f'<Relationships xmlns="{PR}">{rels}</Relationships>'


def text_sp(text, x, y, cx, cy, size=4400, bold=True, color="FFFFFF", face="Calibri"):
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        "</p:spPr><p:txBody><a:p><a:r>"
        f'<a:rPr sz="{size}" b="{1 if bold else 0}">'
        f'<a:solidFill><a:srgbClr val="{color}"/></a:solidFill>'
        f'<a:latin typeface="{face}"/></a:rPr>'
        f"<a:t>{text}</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def empty_text_sp(x, y, cx, cy):
    # A shape with a text body but no runs — a real deck has empty placeholders.
    # The loader must handle it without crashing or inventing a phantom run.
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        "</p:spPr><p:txBody><a:p/></p:txBody></p:sp>"
    )


def pic_sp(rid, x, y, cx, cy):
    return (
        "<p:pic><p:blipFill>"
        f'<a:blip r:embed="{rid}"/></p:blipFill>'
        f'<p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm></p:spPr>'
        "</p:pic>"
    )


def table_graphicframe():
    # An unsupported element (a table) WITH geometry — the loader records a
    # warning AND an Unsupported placeholder element at this rect so the renderer
    # can draw a visible marker there.
    return (
        "<p:graphicFrame>"
        '<p:xfrm><a:off x="2000000" y="3000000"/><a:ext cx="6000000" cy="2000000"/></p:xfrm>'
        "<a:graphic>"
        f'<a:graphicData uri="{A.replace("/main", "/table")}">'
        "<a:tbl><a:tr><a:tc><a:txBody><a:p><a:r><a:t>cell</a:t></a:r></a:p></a:txBody></a:tc></a:tr></a:tbl>"
        "</a:graphicData></a:graphic></p:graphicFrame>"
    )


def slide_xml(body_shapes, bg_hex=None):
    bg = ""
    if bg_hex:
        bg = f'<p:bg><p:bgPr><a:solidFill><a:srgbClr val="{bg_hex}"/></a:solidFill></p:bgPr></p:bg>'
    return (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg}<p:spTree>{''.join(body_shapes)}</p:spTree></p:cSld></p:sld>"
    )


def slide_rels(image_target=None):
    rel = ""
    if image_target:
        rel = f'<Relationship Id="rId1" Type="{R}/image" Target="{image_target}"/>'
    return XML_DECL + f'<Relationships xmlns="{PR}">{rel}</Relationships>'


def tiny_gif():
    """A minimal valid 1x1 GIF89a — a format the renderer's allow-list rejects."""
    return bytes(
        [
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61,  # GIF89a
            0x01, 0x00, 0x01, 0x00,              # 1x1
            0x80, 0x00, 0x00,                    # global color table, 2 colors
            0xFF, 0x00, 0x00,                    # color 0: red
            0x00, 0x00, 0x00,                    # color 1: black
            0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,  # image descriptor
            0x02, 0x02, 0x44, 0x01, 0x00,        # LZW data
            0x3B,                                # trailer
        ]
    )


def multi_para_sp(n, x, y, cx, cy):
    # One text box with n paragraphs, each one run — for the per-box paragraph cap.
    paras = "".join(
        f'<a:p><a:r><a:rPr sz="1800"><a:solidFill><a:srgbClr val="FFFFFF"/></a:solidFill>'
        f"</a:rPr><a:t>line{i}</a:t></a:r></a:p>"
        for i in range(n)
    )
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f"</p:spPr><p:txBody>{paras}</p:txBody></p:sp>"
    )


def graphicframe_at(x, y, cx, cy):
    return (
        "<p:graphicFrame>"
        f'<p:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></p:xfrm>'
        f'<a:graphic><a:graphicData uri="{A.replace("/main", "/table")}"><a:tbl/>'
        "</a:graphicData></a:graphic></p:graphicFrame>"
    )


def tiny_png():
    """A minimal valid 1x1 opaque PNG (no external tooling needed)."""
    def chunk(tag, data):
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)  # 1x1, 8-bit RGB
    raw = b"\x00\xff\x00\x00"  # one filtered scanline: filter 0 + red pixel
    idat = zlib.compress(raw)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def write_zip(name, parts, raw_bytes=None):
    path = HERE / name
    if raw_bytes is not None:
        path.write_bytes(raw_bytes)
        return
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for part_name, data in parts.items():
            z.writestr(part_name, data)


def build_good_text():
    parts = {
        "[Content_Types].xml": content_types(2),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(2),
        "ppt/_rels/presentation.xml.rels": presentation_rels(2),
        "ppt/slides/slide1.xml": slide_xml(
            [text_sp("Quarterly Review", 838200, 365125, 10515600, 1325563)],
            bg_hex="1E2430",
        ),
        "ppt/slides/slide2.xml": slide_xml(
            [text_sp("Agenda", 838200, 365125, 10515600, 1000000, size=3200, bold=False)]
        ),
    }
    write_zip("good_text.pptx", parts)


def build_good_image():
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [
                text_sp("With Image", 838200, 200000, 10515600, 900000),
                pic_sp("rId1", 1000000, 1500000, 2000000, 1500000),
            ]
        ),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": tiny_png(),
    }
    write_zip("good_image.pptx", parts)


def build_good_unsupported():
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        # A text box (supported) AND a table graphicFrame (unsupported → warning).
        "ppt/slides/slide1.xml": slide_xml(
            [text_sp("Has a table", 838200, 200000, 10515600, 900000), table_graphicframe()]
        ),
    }
    write_zip("good_unsupported.pptx", parts)


def build_bad_notzip():
    write_zip("bad_notzip.pptx", None, raw_bytes=b"this is not a zip archive at all\n")


def build_bad_nopresentation():
    parts = {
        "[Content_Types].xml": content_types(0),
        "_rels/.rels": root_rels(),
        # ppt/presentation.xml deliberately absent.
        "ppt/notes.xml": "<notes/>",
    }
    write_zip("bad_nopresentation.pptx", parts)


def build_bad_malformedxml():
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        # Unclosed tag → parser must reject.
        "ppt/presentation.xml": XML_DECL
        + f'<p:presentation xmlns:p="{P}"><p:sldIdLst><p:sldId id="256"',
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([text_sp("x", 0, 0, 100, 100)]),
    }
    write_zip("bad_malformedxml.pptx", parts)


def build_good_edge():
    # One real text box + one empty text box on the same slide.
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [
                text_sp("Present", 838200, 200000, 10515600, 900000),
                empty_text_sp(838200, 1200000, 10515600, 900000),
            ]
        ),
    }
    write_zip("good_edge.pptx", parts)


def build_deep_nest():
    # A background nested very deep — was a stack-overflow crash before the
    # recursive walker became iterative (audit F1a-2). ~3000 levels is well
    # under any part-size cap and must load without crashing or hanging.
    depth = 3000
    inner = "<a:solidFill><a:srgbClr val=\"112233\"/></a:solidFill>"
    nested = inner
    for _ in range(depth):
        nested = f"<a:x>{nested}</a:x>"
    bg = f"<p:bg><p:bgPr>{nested}</p:bgPr></p:bg>"
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg}<p:spTree>"
        + text_sp("Deep", 0, 0, 100, 100)
        + "</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide,
    }
    write_zip("deep_nest.pptx", parts)


def build_drop_missing_rel():
    # 3 declared slides; slide 2's relationship is ABSENT. The loader must keep
    # slide numbering intact (placeholder at index 2), not compact to 2 slides
    # (audit F1a-3 — index drift breaks "go to slide N").
    rels = (
        f'<Relationship Id="rId1" Type="{R}/slide" Target="slides/slide1.xml"/>'
        # rId2 deliberately missing
        f'<Relationship Id="rId3" Type="{R}/slide" Target="slides/slide3.xml"/>'
    )
    parts = {
        "[Content_Types].xml": content_types(3),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(3),
        "ppt/_rels/presentation.xml.rels": XML_DECL
        + f'<Relationships xmlns="{PR}">{rels}</Relationships>',
        "ppt/slides/slide1.xml": slide_xml([text_sp("One", 0, 0, 100, 100)]),
        "ppt/slides/slide3.xml": slide_xml([text_sp("Three", 0, 0, 100, 100)]),
    }
    write_zip("drop_missing_rel.pptx", parts)


def build_drop_missing_part():
    # 3 declared slides, all rels present, but slide2.xml part is ABSENT.
    parts = {
        "[Content_Types].xml": content_types(3),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(3),
        "ppt/_rels/presentation.xml.rels": presentation_rels(3),
        "ppt/slides/slide1.xml": slide_xml([text_sp("One", 0, 0, 100, 100)]),
        # slide2.xml deliberately missing
        "ppt/slides/slide3.xml": slide_xml([text_sp("Three", 0, 0, 100, 100)]),
    }
    write_zip("drop_missing_part.pptx", parts)


def build_many_shapes():
    # One slide with many text boxes — exercises the per-slide shape cap
    # (audit F1a-4). 200 shapes, tested against a low cap.
    shapes = [text_sp(f"box{i}", 0, i * 1000, 500000, 500000) for i in range(200)]
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(shapes),
    }
    write_zip("many_shapes.pptx", parts)


def build_good_fontsizes():
    # Two slides, same wide text in the same box on a black background, at very
    # different point sizes — the large one must paint more text pixels (F1b
    # font-size fidelity). Black bg so non-black pixels == text.
    def slide(sz):
        return slide_xml(
            [text_sp("WWWWWW", 838200, 2000000, 10515600, 2000000, size=sz, bold=True)],
            bg_hex="000000",
        )

    parts = {
        "[Content_Types].xml": content_types(2),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(2),
        "ppt/_rels/presentation.xml.rels": presentation_rels(2),
        "ppt/slides/slide1.xml": slide(6000),  # 60pt
        "ppt/slides/slide2.xml": slide(2000),  # 20pt
    }
    write_zip("good_fontsizes.pptx", parts)


def build_good_missing_image():
    # A picture whose media part is ABSENT — imageData stays empty and the
    # renderer must draw a visible "missing image" placeholder (F1b), not crash.
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [pic_sp("rId1", 3000000, 2000000, 4000000, 3000000)]
        ),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        # ppt/media/image1.png deliberately absent
    }
    write_zip("good_missing_image.pptx", parts)


def build_good_hugefont():
    # An absurd declared font size (audit R1) — must render without hanging/OOM.
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [text_sp("BIG", 838200, 200000, 10515600, 6000000, size=5160000)], bg_hex="000000"
        ),
    }
    write_zip("good_hugefont.pptx", parts)


def build_good_gif_image():
    # A picture whose bytes are a GIF — a format outside the PNG/JPEG allow-list
    # (audit R2). Must render a placeholder, not invoke the GIF decoder.
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([pic_sp("rId1", 3000000, 2000000, 4000000, 3000000)]),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": tiny_gif(),  # GIF bytes behind a .png name
    }
    write_zip("good_gif_image.pptx", parts)


def build_good_overflow():
    # An unsupported element positioned partly OUTSIDE the slide (negative off) —
    # without a clip rect its placeholder box would bleed into the letterbox (R4).
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + "<p:cSld><p:spTree>"
        + graphicframe_at(-3000000, -3000000, 6000000, 6000000)
        + "</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide,
    }
    write_zip("good_overflow.pptx", parts)


def build_good_manypara():
    # One text box with many paragraphs — for the per-box paragraph cap (R5).
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([multi_para_sp(500, 838200, 200000, 10515600, 6000000)]),
    }
    write_zip("good_manypara.pptx", parts)


if __name__ == "__main__":
    build_good_text()
    build_good_image()
    build_good_unsupported()
    build_good_edge()
    build_bad_notzip()
    build_bad_nopresentation()
    build_bad_malformedxml()
    build_deep_nest()
    build_drop_missing_rel()
    build_drop_missing_part()
    build_many_shapes()
    build_good_fontsizes()
    build_good_missing_image()
    build_good_hugefont()
    build_good_gif_image()
    build_good_overflow()
    build_good_manypara()
    print("fixtures written to", HERE)
