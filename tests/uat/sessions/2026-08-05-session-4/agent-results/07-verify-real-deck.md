# UAT-4 — hostile-deck testing of the loader + renderer (malicious-user persona)

**Harness** (all under `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4/`):
`build-ubsan/` = HEAD `cd11cf8` + `-fsanitize=address,undefined -fno-sanitize-recover=undefined`; `build-rel/` = plain Release for observable behaviour. 96 generated decks (`gen_hostile.py`, `gen2.py`, `gen3.py`), driver `run.py` (sanitised), `inspect.py` (classifies the rendered PNG: picture drawn / placeholder drawn / **nothing**), `prevent_probe.cpp` (F7b PREVENT gate + per-slide render cost), `load_probe.cpp`, `fuzz.py`. Project tree untouched; no git writes. The confidential deck was never opened, rendered or read — only `<p:ph>` **attribute values** and rel Targets were extracted via `ph_shape.py`, inside the stated carve-out.

---

## FINDING 1 — SEV-1 · VERIFIED · BUG-58 regressed BUG-41; the real deck's slide-1 picture is silently lost again

`placeholderKey()` (`src/loader/deck_loader.cpp:537-566`) now namespaces the key by the **non-visual-properties holder element**. PowerPoint declares a picture placeholder in a *layout* as `<p:sp>` and on a *slide* as `<p:pic>`. The two keys can therefore never match, so the slide's `<p:pic>` inherits no geometry, stays 0×0, and the renderer draws nothing.

A/B, identical decks except the layout's holder tag (same `<p:ph type="pic" idx="11"/>` on both sides, slide `<p:pic>` has no `<p:spPr>`):

```
$ python3 inspect.py 'c01[ab]*'
DECK                                      RC   REDpx  GREYpx  VERDICT
c01a_layout_holder_sp                      0       0       0  NOTHING      <- layout <p:sp>
c01b_layout_holder_pic                     0   16589       0  RED          <- layout <p:pic>
```

It is a regression introduced by commit `571ccce` (BUG-57/58). Before it the key was `type + ":" + idx`, which matched:

```
$ git show 460f390:src/loader/deck_loader.cpp | sed -n '/^QString placeholderKey/,/^}/p'
    return type + QLatin1Char(':') + attrLocal(ph, "idx");
```

The reference deck is the failing shape — every layout placeholder in it is `<p:sp>`, there is not one `<p:pic>` placeholder in any layout, and slide 1's `<p:pic>` carries no geometry of its own (`ownExt=0`):

```
$ python3 ph_shape.py "<reference deck>"        # tags + type/idx values only
ppt/slides/slide1.xml        <p:pic> ph(holder=nvPicPr, type='pic', idx='11') ownExt=0  loaderKey=nvPicPr|pic:11
ppt/slideLayouts/slideLayout1.xml  <p:sp> ph(holder=nvSpPr, type='pic', idx='11') ownExt=1  loaderKey=nvSpPr|pic:11
# slide1.xml.rels -> slideLayout ../slideLayouts/slideLayout1.xml
```

`nvPicPr|pic:11` ≠ `nvSpPr|pic:11` → rect 0×0 → `slide_renderer.cpp:296` `else if (r.width() >= 1 …)` is false → no draw, no placeholder, no warning. Slide 9's `<p:pic>` has `ownExt=1` so it survives; slide 1's is the one that disappears. Note BUG-59 is recorded as "already fixed by BUG-41" — it is not.

The collision BUG-58 was written to prevent is real, but the discriminator must be `type:idx` scoped so that a `<p:pic>` on a slide can match an `<p:sp>` placeholder in a layout (ECMA-376 matches placeholders on `idx`, not on the holder element).

---

## FINDING 2 — SEV-2 · VERIFIED · Signed-integer overflow in `sourceRect()`, on the two lines below the guard that was just fixed for it

`src/render/slide_renderer.cpp:217` and `:218` sum the srcRect insets in **`int`**, immediately after lines 210-211 sum the same values in `qint64` precisely because that overflow was UB (adversarial review F5).

