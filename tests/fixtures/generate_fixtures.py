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


def theme_xml():
    slots = {
        "dk1": "1E2430", "lt1": "FFFFFF", "dk2": "101114", "lt2": "EEEEEE",
        "accent1": "FF0000", "accent2": "00FF00", "accent3": "0000FF",
        "accent4": "FFFF00", "accent5": "FF00FF", "accent6": "00FFFF",
        "hlink": "0563C1", "folHlink": "954F72",
    }
    scheme = "".join(f'<a:{k}><a:srgbClr val="{v}"/></a:{k}>' for k, v in slots.items())
    return (
        XML_DECL
        + f'<a:theme xmlns:a="{A}" name="T"><a:themeElements>'
        + f'<a:clrScheme name="C">{scheme}</a:clrScheme>'
        + '<a:fontScheme name="F"/>'
        # bgFillStyleLst backs <p:bgRef idx="1001|1002|...">. Entry 1 is a plain
        # solidFill of phClr (the color carried by the bgRef itself) and so is
        # exactly resolvable; entry 2 is a gradient and so is not.
        + '<a:fmtScheme name="S"><a:fillStyleLst/><a:lnStyleLst/><a:effectStyleLst/>'
        + "<a:bgFillStyleLst>"
        + '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>'
        + '<a:gradFill><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr"/></a:gs>'
        + '<a:gs pos="100000"><a:schemeClr val="lt1"/></a:gs></a:gsLst></a:gradFill>'
        + "</a:bgFillStyleLst></a:fmtScheme>"
        + "</a:themeElements></a:theme>"
    )


def scheme_run_sp(text, x, y, cx, cy, scheme, size=3600):
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        "</p:spPr><p:txBody><a:p><a:r>"
        f'<a:rPr sz="{size}"><a:solidFill><a:schemeClr val="{scheme}"/></a:solidFill></a:rPr>'
        f"<a:t>{text}</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def nocolor_run_sp(text, x, y, cx, cy, size=3600):
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f'</p:spPr><p:txBody><a:p><a:r><a:rPr sz="{size}"/>'
        f"<a:t>{text}</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def multirun_sp(x, y, cx, cy, size=3600):
    def run(t, hexc):
        return (
            f'<a:r><a:rPr sz="{size}"><a:solidFill><a:srgbClr val="{hexc}"/></a:solidFill></a:rPr>'
            f"<a:t>{t}</a:t></a:r>"
        )
    runs = run("RRRR", "FF0000") + run("GGGG", "00FF00") + run("BBBB", "0000FF")
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        f"</p:spPr><p:txBody><a:p>{runs}</a:p></p:txBody></p:sp>"
    )


def linebreak_sp(x, y, cx, cy):
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        '</p:spPr><p:txBody><a:p>'
        '<a:r><a:rPr sz="3600"><a:solidFill><a:srgbClr val="FFFFFF"/></a:solidFill></a:rPr>'
        "<a:t>Q3</a:t></a:r><a:br/>"
        '<a:r><a:rPr sz="3600"><a:solidFill><a:srgbClr val="FFFFFF"/></a:solidFill></a:rPr>'
        "<a:t>FY26</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def bullet_sp(x, y, cx, cy):
    return (
        "<p:sp><p:spPr>"
        f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
        '</p:spPr><p:txBody>'
        '<a:p><a:pPr lvl="1"><a:buChar char="•"/></a:pPr>'
        '<a:r><a:rPr sz="2400"><a:solidFill><a:srgbClr val="FFFFFF"/></a:solidFill></a:rPr>'
        "<a:t>bullet item</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def title_ph_no_xfrm(text):
    # A title placeholder shape with NO inline xfrm (position inherited from layout).
    return (
        "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Title\"/><p:cNvSpPr/>"
        '<p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr>'
        "<p:spPr/><p:txBody><a:p><a:r>"
        '<a:rPr sz="4400"><a:solidFill><a:srgbClr val="FFFFFF"/></a:solidFill></a:rPr>'
        f"<a:t>{text}</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def layout_xml():
    # A slideLayout defining the title placeholder's geometry.
    title = (
        '<p:sp><p:nvSpPr><p:cNvPr id="2" name="Title"/><p:cNvSpPr/>'
        '<p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr>'
        '<p:spPr><a:xfrm><a:off x="600000" y="400000"/>'
        '<a:ext cx="11000000" cy="1500000"/></a:xfrm></p:spPr>'
        "<p:txBody><a:p/></p:txBody></p:sp>"
    )
    return (
        XML_DECL
        + f'<p:sldLayout xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld><p:spTree>{title}</p:spTree></p:cSld></p:sldLayout>"
    )


def group_sp(text, x, y, cx, cy):
    inner = scheme_run_sp(text, x, y, cx, cy, "lt1")  # white text via theme
    return f"<p:grpSp><p:grpSpPr/>{inner}</p:grpSp>"


def build_good_theme():
    # Themed dark background + scheme-colored text + a no-color run. Exercises
    # BUG-1: scheme colors resolve; uncolored text stays visible on dark.
    bg = '<p:bg><p:bgPr><a:solidFill><a:schemeClr val="dk1"/></a:solidFill></p:bgPr></p:bg>'
    body = scheme_run_sp("Accent", 800000, 500000, 6000000, 1000000, "accent1") + nocolor_run_sp(
        "Plain", 800000, 2000000, 6000000, 1000000
    )
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg}<p:spTree>{body}</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/theme/theme1.xml": theme_xml(),
        "ppt/slides/slide1.xml": slide,
    }
    write_zip("good_theme.pptx", parts)


