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
    # An unsupported element (a table) — the loader must record a warning + skip.
    return (
        "<p:graphicFrame><a:graphic>"
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
    print("fixtures written to", HERE)