```
$ ./build-ubsan/render_preview decks/a01_srcrect.pptx /tmp/o 640 360      # 3 097-byte deck
slide_renderer.cpp:217:66: runtime error: signed integer overflow: -2147483648 + -1 cannot be represented in type 'int'
    #0 pptv::SlideRenderer::render(...) slide_renderer.cpp:302
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior slide_renderer.cpp:217:66
```

Reproduced by `a01` (`l="-2147483648" r="-1"`), `a02` (`l=r="-2000000000"`), `a17` (`l=r="-2147483648"`), `a03` (line 218, the vertical pair), and `h01` (same, nested five groups deep). The 64-bit guard at 210-212 lets every negative pair straight through.

**It is not only theoretical — in the shipping Release build it silently deletes the picture.** The wrap flips the computed width's sign, `QRectF::intersected` then returns empty, and `src.width() <= 0` breaks out of the draw:

```
$ python3 inspect.py 'a0[15]*'
a01_srcrect   (l=-2147483648 r=-1)      REDpx=0      NOTHING     <- overflow: picture gone
a05_srcrect   (l=-2147483648 alone)     REDpx=8294   RED         <- no overflow: picture drawn
```

Same deck, one extra attribute, no warning and no placeholder either way.

---

## FINDING 3 — SEV-2 · VERIFIED · A 134 KB deck freezes one slide's raster for 53 seconds; the TM-018 PREVENT gate counts runs, not characters

`measureComplexity()` counts `shapes` and `textRuns` only. `LoaderLimits` allows 2 000 paragraphs/box × 1 000 runs/paragraph × 100 000 chars/run, so a slide with **one** shape and 2 000 runs holds 80 million characters and sails through `RenderCaps{5000 runs, 2000 shapes}`:

```
$ ./prevent_probe decks/k05_text_1x2000.pptx           # 134 207 bytes on disk
slide 1: shapes=1 textRuns=2000 chars=80000000  PREVENT-gate: PASSES -> goes to QPainter
slide 1 render: 52.47 s (image 1920x1080)

$ ./load_probe decks/k05_text_1x2000.pptx
load ok=1 slides=1 warnings=0  0.05 s                  # the loader is innocent; it is all QTextLayout
```

Scaling is linear and the multiplier is free — 300 `<p:sldId>` entries may all point at the same slide part, and the decompression cap (which walks the central directory once) charges that part once:

```
k01 1 slide  ×      1.6e6 chars   0.45 s   (2 649 bytes)
k02 1 slide  ×      1.6e6 chars   2.33 s   (5 278 bytes)
k03 10 slides×      1.6e6 chars  18.61 s   (5 305 bytes)
k04 300slides×      1.6e6 chars  >9 min    (6 014 bytes)
k06 300slides× 80e6 chars each   ≈4.4 h    (134 943 bytes)   [derived from the measured 52.47 s/slide]
```

ISOLATE works — the worker is off-thread so the UI survives — but the presenter looks at an unpainted surface until `slideReady` arrives. CONTAIN is documented in `pre_render_worker.hpp:22` but is not wired: `progress(done,total,elapsedMs)` is emitted and never connected (`grep -rn PreRenderWorker src/` → `app_shell.cpp` connects `slideReady` and `finished` only), so nothing degrades or abandons a slide that is already 50 s in. The missing cap is characters (or a per-slide render deadline), not runs.

---

## FINDING 4 — SEV-2 · VERIFIED · The renderer draws a picture the deck did not ask for

`deck_loader.cpp:652` locates the image with an **unscoped** whole-subtree search: `pugi::xml_node blip = descendantLocal(node, "blip");`. Four lines later (`:657-663`) the same function deliberately reads `<a:srcRect>` and `<a:stretch>` as *direct children* of `<p:blipFill>`, with the comment *"NOT by descendant search: a group or a nested fill elsewhere in the `<p:pic>` must not supply this picture's crop"* — the hazard was reasoned about for the crop and left on the picture's identity.

Both decks embed image1 (red, the picture the deck names) and image2 (green, the planted one). Both rendered **green**:

```
j01_blip_from_line_fill        [((255,255,255),40852), ((0,255,0),16224), …]
j02_blip_shadowed_by_nvpicpr   [((255,255,255),40852), ((0,255,0),16224), …]
```