def build_good_layout():
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld><p:spTree>{title_ph_no_xfrm('Inherited Title')}</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide,
        "ppt/slides/_rels/slide1.xml.rels": XML_DECL
        + f'<Relationships xmlns="{PR}"><Relationship Id="rId1" Type="{R}/slideLayout" '
        + 'Target="../slideLayouts/slideLayout1.xml"/></Relationships>',
        "ppt/slideLayouts/slideLayout1.xml": layout_xml(),
    }
    write_zip("good_layout.pptx", parts)


def build_good_group():
    bg = '<p:bg><p:bgPr><a:solidFill><a:srgbClr val="000000"/></a:solidFill></p:bgPr></p:bg>'
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg}<p:spTree>{group_sp('Grouped', 800000, 800000, 6000000, 1000000)}"
        + "</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/theme/theme1.xml": theme_xml(),
        "ppt/slides/slide1.xml": slide,
    }
    write_zip("good_group.pptx", parts)


def build_good_multiruns():
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [multirun_sp(800000, 2500000, 10000000, 1500000)], bg_hex="000000"
        ),
    }
    write_zip("good_multiruns.pptx", parts)


def build_good_linebreak():
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [linebreak_sp(800000, 500000, 10000000, 3000000)], bg_hex="000000"
        ),
    }
    write_zip("good_linebreak.pptx", parts)


def build_good_bullets():
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [bullet_sp(800000, 1000000, 10000000, 1000000)], bg_hex="000000"
        ),
    }
    write_zip("good_bullets.pptx", parts)


def build_good_longtext():
    long_line = "This is a long sentence that will not fit on a single line and must wrap"
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml(
            [text_sp(long_line, 500000, 500000, 4000000, 4000000, size=3200)], bg_hex="000000"
        ),
    }
    write_zip("good_longtext.pptx", parts)


def master_xml():
    return (
        XML_DECL
        + f'<p:sldMaster xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + "<p:cSld><p:spTree/></p:cSld><p:txStyles>"
        + '<p:titleStyle><a:lvl1pPr><a:defRPr sz="4000"/></a:lvl1pPr></p:titleStyle>'
        + '<p:bodyStyle><a:lvl1pPr><a:defRPr sz="2400"/></a:lvl1pPr>'
        + '<a:lvl2pPr><a:defRPr sz="2000"/></a:lvl2pPr></p:bodyStyle>'
        + '<p:otherStyle><a:lvl1pPr><a:defRPr sz="1800"/></a:lvl1pPr></p:otherStyle>'
        + "</p:txStyles></p:sldMaster>"
    )


def ph_no_size_sp(text, ph_type, x, y, cx, cy):
    # A placeholder shape whose run declares NO size or color (both inherited).
    return (
        f'<p:sp><p:nvSpPr><p:cNvPr id="2" name="P"/><p:cNvSpPr/>'
        f'<p:nvPr><p:ph type="{ph_type}"/></p:nvPr></p:nvSpPr>'
        f'<p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm></p:spPr>'
        f"<p:txBody><a:p><a:r><a:t>{text}</a:t></a:r></a:p></p:txBody></p:sp>"
    )


