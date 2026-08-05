UAT-4 STRUCTURAL LOAD REPORT — real deck, 10 slides, `src/loader/deck_loader.cpp` @ cd11cf8

NOTE ON TREE STATE: I made no writes. Mid-session the orchestrator moved the checkout from `main` (cd11cf8) to branch `walk/uat4-state` (89004de); `git show --stat 89004de` = docs only (`WALK-STATE.md`, `.claude/process-state.json`). Code under test is unchanged. `git status --porcelain` = empty. (One correction of my own: `tests/fixtures/generate_fixtures.py`'s `write_zip()` hardcodes `HERE`, so importing it dropped `tests/fixtures/shapefill.pptx` into the repo. I deleted it within the same minute and re-verified the tree clean; all later fixtures were written with a local `zipfile.ZipFile`.)

HARNESS: `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4/`
- `dump_structure.cpp` → geometry/srcRect/alpha/stretch/background dump (prints no text, no alt-text, no pixels)
- `count_xml.py` → mirrors `processShapeTree`'s walk over each slide's XML
- `pixstat.cpp` → pixel histogram, run ONLY on synthetic fixtures, never on the real deck
Build: `cmake -S . -B .../uat4/build -G Ninja -DCMAKE_BUILD_TYPE=Release` then linked against `build/libpptv_core.a`.

═══ PER-SLIDE DATA (VERIFIED — `./dump_structure "<deck>.pptx"`, full output in `dump_real.txt`) ═══

Deck: 12192000 x 6858000 EMU, 10 slides, **0 warnings**.

| Sl | els | background (all SOLID) | source | images |
|---|---|---|---|---|
| 1 | 5 | `373F51` | layout1 | 3 (see below) |
| 2 | 2 | `0076A3` | layout2 | – |
| 3 | 2 | `EAF0F6` | layout14 | – |
| 4 | 2 | `FFFFFF` | layout8 `schemeClr=bg1` | – |
| 5 | 21 | `FFFFFF` | **master** (layout13 declares none) | – |
| 6 | 7 | `FFFFFF` | **master** | – |
| 7 | 3 | `FFFFFF` | **master** | – |
| 8 | 2 | `0076A3` | layout2 | – |
| 9 | 3 | `EAF0F6` | layout4 | 1 |
| 10 | 4 | `0098D1` | layout3 | 1 |

Images:
```
S1 E01 rect=(0,0 0x0)                          image10.jpeg 198219B srcRect=(l=29178 t=0 r=29178 b=0)      alpha=100000 stretch=1
S1 E03 rect=(3163308,2238091 6529146x4619910)  image11.emf    2188B srcRect=(l=-6726 t=0 r=-1 b=36909)     alpha=70000  stretch=1
S1 E04 rect=(3163308,2238091 6529146x4619910)  image11.emf    2188B srcRect=(l=-6726 t=0 r=-1 b=36909)     alpha=70000  stretch=1
S9 E01 rect=(8769927,956691 3418206x5217906)   image12.jpeg 141880B srcRect=(l=31593 t=-190 r=40879 b=190) alpha=100000 stretch=1
S10 E01 rect=(9120359,6173717 2182361x305351)  image2.png     5747B srcRect=IDENTITY                       alpha=100000 stretch=1
```

═══ THE CORE COMPARISON: declared vs loaded ═══

**VERIFIED — zero count discrepancies.** Declared totals 51, loaded 51, delta 0 on every slide (5/2/2/2/21/7/3/2/3/4 both sides). No `<p:sp>` lacks `<p:txBody>`; no unknown tags; no `mc:AlternateContent`; slide 10's `grpSp` has `chOff==off` and `chExt==ext`, so the un-applied group transform is a no-op here. **The count-parity check this task was built around comes back clean.** The picture loss below is invisible to it — the element is present in the vector, with a rect of zero.

═══ SEV-1 — slide 1's hero photograph loads at 0x0 and is drawn as nothing. BUG-41 is not fixed on the real deck; BUG-58 broke it. ═══

`dump_structure` line 1 above: `S1 E01 rect=(0,0 0x0)`, 198219 bytes of valid JPEG attached, **zero warnings on the whole deck**.

Root cause, VERIFIED. `placeholderKey()` (`deck_loader.cpp:537-566`) namespaces the key by the non-visual-properties holder. The two sides of the match disagree on holder:
- slide1.xml declares it `<p:pic>` → holder `nvPicPr` → key `nvPicPr|pic:11`
- **slideLayout1.xml declares the very same placeholder as `<p:sp>`** → holder `nvSpPr` → key `nvSpPr|pic:11`, rect `(8017727,-1 4282068x6858001)`

The keys can never be equal, so `layoutPh.contains(key)` is false and the rect stays 0x0. Slide 9's picture only escapes because it happens to carry an inline `xfrm` — it never needed inheritance, so BUG-59's "already fixed by BUG-41" is untested.

Isolated repro (differential fixture: byte-identical to the committed one except the layout's `<p:pic>`→`<p:sp>`):
```
$ ./dump_structure real_shape_layout_sp.pptx
  E00 image rect=(0,0 0x0) media=ppt/media/image1.png bytes=69 ...
$ ./dump_structure tests/fixtures/good_pic_placeholder.pptx      # committed control
  E00 image rect=(8017727,0 4174273x6858000) media=ppt/media/image1.png bytes=69 ...
```
Pixel proof on those synthetic fixtures:
```
$ ./pixstat real_shape_layout_sp.pptx     → distinct colours=1   #FFFFFF : 1440000   (100% blank)
$ ./pixstat good_pic_placeholder.pptx     → distinct colours=3   #FF0000 : 492300    (picture drawn)
```
`slide_renderer.cpp:294-296`: `decoded.isNull()` is **false** (the JPEG decodes fine), so the `missing image` placeholder branch is skipped; then `r.width() >= 1 && r.height() >= 1` is false, so nothing is drawn and nothing is reported. A decodable image at 0x0 is the one case that produces neither a picture nor a placeholder nor a warning — an undecodable image at 0x0 at least gets a box.

Why the suite is green: `tests/test_deck_loader.cpp:555` pins the fixture whose **layout** side is `<p:pic>`. The test's own comment describes the real deck accurately on the slide side ("a `<p:pic>` carrying `<p:ph type="pic" idx="11"/>` and NO `<p:spPr>`") but the fixture's layout side does not match the deck it was written for. `ctest` = **224/224 pass** while the opening slide's photograph is absent. This is exactly BUG-55's open complaint ("the committed fixture is not the shape the real deck has") — still true, and it is now load-bearing, not cosmetic.

═══ SEV-1 — slide 5: 10 runs of white text render white-on-white, because autoshape fills are discarded with no warning ═══

`struct TextBox { RectEmu rect; std::vector<Paragraph> paragraphs; };` (`slide_model.hpp`) has no fill/outline/geometry field, so `<a:solidFill>`/`<a:ln>`/`<a:prstGeom>` on a `<p:sp>` are structurally unrepresentable and silently dropped. Slide 5's XML (attribute values only):
```
E01/E05/E09/E13/E17  prstGeom=roundRect  fill=srgbClr=373F51  runColours=['srgbClr=FFFFFF','srgbClr=FFFFFF']
E03/E07/E11/E15      prstGeom=rightArrow fill=srgbClr=0098D1  runColours=[]
E04/E08/E12/E16/E19  prstGeom=diamond    fill=srgbClr=FFFFFF  runColours=['srgbClr=373F51'] (E19: [])
```
Slide 5's background resolves to `FFFFFF`. `slide_renderer.cpp` `colorFor()` uses the run's **explicit** colour when present, so `defaultTextColor`'s luminance fallback never engages: 5 × 2 = **10 white runs painted on white**. The 5 cyan `rightArrow` connectors carry no runs at all and become literally nothing.

Repro on a synthetic mirror (`shapefill.pptx`: one `roundRect` fill `373F51` + white run, one `rightArrow` fill `0098D1`, bg `FFFFFF`):
```
$ ./dump_structure shapefill.pptx
  E00 text rect=(572871,1783080 1828800x1024128) paras=1 runs=1
  E01 text rect=(2429103,2212848 420624x164592) paras=1 runs=0
  (warnings=0)
$ ./pixstat shapefill.pptx
  distinct colours=1    #FFFFFF : 1440000
```
A fully blank slide. Note the Manifesto's F1 contract is "unsupported element → visible placeholder + triage-shaped load-time warning list (never a silent wrong render)". A filled autoshape is classified as a plain text box, so it gets no placeholder, no warning, and an actively wrong render. Slide 5 is the deck's five-step process diagram; only the `373F51` body copy survives.

═══ SEV-2 — `<mc:AlternateContent>` is dropped whole, silently ═══
Not present in the deck today, so it costs nothing now; it is a live risk because the presenter is still editing for ~5 days, and PowerPoint emits it for modern chart types, ink, 3D models and some effects. `processShapeTree` matches only `sp`/`pic`/`grpSp`/`graphicFrame`/`cxnSp`; `mc:AlternateContent` falls off the `else if` chain, taking both `<mc:Choice>` and `<mc:Fallback>` with it.
```
$ ./dump_structure altcontent.pptx     # 1 chart in AlternateContent + 1 keeper sp
DECK ... slides=1 warnings=0
SLIDE 1 elements=1 ...                 # expected 2
```
Same shape for `<p:sp>` with no `<p:txBody>` (`sp_no_txbody.pptx`): a full-width red bar declared, `elements=1`, `warnings=0`.

═══ SEV-3 (BUG-39, already open — new detail) ═══
Slide 1 references `image11.emf` from **two identical `<p:pic>` elements** (same `rId4`, same rect, same crop, same `amt=70000`) — that duplication is the deck's own. `QImageReader::supportedImageFormats()` on this Qt build: `bmp cur gif heic heif icns ico jfif jp2 jpeg jpg mng pbm pdf pgm png ppm svg svgz tga tif tiff wbmp webp xbm xpm` — **no emf/wmf**. So both decode null and both draw the grey "missing image" box at `(3163308,2238091 6529146x4619910)` = 53.6% × 67.4% of the slide, stacked, over the centre-right of the title slide. Confirmed against `tests/fixtures/good_emf_image.pptx`: `./pixstat` → `#C8C8C8 : 195180` px, matching the computed box area.

═══ THE HARD QUESTIONS, ANSWERED ═══
- **Pictures still 0x0 or absent?** One: slide 1 E01 (SEV-1). No image has empty `imageData`.
- **Crops resolving to identity where XML declares insets?** No. All four declared `srcRect`s round-trip exactly (`29178/29178`; `-6726/-1/36909`; `31593/-190/40879/190`), including the negative and sub-pixel values. BUG-47/48/52/57 hold on this deck. Slide 10's `<a:srcRect/>` is genuinely attribute-less, so identity is correct there. Slide 1 E03/E04's `l=-6726` is clamped by `intersected(whole)` rather than padded — that is BUG-54, known open, and moot since the EMF cannot decode.
- **Does every slide resolve a background?** Yes, all 10 SOLID, and all 10 **verified correct against the XML chain**: 7 from the layout, and slides 5/6/7 from the **master** via `schemeClr=bg1`→`lt1`=`FFFFFF` (layout13/11/12 declare none). BUG-32's slide→layout→master walk is genuinely working.
- **Silently dropped elements?** Not by count on this deck — 51/51. The dropped things are *sub-element*: every autoshape fill, outline and preset geometry, and (latently) anything inside `mc:AlternateContent`.

═══ BOTTOM LINE ═══
The deck does **not** load correctly. Count parity is clean and all 224 tests pass, which is precisely why both SEV-1s are still open: one hides inside a present-but-zero-sized element, the other inside a present-but-unstyled one. Neither emits a warning — the deck reports `warnings=0` end to end. Slide 1 opens the talk missing its photograph and showing two grey error boxes; slide 5 shows a blank white area where the five-step diagram should be.

TL;DR — I loaded the presenter's real deck and checked the numbers, never the words or pictures. Good news: nothing is being lost by the count — all 51 shapes on all 10 slides come through, all 10 slide background colours are right, and all the image-cropping fixes from last round work correctly. Bad news: two things will visibly break the talk. On slide 1 the main photograph loads with a size of zero and is simply never drawn — the fix for this was tested against a sample file that doesn't match how the real deck is actually written, so the test passes while the real slide is broken. And on slide 5, the app throws away the coloured boxes behind shapes but keeps their text — the text on that slide is white, so it lands on a white background and disappears; that slide comes out blank. Neither problem produces any warning, so nobody would find out until the presenter is standing in front of the room.