- `j01`: `<p:blipFill>` contains **no** `<a:blip>` at all; the only blip is a decorative outline fill at `<p:spPr><a:ln><a:blipFill><a:blip r:embed="rId2"/>`. It is drawn as the picture.
- `j02`: `<p:blipFill><a:blip r:embed="rId1"/>` correctly names image1, but a blip planted in `<p:cNvPr><a:hlinkClick>` comes first in document order and wins the DFS. **rId1 is never drawn.** 3 290-byte deck.

An attacker who can substitute one relationship in a deck controls what appears on the projector while the visible XML still names the legitimate image.

---

## FINDING 5 — SEV-2 · VERIFIED · Layout and master selection is nondeterministic: the same deck renders differently on different launches

`deck_loader.cpp:1033-1071` iterates `slideRels` — a `QHash` — and takes the first entry whose **Target string contains** `"slideLayout"`; `:1053-1059` does the same for `"slideMaster"`. Qt 6 randomises the hash seed per process, so when more than one rel matches, which layout supplies the background *and* all placeholder geometry is a coin flip per launch. Twelve consecutive runs of each deck, most-common pixel:

```
i01_rel_substring_confusion    [((255,255,255), 6), ((0,255,0), 6)]     # bg lost / kept
i02_master_substring_confusion [((0,255,0), 7), ((255,255,255), 5)]     # master bg lost / kept
f05_two_layouts                [((255,0,0), 6), ((0,0,255), 6)]         # layout1 / layout2
```

`f05` is the two-layout case. `i01`/`i02` are the realistic trigger and need no malformed XML at all: the match is on a *substring of the Target*, so an ordinary image relationship pointing at `../media/slideLayout-logo.png` is accepted as the slide's layout, and `../media/slideMaster-bg.png` as the layout's master. Half the time the deck then loses its background and every placeholder rectangle, with no warning. Match on the relationship **Type** URI, and iterate in document order.

---

## FINDING 6 — SEV-3 · VERIFIED · A negative `<a:alphaModFix amt>` makes the picture 100 % invisible

`deck_loader.cpp:702` does `qBound(0, parsed, 100000)`, so any negative `amt` becomes 0 → `p.setOpacity(0.0)`. `amt` is `ST_PositivePercentage`; a negative value is out of schema, and the loader's own stated invariant three lines below is *"an unreadable opacity must not be able to hide a picture"* — an unreadable one cannot, a readable out-of-range one can.

```
b01_alpha (amt="-2147483648")  [((255,255,255),57600)]   # picture completely gone, no warning
b02_alpha (amt="-1")           [((255,255,255),57600)]   # ditto
b05_alpha (amt="1e30%")        RED + "warning: unreadable opacity; drawing it opaque"   # correct
b07_alpha (amt="50%")          [((255,255,255),49226), ((255,129,129),8008)]            # correct
```

`amt="-0.000001%"` (`b10`) also renders fully invisible. Treat out-of-range like unparseable: opaque + warning.

## FINDING 7 — SEV-3 · VERIFIED · `static_cast<int>(NaN)` UB from `<a:rPr sz="nan">`

`slide_renderer.cpp:31` `clampFontPx()`; `std::clamp` propagates NaN (all comparisons false) and the cast is UB.

```
$ ./build-ubsan/render_preview decks/l01_sz_nan.pptx /tmp/o 640 360
slide_renderer.cpp:31:29: runtime error: nan is outside the range of representable values of type 'int'
    #0 …drawTextBox(…)::$_0::operator()(pptv::TextRun const&) slide_renderer.cpp:111
```

Reproduced by `l01` (`sz="nan"`), `l02` (`sz="NaN"`), `l14`. `deck_loader.cpp:307-309` accepts `sz.toDouble()` with no validation. On arm64 the cast yields 0 and QFont ignores it; on x86-64 the same cast yields `INT_MIN`. The master-styles path (`m01`-`m03`) is accidentally safe because `resolveDefaultSizePt` filters on `> 0.0`.

## FINDING 8 — SEV-3 · VERIFIED · Any degenerate destination frame drops the picture with no placeholder and no warning