def build_good_inherit_size():
    bg = '<p:bg><p:bgPr><a:solidFill><a:srgbClr val="000000"/></a:solidFill></p:bgPr></p:bg>'
    body = ph_no_size_sp("Header", "title", 600000, 400000, 11000000, 1500000) + ph_no_size_sp(
        "Body line", "body", 600000, 2200000, 11000000, 3000000
    )
    slide = (
        XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg}<p:spTree>{body}</p:spTree></p:cSld></p:sld>"
    )
    parts = {
        "[Content_Types].xml": content_types(1),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slideMasters/slideMaster1.xml": master_xml(),
        "ppt/slides/slide1.xml": slide,
    }
    write_zip("good_inherit_size.pptx", parts)




def wide_png():
    """A 4x1 solid-red PNG (wide aspect) to test aspect-preserving image draw."""
    import struct, zlib
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data) & 0xFFFFFFFF)
    w, h = 4, 1
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = b""
    for _ in range(h):
        raw += b"\x00" + b"\xff\x00\x00" * w
    idat = zlib.compress(raw)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def build_good_wideimage():
    # 4:1 image placed in a 1:1 (square) frame -> aspect-preserved draw must NOT
    # fill the frame vertically (background shows above/below).
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([pic_sp("rId1", 2000000, 2000000, 4000000, 4000000)], bg_hex="000000"),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": wide_png(),
    }
    write_zip("good_wideimage.pptx", parts)

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


def tiny_emf():
    """A minimal EMF header — a format NO Qt image plugin handles.

    That is the point. A GIF is a format Qt HAS a plugin for, so identifying it
    is cheap; EMF matches nothing, so QImageReader::format() enumerates and
    dlopen()s every installed image plugin looking for a handler. That plugin
    sweep, executed on the pre-render WORKER thread, is what crashed the app on
    Karl's real deck (BUG-30) — his deck carries 5 EMF parts.
    """
    import struct

    # ENHMETAHEADER: iType=1 (EMR_HEADER), nSize=88, bounds/frame rects,
    # dSignature=' EMF' (0x464D4520) at offset 40.
    return (
        struct.pack("<II", 1, 88)
        + struct.pack("<iiii", 0, 0, 100, 100)        # rclBounds
        + struct.pack("<iiii", 0, 0, 2000, 2000)      # rclFrame
        + struct.pack("<I", 0x464D4520)               # dSignature ' EMF'
        + struct.pack("<I", 0x00010000)               # nVersion
        + struct.pack("<I", 88)                       # nBytes
        + struct.pack("<I", 1)                        # nRecords
        + struct.pack("<HH", 0, 0)                    # nHandles, sReserved
        + struct.pack("<II", 0, 0)                    # nDescription, offDescription
        + struct.pack("<I", 0)                        # nPalEntries
        + struct.pack("<ii", 1920, 1080)              # szlDevice
        + struct.pack("<ii", 508, 285)                # szlMillimeters
        + struct.pack("<II", 14, 88)                  # EMR_EOF-ish tail
    ).ljust(88, b"\x00")


def build_good_emf_image():
    # BUG-30 regression fixture: a picture part whose bytes are EMF. The renderer
    # must recognise the bytes are not on the PNG/JPEG allow-list and substitute a
    # placeholder WITHOUT ever constructing a Qt decoder.
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([pic_sp("rId1", 3000000, 2000000, 4000000, 3000000)]),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": tiny_emf(),  # EMF bytes behind a .png name
    }
    write_zip("good_emf_image.pptx", parts)


def pic_sp_cropped(rid, x, y, cx, cy, l=0, t=0, r=0, b=0, alpha=None):
    """A <p:pic> carrying <a:srcRect> crop insets and optional <a:alphaModFix>.

    Inset values may be ints (Transitional, per-100000) or strings such as "25%"
    (ISO 29500 Strict) or outright garbage, so the parser's tolerance is testable.
    """
    amf = f'<a:alphaModFix amt="{alpha}"/>' if alpha is not None else ""
    src = f'<a:srcRect l="{l}" t="{t}" r="{r}" b="{b}"/>' if (l or t or r or b) else ""
    return (
        "<p:pic><p:blipFill>"
        f'<a:blip r:embed="{rid}">{amf}</a:blip>{src}<a:stretch><a:fillRect/></a:stretch>'
        "</p:blipFill>"
        f'<p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm></p:spPr>'
        "</p:pic>"
    )