`slide_renderer.cpp:293-345`: `if (decoded.isNull()) { placeholder } else if (r.width() >= 1 && r.height() >= 1) { draw }` — **there is no `else`**. An undecodable image gets the grey placeholder; a decodable image in a bad frame gets silence. All of these render an empty slide with an empty warning list:

```
e01_ext_zero      <a:ext cx="0" cy="0"/>                        NOTHING
e02_ext_negative  <a:ext cx="-4000000" cy="-3000000"/>          NOTHING
e05_ext_overflow  cx="99999999999999999999" (toLongLong fails)  NOTHING
e06_ext_1         cx="1" cy="1" (sub-pixel destination)         NOTHING
e10/e11           the same, with <a:stretch>                    NOTHING
a04/a06/a15       crop that legitimately resolves to nothing    NOTHING
```

This is BUG-57's defect class on the **destination** side: BUG-57 established that a sub-pixel *source* is still real content and must be rounded outward; the *destination* guard still deletes silently. Route every non-draw through `drawPlaceholderBox` + a `LoadWarning`, the same as a missing image.

## FINDING 9 — SEV-3 · VERIFIED · An out-of-range `<a:pPr lvl>` silently deletes the paragraph

`deck_loader.cpp:339` `para.indentLevel = attrLocal(pPr,"lvl").toInt()` is unbounded (`ST_TextIndentLevelType` is 0-8). `slide_renderer.cpp:104` `indent = para.indentLevel * indentUnit` then pushes the line off the clip rect.

```
l10_lvl_intmax  (lvl="2147483647")   non-white px = 0     # text entirely gone
l11_lvl_intmin  (lvl="-2147483648")  non-white px = 0
l12_lvl_nonnum  (lvl="abc")          non-white px = 1770  # toInt fails -> 0, fine
```

## FINDING 10 — SEV-4 · VERIFIED · A present-but-non-positive `sz` still renders 1 px

BUG-8 fixed *absent* `sz`; a present non-positive one still lands on `clampFontPx`'s floor of 1. `l09` (`sz="0"`), `l08` (`sz="-4400"`), `l04`/`l06` (`±inf`) all render 31 non-white pixels against 1 770 for the same text at its normal size — invisible, no warning. Same symptom BUG-8 exists to eliminate.

---

## What held up (negative results worth recording)

- **Media abuse** — 0-byte, truncated mid-IDAT, header-only, truncated IHDR, PNG/JPEG magic + 4 KB of random bytes, and a 1×1 declaring 30000×30000 / `0x7FFFFFFF²` / 65535 wide: **all ten produced the grey "missing image" placeholder**, no decoder crash, no over-read. The magic-byte allow-list and `kMaxImagePixels` both do their job.
- **Deep nesting** — `<p:grpSp>` nested 40 / 5 000 / **100 000** deep: capped at depth 32 with the documented warning, no stack overflow, 0.09 s. 50 000 nested elements inside `<p:nvSpPr>` (the `descendantLocal` DFS) also fine — the explicit heap stack works.
- **Self-referential rels** — layout→itself, layout's master→the layout, slide's layout rel→the slide: no loop, no crash.
- **Geometry extremes** — `cx=LLONG_MAX`, `off=LLONG_MIN`, `sldSz` of 0 / 1 / `LLONG_MAX`: no hang, no UB, correct warning for the zero slide size.
- **Percentage spellings** — `"1e30%"`, `"NaN%"`, `"inf%"`, `"%"`, `"9999999999"`, `""`, `" 50000 "`, `"-50%"`: `parsePercentAttr` classifies every one correctly and emits the right warning. This part of the BUG-47/48/52 work is solid.
- **Mutation fuzz** — 900 mutants (raw byte flips + per-part XML corruption) across 6 seeds through the ASan+UBSan render path: **0 hits**. The zip layer, pugixml walk and caps survive random corruption; every sanitizer hit in this session came from structured input, not from malformed bytes.

Recommended fix order for a talk in 5 days: **F1** (the opening slide of the actual deck loses its photograph), then **F4** and **F5** (wrong / random rendering with no signal), then **F2**, **F3**, **F8**.