def stripe_png():
    """A 4x1 PNG: white, white, RED, white. Cropping the outer 25% each side leaves
    the middle 2 px (white+RED); the point is that the WHITE margins disappear."""
    import struct, zlib
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    px = [(255, 255, 255), (255, 255, 255), (255, 0, 0), (255, 255, 255)]
    raw = b"\x00" + b"".join(bytes(c) for c in px)
    return (b"\x89PNG\r\n\x1a\x0a".replace(b"\x0a", b"\n")
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def build_good_srcrect():
    # BUG-37: <a:srcRect> selects the part of the source the deck actually shows.
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        # Three pictures of the SAME 4x1 source (white, white, RED, white):
        #   [0] cropped 25% off each side, opaque  -> shows 2 px: white, RED (50% red)
        #   [1] uncropped, opaque                  -> shows 4 px (25% red)
        #   [2] uncropped, 50% opacity             -> red must composite to pink
        # [0] vs [1] differ ONLY by the crop, so the assertion cannot pass by accident.
        "ppt/slides/slide1.xml": slide_xml([
            pic_sp_cropped("rId1", 0, 0, 6000000, 3000000, l=25000, r=25000),
            pic_sp_cropped("rId1", 6000000, 0, 6000000, 3000000),
            pic_sp_cropped("rId1", 0, 3500000, 6000000, 3000000, alpha=50000),
        ]),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": stripe_png(),
    }
    write_zip("good_srcrect.pptx", parts)


def build_good_pic_placeholder():
    # BUG-41: a <p:pic> dropped into a layout's PICTURE placeholder carries a
    # <p:ph type="pic"> and NO <p:spPr> of its own — PowerPoint writes it exactly
    # this way. Its geometry lives in the layout. Without inheritance it is 0x0 and
    # the renderer skips it, silently losing the picture.
    pic = (
        '<p:pic><p:nvPicPr><p:cNvPr id="7" name="Picture Placeholder 6"/><p:cNvPicPr/>'
        '<p:nvPr><p:ph type="pic" sz="quarter" idx="11"/></p:nvPr></p:nvPicPr>'
        '<p:blipFill><a:blip r:embed="rId1"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>'
        "</p:pic>"  # NOTE: no <p:spPr> at all
    )
    layout_pic = (
        '<p:pic><p:nvPicPr><p:cNvPr id="3" name="Picture Placeholder 2"/><p:cNvPicPr/>'
        '<p:nvPr><p:ph type="pic" sz="quarter" idx="11"/></p:nvPr></p:nvPicPr>'
        "<p:blipFill/>"
        '<p:spPr><a:xfrm><a:off x="8017727" y="0"/>'
        '<a:ext cx="4174273" cy="6858000"/></a:xfrm></p:spPr></p:pic>'
    )
    layout = (
        XML_DECL
        + f'<p:sldLayout xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld><p:spTree>{layout_pic}</p:spTree></p:cSld></p:sldLayout>"
    )
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([pic]),
        "ppt/slides/_rels/slide1.xml.rels": XML_DECL
        + f'<Relationships xmlns="{PR}">'
        + f'<Relationship Id="rId1" Type="{R}/image" Target="../media/image1.png"/>'
        + f'<Relationship Id="rId2" Type="{R}/slideLayout" Target="../slideLayouts/slideLayout1.xml"/>'
        + "</Relationships>",
        "ppt/slideLayouts/slideLayout1.xml": layout,
        "ppt/media/image1.png": tiny_png(),
    }
    write_zip("good_pic_placeholder.pptx", parts)


def wide_red_png():
    """A 16x1 all-red PNG. Width matters: with a 4px source an over-crop's mirrored
    region rounds to under one pixel and the renderer's separate width guard catches
    it by accident, so the over-crop guard itself goes untested. 16px makes the
    mirrored region ~3px — big enough to actually be drawn if the guard is removed."""
    import struct, zlib
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b"\x00" + b"\xff\x00\x00" * 16
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def build_good_srcrect_edge():
    # Adversarial-review fixtures. Each of these survived a mutation of the
    # production code, i.e. the original tests could not tell them apart:
    #   [0] Strict-spelling percentages ("25%") — parsed as 0 by QString::toInt(),
    #       which silently meant "no crop" for srcRect and "invisible" for alpha
    #   [1] TOP/BOTTOM insets only — the real deck uses b=36909 and nothing pinned it
    #       (deleting the t or b parse line survived the whole suite)
    #   [2] ASYMMETRIC l/r — swapping l and r in the parser survived the whole suite
    #   [3] OVER-CROP l+r > 100000 — negative width, which QRectF::intersected
    #       normalises into a MIRRORED draw of the region the deck excluded
    #   [4] unreadable garbage — must warn and show the whole picture, never vanish
    parts = {
        "[Content_Types].xml": content_types(1, has_png=True),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(1),
        "ppt/_rels/presentation.xml.rels": presentation_rels(1),
        "ppt/slides/slide1.xml": slide_xml([
            pic_sp_cropped("rId1", 0, 0, 2000000, 2000000, l="25%", r="25%", alpha="50%"),
            pic_sp_cropped("rId1", 2000000, 0, 2000000, 2000000, t=25000, b=25000),
            pic_sp_cropped("rId1", 4000000, 0, 2000000, 2000000, l=10000, r=40000),
            pic_sp_cropped("rId1", 6000000, 0, 2000000, 2000000, l=60000, r=60000),
            pic_sp_cropped("rId1", 8000000, 0, 2000000, 2000000, l="banana", alpha="banana"),
        ]),
        "ppt/slides/_rels/slide1.xml.rels": slide_rels(image_target="../media/image1.png"),
        "ppt/media/image1.png": wide_red_png(),
    }
    write_zip("good_srcrect_edge.pptx", parts)


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


# --------------------------------------------------------------------------
# BUG-32: background inheritance (slide -> layout -> master).
#
# Real decks almost never put <p:bg> on the slide. Karl's deck has it on the
# master (1/1) and 12 of 17 layouts, and on zero slides — so a loader that reads
# only the slide level renders every slide white.
# --------------------------------------------------------------------------
def bg_el(inner):
    return f"<p:bg><p:bgPr>{inner}<a:effectLst/></p:bgPr></p:bg>"


def solid_bg(hex_or_scheme, scheme=False):
    clr = (
        f'<a:schemeClr val="{hex_or_scheme}"/>'
        if scheme
        else f'<a:srgbClr val="{hex_or_scheme}"/>'
    )
    return bg_el(f"<a:solidFill>{clr}</a:solidFill>")


def layout_with_bg(bg_xml):
    return (
        XML_DECL
        + f'<p:sldLayout xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg_xml}<p:spTree/></p:cSld></p:sldLayout>"
    )


def master_with_bg(bg_xml):
    return (
        XML_DECL
        + f'<p:sldMaster xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{bg_xml}<p:spTree/></p:cSld>"
        + '<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" '
        + 'accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" '
        + 'accent6="accent6" hlink="hlink" folHlink="folHlink"/>'
        + "</p:sldMaster>"
    )


def slide_layout_rel(n):
    return (
        XML_DECL
        + f'<Relationships xmlns="{PR}"><Relationship Id="rId1" Type="{R}/slideLayout" '
        + f'Target="../slideLayouts/slideLayout{n}.xml"/></Relationships>'
    )


def layout_master_rel():
    return (
        XML_DECL
        + f'<Relationships xmlns="{PR}"><Relationship Id="rId1" Type="{R}/slideMaster" '
        + 'Target="../slideMasters/slideMaster1.xml"/></Relationships>'
    )


def build_good_bg_inherit():
    body = [text_sp("T", 600000, 400000, 6000000, 1000000)]
    parts = {
        "[Content_Types].xml": content_types(4),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(4),
        "ppt/_rels/presentation.xml.rels": presentation_rels(4),
        "ppt/theme/theme1.xml": theme_xml(),
        # 1: the slide declares its own -> slide wins over layout AND master.
        "ppt/slides/slide1.xml": slide_xml(body, bg_hex="AABBCC"),
        "ppt/slides/_rels/slide1.xml.rels": slide_layout_rel(1),
        # 2: no slide bg, layout1 declares one -> layout wins over master.
        "ppt/slides/slide2.xml": slide_xml(body),
        "ppt/slides/_rels/slide2.xml.rels": slide_layout_rel(1),
        # 3: no slide bg, layout2 declares none -> falls through to the master,
        #    whose fill is a SCHEME color and so also exercises theme resolution.
        "ppt/slides/slide3.xml": slide_xml(body),
        "ppt/slides/_rels/slide3.xml.rels": slide_layout_rel(2),
        # 4: no slide bg and no layout rel at all -> still reaches the master.
        "ppt/slides/slide4.xml": slide_xml(body),
        "ppt/slideLayouts/slideLayout1.xml": layout_with_bg(solid_bg("0076A3")),
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels": layout_master_rel(),
        "ppt/slideLayouts/slideLayout2.xml": layout_with_bg(""),
        "ppt/slideLayouts/_rels/slideLayout2.xml.rels": layout_master_rel(),
        # dk1 -> 1E2430 in theme_xml()'s scheme.
        "ppt/slideMasters/slideMaster1.xml": master_with_bg(solid_bg("dk1", scheme=True)),
    }
    write_zip("good_bg_inherit.pptx", parts)


def build_good_bg_unsupported():
    # A background we cannot faithfully paint must WARN, never silently render
    # white and never guess a solid color (Manifesto F1: no silent wrong render).
    grad = bg_el(
        "<a:gradFill><a:gsLst>"
        '<a:gs pos="0"><a:srgbClr val="FF0000"/></a:gs>'
        '<a:gs pos="100000"><a:srgbClr val="0000FF"/></a:gs>'
        "</a:gsLst></a:gradFill>"
    )
    blip = bg_el('<a:blipFill><a:blip r:embed="rId9"/></a:blipFill>')
    parts = {
        "[Content_Types].xml": content_types(3),
        "_rels/.rels": root_rels(),
        "ppt/presentation.xml": presentation_xml(3),
        "ppt/_rels/presentation.xml.rels": presentation_rels(3),
        "ppt/theme/theme1.xml": theme_xml(),
        # 1: slide declares a gradient -> warn, and do NOT fall through to the
        #    layout (the slide's declaration is what applies, supported or not).
        "ppt/slides/slide1.xml": XML_DECL
        + f'<p:sld xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}">'
        + f"<p:cSld>{grad}<p:spTree/></p:cSld></p:sld>",
        "ppt/slides/_rels/slide1.xml.rels": slide_layout_rel(1),
        # 2: layout declares a picture fill -> warn against slide 2.
        "ppt/slides/slide2.xml": slide_xml([]),
        "ppt/slides/_rels/slide2.xml.rels": slide_layout_rel(2),
        # 3: layout declares <p:bgRef> into the theme's bgFillStyleLst, whose
        #    first entry is a plain solidFill of phClr -> resolvable, no warning.
        "ppt/slides/slide3.xml": slide_xml([]),
        "ppt/slides/_rels/slide3.xml.rels": slide_layout_rel(3),
        "ppt/slideLayouts/slideLayout1.xml": layout_with_bg(solid_bg("00FF00")),
        "ppt/slideLayouts/slideLayout2.xml": layout_with_bg(blip),
        "ppt/slideLayouts/slideLayout3.xml": XML_DECL
        + f'<p:sldLayout xmlns:a="{A}" xmlns:r="{R}" xmlns:p="{P}"><p:cSld>'
        + '<p:bg><p:bgRef idx="1001"><a:srgbClr val="123456"/></p:bgRef></p:bg>'
        + "<p:spTree/></p:cSld></p:sldLayout>",
    }
    write_zip("good_bg_unsupported.pptx", parts)


if __name__ == "__main__":
    build_good_bg_inherit()
    build_good_bg_unsupported()
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
    build_good_emf_image()
    build_good_srcrect()
    build_good_srcrect_edge()
    build_good_pic_placeholder()
    build_good_overflow()
    build_good_manypara()
    build_good_theme()
    build_good_layout()
    build_good_group()
    build_good_multiruns()
    build_good_linebreak()
    build_good_bullets()
    build_good_longtext()
    build_good_inherit_size()
    build_good_wideimage()
    print("fixtures written to", HERE)
