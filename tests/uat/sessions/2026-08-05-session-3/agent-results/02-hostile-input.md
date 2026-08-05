# Agent Result — hostile-input (UAT Session 3)

**Summary:** Drove 27 hand-built hostile .pptx families through both ./build/render_preview and the real app binary. The archive-level defenses are genuinely good: zip bombs, lying size fields, zip-slip/traversal, duplicate names, entity/XXE bombs, 100 MB attribute floods, non-images, absurd image dimensions, nested archives, zero/negative/int64-max slide sizes, relationship cycles and 150k-entry archives were all contained, and the C5 media read-amplification fix HOLDS (a 100 MB media part referenced by 2000 <p:pic> peaks at 149 MB RSS in 0.3 s, and the cumulative cap is charged once per distinct part). The app never wrote a single file to disk in any run, and never printed the deck's file path to stdout, stderr or a dialog — describeLoadError() correctly shields the UI from LoadError::message.

Two real breaks, one of them fatal. (1) SEV-1: processShapeTree() still recurses on <p:grpSp>, and the per-slide shape cap cannot see nested empty groups, so nesting depth is unbounded. A 5,713-byte, structurally normal 10-slide deck with one poisoned slide kills the app instantly with SIGBUS — no dialog, no stderr, no crash report. This is the same class of bug as audit F1a-2, which was fixed for descendantLocal but missed on this path. (2) SEV-2: the 1 GB cumulative decompression cap is charged from the central directory once per entry, but slide XML, slide rels and slideLayout parts are re-read per slide with no cache (the C5 cache covers media only). A 264 KB archive that declares 254 MB uncompressed makes the loader decompress ~76 GB, producing a 61-second load that has no cancellation hook at all. Plus a SEV-3 TM-012 leak: libpng writes deck-derived bytes straight to the app's stderr, bypassing Qt's logging categories.

Peak RSS never exceeded 448 MB in any test, so memory exhaustion is not a live risk — the failures are crash and wall-clock, not RSS. All work was done in the scratchpad; the repository is unmodified (git status --porcelain is empty) and the Confidential deck was never touched.

## Findings (4)

### [SEV-1] Unbounded recursion on nested <p:grpSp> stack-overflows the load worker: a 5.7 KB deck kills the app instantly with no error

**Repro:** Generator: /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/hostile/gen.py

Build the realistic attack deck (10 slides, only slide 7 poisoned with 1000 nested <p:grpSp>, 5,713 bytes):
  python3 -c "import gen; slides=[]\nfor i in range(1,11):\n  b='<p:grpSpPr/>'+gen.text_sp('Q3 Results')\n  if i==7:\n    for _ in range(1000): b=f'<p:grpSp><p:grpSpPr/>{b}</p:grpSp>'\n  else: b=gen.text_sp(f'Slide {i}')\n  slides.append(gen.slide_xml(b))\ngen.write_zip('a24_realistic_10slide_poison.pptx', gen.base_parts(10, slides))"

Real app binary:
  QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice .../decks/a24_realistic_10slide_poison.pptx
  -> exit=-10 (SIGBUS), wall=0.0s, peak RSS 20 MB, stdout EMPTY, stderr EMPTY

Headless tool:
  ./build/render_preview .../decks/a01_grpsp_deep_100000.pptx /tmp/out
  -> exit=-11 (SIGSEGV), wall=0.2s, peak RSS 44 MB (9,295-byte file)

Measured thresholds (bisected):
  app / load worker thread (512 KB pthread stack): survives depth 670, SIGBUS at depth 680
  render_preview / main thread (8 MB stack):       survives depth 10,500, SIGSEGV at depth 11,000
  ratio 10,750/675 = 15.9 ~= 16 = 8 MB / 512 KB  -> conclusive stack exhaustion, ~760 bytes/frame

Isolation control (proves it is the grpSp branch specifically, not pugixml or the XML walkers):
  <a:x> nested 100,000 deep inside <p:bgPr> -> exit=0, wall=0.0s, 33 MB RSS (handled by the ITERATIVE descendantLocal)
  tests/fixtures/deep_nest.pptx (depth 3000 of <a:x>) -> exit=0, loads fine
Only the <p:grpSp> path dies.

**Impact:** Karl double-clicks a deck that arrived by email or USB and the application vanishes. Not a dialog, not a black screen, not a placeholder slide — the process is gone before onDeckLoaded ever runs, so QMessageBox::warning at app_shell.cpp:112 is never reached and nothing is printed to stdout or stderr. On stage this is unrecoverable within the talk: there is no error to react to and no state to fall back on, and relaunching with the same file reproduces it every time. The trigger file is 5.7 KB and structurally a completely normal 10-slide deck, so nothing about it looks suspicious before opening. It also defeats the entire TM-018 architecture: the crash happens during DeckLoader::load on the load worker thread, long before the pre-render worker's PREVENT caps get to measure anything, and a stack overflow on a secondary thread takes the whole process down regardless of which thread it happened on.

**Fix:** src/loader/deck_loader.cpp:471-475 — the grpSp branch calls processShapeTree() recursively with nothing bounding depth. The guard at line 436 tests slide.elements.size() against lim.maxShapesPerSlide, but a chain of empty <p:grpSp> adds zero elements, so the cap never fires no matter how deep the nesting goes.

Two options, both cheap:
(a) Preferred, and consistent with the fix already applied to descendantLocal for audit F1a-2: replace the recursion with an explicit heap-allocated work stack, pushing group children instead of calling into itself.
(b) Minimum viable: thread an `int depth` parameter through processShapeTree and stop descending past a hard limit (64 is far above anything PowerPoint itself produces), emitting a LoadWarning with elementType "group-depth" so the behaviour is visible rather than silent.

Either way, add a fixture at depth 100,000 alongside tests/fixtures/deep_nest.pptx — the existing deep_nest fixture only exercises <a:x>, which is why this survived the F1a-2 audit. Note that app_shell.cpp:95-104 never calls QThread::setStackSize(), so the load worker gets macOS's 512 KB default; raising that would only move the threshold, not remove the bug, and should not be treated as the fix.

### [SEV-2] Cumulative decompression cap is charged per zip entry but parts are re-read per slide: a 264 KB deck forces ~76 GB of decompression and a 61-second load that cannot be cancelled

**Repro:** Build the amplifier (300 <p:sldId> whose relationships all point at ONE 127 MB slide part, whose rels point at ONE 127 MB slideLayout):
  python3 -c "import gen\nMB=127\nbig_slide=gen.XML_DECL+f'<p:sld xmlns:a=\"{gen.A}\" xmlns:r=\"{gen.R}\" xmlns:p=\"{gen.P}\">'+'<!--'+'z'*(MB*1024*1024)+'-->'+'<p:cSld><p:spTree>'+gen.text_sp('amp')+'</p:spTree></p:cSld></p:sld>'\nbig_layout=gen.XML_DECL+f'<p:sldLayout xmlns:a=\"{gen.A}\" xmlns:r=\"{gen.R}\" xmlns:p=\"{gen.P}\">'+'<!--'+'y'*(MB*1024*1024)+'-->'+'<p:cSld><p:spTree/></p:cSld></p:sldLayout>'\nparts={'[Content_Types].xml':gen.content_types(1),'_rels/.rels':gen.root_rels(),'ppt/presentation.xml':gen.presentation_xml(300),'ppt/_rels/presentation.xml.rels':gen.presentation_rels_all_to(300,'slides/slide1.xml'),'ppt/slides/slide1.xml':big_slide,'ppt/slideLayouts/slideLayout1.xml':big_layout,'ppt/slides/_rels/slide1.xml.rels':gen.XML_DECL+f'<Relationships xmlns=\"{gen.PR}\"><Relationship Id=\"rIdL\" Type=\"{gen.R}/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/></Relationships>'}\ngen.write_zip('a22_combined_amp.pptx',parts)"

  ./build/render_preview .../decks/a22_combined_amp.pptx /tmp/out
  -> exit=0, wall=66.7s, peak RSS 425 MB. Archive on disk: 263,690 bytes.

Matched control to isolate loader cost from render+PNG-save cost (300 slides, same render work, tiny layout):
  ./build/render_preview .../decks/a11_layout_reread_300x0mb.pptx /tmp/out -> wall=5.9s, 44 MB
  => ~60.8 s is pure DeckLoader::load re-read cost.

Single-vector variants:
  a10 (slide part only, 300 x 100 MB, 106,040-byte file) -> wall=30.3s, 244 MB (~24s load)
  a11 (layout only,     300 x 100 MB, 328,817-byte file) -> wall=29.1s, 245 MB (~23s load)

Real app binary: QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice .../a22_combined_amp.pptx
  -> no crash, survives; the deck simply does not appear for ~a minute. No files written anywhere.

Arithmetic: the archive DECLARES 254 MB uncompressed (well under maxTotalUncompressed = 1 GB) but the loader actually decompresses 300 x (127 MB + 127 MB) ~= 76 GB — a ~300x bypass of the zip-bomb cap.

**Impact:** This is the pre-show failure mode. Karl opens the deck and gets a start view that sits there doing nothing for a full minute with no progress indication, on a stage, in front of an audience. His natural reaction — force-quit and try again — costs another minute, and the second attempt behaves identically. Worse, the load is completely un-cancellable: DeckLoader::load takes no cancellation token and checks no flag anywhere in its body, and DeckLoadWorker::start only tests cancelled_ before the parse begins and after it returns (deck_load_worker.cpp:56 and 90). So once the read amplification is under way, nothing stops it. If he tries to quit or open a different deck, AppShell::teardownWorkers cancels, waits 5 seconds, and then calls QThread::terminate() (app_shell.cpp:80-83) on a thread suspended inside libzip inflate or pugixml allocation — killing a thread that may hold the malloc lock, which can wedge the process outright rather than exiting cleanly. Peak RSS stays at 425 MB, so this never OOMs; it is purely a wall-clock and cancellability denial of service. The security framing matters too: the TM-014/TM-017 zip-bomb defence is the control that is supposed to make this impossible, and it is bypassed by a factor of 300 without ever tripping.

**Fix:** src/loader/deck_loader.cpp. The cap loop at lines 561-590 walks the central directory and charges st.size once per ENTRY, but nothing charges per READ. Three call sites re-read unboundedly inside the per-slide loop: the slide part at line 711, the slide rels at line 724, and the slideLayout at line 735. The audit C5 fix at lines 762-776 solved exactly this problem for media parts via mediaCache + the mediaBytes counter — the same treatment simply was not extended to the XML parts.

Recommended: hoist the caching one level up so it is structural rather than per-call-site. Give readPart a small QHash<QString, QByteArray> cache keyed by part name and a running `bytesRead` budget checked on every cache MISS, then charge every part read through it (slide, rels, layout, theme, master, media alike). That removes the amplification and makes the cumulative cap mean what its comment claims. Cheaper interim: cache parsed layouts in a QHash<QString, QHash<QString,RectEmu>> keyed by layoutPart, and cache slideXml by slidePart, which kills the 300x multiplier for the two dominant vectors.

Separately and independently worth doing: give DeckLoader::load a `const std::atomic<bool>& cancelled` parameter checked at the top of the per-slide loop, so a long load is abortable and teardownWorkers never has to reach QThread::terminate().

### [SEV-3] libpng writes deck-derived bytes straight to the app's stderr, bypassing Qt's logging categories (TM-012/TM-013)

**Repro:** Craft a media part whose PNG chunk-type field carries recognisable content, then open it with the REAL app binary:
  python3 -c "import gen\npayload=b'ACQUISITION-TARGET-ACME-CORP-PRICE-42M'\nbig=b'\\x89PNG\\r\\n\\x1a\\n'+b'\\x00\\x00\\x00\\x10'+payload[:4]+payload\nparts=gen.base_parts(1,has_png=True)\nparts['ppt/media/image1.png']=big\nparts['ppt/slides/slide1.xml']=gen.slide_xml('<p:pic><p:blipFill><a:blip r:embed=\"rIdImg\"/></p:blipFill><p:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"3000000\" cy=\"3000000\"/></a:xfrm></p:spPr></p:pic>')\nparts['ppt/slides/_rels/slide1.xml.rels']=gen.XML_DECL+f'<Relationships xmlns=\"{gen.PR}\"><Relationship Id=\"rIdImg\" Type=\"{gen.R}/image\" Target=\"../media/image1.png\"/></Relationships>'\ngen.write_zip('a26_content_to_stderr.pptx',parts)"

  QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice .../decks/a26_content_to_stderr.pptx
  -> stderr: libpng error: ACQU: CRC error

The four bytes 'ACQU' are read verbatim out of the media part inside the deck.

Second instance with random media bytes (a25 deck):
  -> stderr: libpng error: [A9]Q[FC][9E]: bad header (invalid type)

Note the two distinct channels: Qt's own decoder messages are tagged 'qt.gui.imageio:' and ARE suppressible via QLoggingCategory, but the bare 'libpng error:' line comes from libpng's default error handler writing to stderr directly and is not filtered by anything.

**Impact:** Project Bible section 8 / TM-012 / TM-013 require that deck CONTENT never reach a log or any persisted artefact. This is a live channel that does exactly that. The bytes are limited (a chunk-type field, so 4 bytes per malformed chunk, and only for images that fail to decode) so it is not a bulk exfiltration path, and it does not reach the projector. But for a .app launched from Finder — which is how Karl will actually start it — stderr is captured by the macOS unified logging system and persisted to disk outside the application's control, which is precisely the outcome the constraint exists to prevent. The same mechanism applies to Karl's real Confidential deck, not just to a hostile one: any image in it that trips a libpng warning path emits bytes from that image into the system log.

**Fix:** src/render/slide_renderer.cpp:41-59, decodeGuarded(). The allow-list and the 40 Mpx / 128 MiB bounds are doing their job — the images are correctly rejected and a placeholder is drawn — but the codec is still invoked on attacker bytes and is allowed to narrate its failures to stderr.

Install a qInstallMessageHandler in main.cpp that drops (or redacts to a fixed string) anything in the qt.gui.imageio category, and suppress libpng's own handler — the practical route is to validate the PNG/JPEG header structure yourself before handing the buffer to QImageReader, so structurally broken images never reach libpng at all. Given decodeGuarded already parses enough to check format() and size(), extending it to sanity-check chunk structure is a small addition and has the side benefit of rejecting malformed images faster.

### [SEV-4] Render path has no decoded-image cache, so one media part is decoded once per <p:pic> that references it (overlaps known-deferred BUG-21)

**Repro:** 3,557-byte deck: 2000 <p:pic> elements all referencing a single 4000x4000 PNG media part.
  python3 -c "import gen; gen.a12_pic_amplify(2000, 4000)"
  ./build/render_preview .../decks/a12_pic_amplify_2000.pptx /tmp/out
  -> exit=0, wall=8.6s for ONE slide, peak RSS 47 MB

Contrast with the LOAD side, which is correctly fixed (audit C5) — this is the verification you asked for, and it HOLDS:
  100 MB media part referenced by 2000 <p:pic> (a25, 109,955-byte file)
  -> exit=0, wall=0.3s, peak RSS 149 MB
  Pre-C5 this would have been 2000 x 100 MB = ~200 GB resident. The mediaCache keyed by part name shares the QByteArray copy-on-write, and mediaBytes charges the cumulative cap exactly once per DISTINCT part — both behaviours confirmed.

Shape count 2000 is deliberate: RenderCaps::maxShapesPerSlide is 2000 and exceedsCaps() tests `>`, so exactly 2000 passes the PREVENT gate and enters the renderer. The loader's own maxShapesPerSlide is 5000, so all 2000 pics survive parsing.

**Impact:** 8.6 seconds of pre-render for a single slide. Because rendering is off-thread (the ISOLATE leg of TM-018), the UI thread does not block and Karl can still drive the deck — the affected slide simply is not ready when he arrives at it, and he gets a placeholder or a stale raster instead of his content. On a 10-slide deck the practical consequence is one or two slides appearing late rather than a broken talk, which is why this is SEV-4 and not higher. Worth flagging honestly: this is an instance of the general class described by known-deferred BUG-21 (the TM-018 caps count shapes and text runs, not actual work), so triage should decide whether to fold it into BUG-21 rather than track it twice. I am reporting it separately only because the specific mechanism is narrow and the fix is a near-copy of one already in the tree.

**Fix:** src/render/slide_renderer.cpp:240 calls decodeGuarded(e.image.imageData) inside the per-element z-order loop with no memoisation, so N references to one part cost N full decodes.

Mirror the load-side C5 fix on the render side: add a QHash<QString, QImage> keyed by e.image.mediaPart, local to SlideRenderer::render, and reuse the decoded QImage across elements within a single slide render. QImage is implicitly shared so the cache costs one decode and one buffer regardless of reference count, and the change is contained entirely within the Image case of that switch. If a per-deck cache is wanted later it can be lifted into the pre-render worker, but per-slide alone removes this amplifier.

## Could not break

- ZIP BOMBS — a02: 12 highly-compressible parts declaring 100 MB each (1.2 GB total) in a 1,226,180-byte archive. Rejected in 0.0s at 18 MB RSS with LoadErrorKind::DecompressionLimit before any part was read. The central-directory pre-scan at deck_loader.cpp:561-590 does its job for DISTINCT entries; the only way past it was re-reading the same entry (SEV-2 above).
- PARTS THAT LIE ABOUT THEIR SIZE — four hand-patched variants (both central directory AND local header rewritten): declared 1 KB with 64 MB actual; declared 127 MB with 1 MB actual; a slide XML part declared 4 KB with 32 MB actual; and eight parts each declaring 127 MB (1016 MB total, deliberately just under the 1 GB cap) with 1 KB actual. All safe: 0.2-0.5s, 18-43 MB RSS. readPart() reads exactly st.size bytes and rejects on the got != st.size mismatch, so a short-declaring part truncates cleanly and a long-declaring part cannot over-read. The truncated slide XML correctly failed as MalformedXml rather than parsing garbage.
- OVER-ALLOCATION VIA LYING SIZE, REFERENCED — a23: repeated the 8 x 127 MB lie but wired every part to a real <p:pic> so readPart() and its out.resize(st.size) were actually reached. Peak RSS 43 MB. The resize is bounded by maxPartUncompressed and the pages are never touched because zip_fread writes only the real byte count, so there is no memory amplification here.
- ZIP-SLIP / PATH TRAVERSAL — a04: relationship targets '../../../../../../tmp/pwned_slide.xml' and '/etc/passwd', plus matching zip entries with those literal names. Fully neutralised. resolveTarget() (deck_loader.cpp:89-104) collapses '..' segments and strips the leading '/', and — the important part — the result is only ever used as a zip entry name for zip_stat/zip_fopen. The loader never touches the filesystem for deck content at all, so there is no traversal primitive to exploit in either direction. '/etc/passwd' resolved to the in-archive entry 'etc/passwd', never the real file. Confirmed zero files created anywhere.
- DUPLICATE PART NAMES — a05: two entries named ppt/slides/slide1.xml (benign then hostile) and two named ppt/presentation.xml. Loaded cleanly in 0.2s at 43 MB. libzip resolves the name consistently between the cap scan and the read, so there is no scan-one-entry / read-the-other split.
- 300 SLIDES DECLARED, NONE PRESENT — a06: 300 <p:sldId> with no slide parts in the archive. Produced 300 placeholder slides with numbering preserved and 300 warnings, 6.1s, 44 MB. The audit F1a-3 anti-index-drift behaviour holds — 'go to slide N' would still land correctly. a07 at 301 slides was rejected outright with TooManySlides in 0.0s.
- XML ENTITY BOMBS AND XXE — a09: an eight-level billion-laughs expansion plus <!ENTITY xxe SYSTEM "file:///etc/passwd"> in the slide part. Completely inert, 0.2s at 43 MB. pugixml does not expand custom entities and has no external-entity resolution, so neither the expansion nor the file read occurs.
- ENORMOUS XML — a08: ~100 MB of attributes (2.6 million of them) on one element, in a 6,589,307-byte archive. 0.1s, 328 MB peak RSS. attrLocal()'s linear scan is only invoked on a handful of nodes, so the quadratic blowup I was hoping for never materialised. a16: a single <a:t> holding 100 MB of text -> 0.3s, 448 MB, correctly truncated to maxRunTextChars (100,000). 448 MB was the highest RSS I recorded in the entire session.
- IMAGES THAT ARE NOT IMAGES / ABSURD DIMENSIONS — a13: raw non-image bytes with a .png name; a PNG whose IHDR declares 0x7FFFFFFF x 0x7FFFFFFF; one declaring 65535 x 65535; one declaring 0 x 0. All four rejected, visible 'missing image' placeholders drawn, 0.4s at 47 MB. decodeGuarded()'s PNG/JPEG allow-list (excluding the CVE-prone TIFF/WebP/GIF codecs), the 40 Mpx bound and QImageReader::setAllocationLimit(128 MiB) all held. Only side effect was the libpng stderr chatter reported as SEV-3.
- NESTED ARCHIVE AS A MEDIA PART — a17: a .pptx whose ppt/media/image1.png is itself a zip containing a 50 MB payload, referenced by a <p:pic>. Inert: not a valid PNG, so decodeGuarded rejects it and a placeholder is drawn. 0.2s, 47 MB. No recursive unpacking anywhere in the loader.
- SLIDE WIDTH/HEIGHT 0, NEGATIVE, OVERFLOW, INT64-MAX — a14, four variants. cx=cy=0, cx=cy=-1 and a 20-digit overflow value all produced the 'slide-size' LoadWarning and rendered a safe black frame; int64-max loaded and rendered black via a degenerate clip rect. No divide-by-zero, no crash, 0.0-0.2s, 24-43 MB. The audit F1a-5 guard at slide_renderer.cpp:186 holds.
- RELATIONSHIP CYCLES — a15: slide1's rels declare slide1.xml itself as its own slideLayout, plus a rel pointing at the .rels file, plus presentation rels pointing back at presentation.xml. No infinite loop, no unbounded recursion, 0.2s at 43 MB. The loader follows the layout exactly one level deep and never re-enters relationship resolution, so the graph cannot be walked into a cycle.
- 150,000 ZIP ENTRIES — a20: a 17,402,786-byte archive with 150k tiny entries. 0.5s, 83 MB. The central-directory scan is linear and libzip's name lookup is hashed, so entry count alone is not a lever.
- MEDIA READ AMPLIFICATION (the fix you asked me to verify) — HOLDS, and the cumulative cap IS charged. a25: a 100 MB media part referenced by 2000 <p:pic> in a 109,955-byte archive -> 0.3s, 149 MB peak RSS. The mediaCache at deck_loader.cpp:762-776 shares one copy-on-write QByteArray across all references, and mediaBytes charges maxTotalUncompressed exactly once per distinct part. Without the fix this deck would have been ~200 GB resident. The only residual gap is on the RENDER side (SEV-4).
- DISK WRITES — across every hostile deck driven through the real app binary I snapshotted the repository tree, ~/Library/Application Support, ~/Library/Preferences, ~/Library/Saved Application State, ~/Library/Logs, /var/tmp and the process working directory before and after each run. Zero files created by the application in any run. (Snapshots did show unrelated concurrent writes from Brave and Obsidian, which I confirmed are not attributable to the app.) The loader genuinely never writes to disk, as deck_loader.hpp claims.
- DECK PATH AND CONTENT IN THE UI — the app never printed the deck's file path to stdout, stderr or a dialog. AppShell::onDeckLoaded (app_shell.cpp:107-118) shows describeLoadError(kind), a fixed string chosen by error kind, and never LoadError::message. I confirmed this with a deck whose failing part was named 'CONFIDENTIAL-Q3-BOARD-DECK.xml' and another named 'SECRET-ACQUISITION-TARGET-NAME.bin': both names appear in LoadError::message and are printed verbatim by ./build/render_preview (a dev tool), but neither reaches the app's UI. Worth a note for triage rather than a finding: LoadError::message at deck_loader.cpp:582 and 748 does embed attacker-controlled internal part names, so it is one careless .arg() away from becoming a projector-visible leak — the current shielding is correct but load-bearing and undocumented at the call site.
- PRE-RENDER PREVENT CAP — a27: 3000 text shapes on one slide (over RenderCaps::maxShapesPerSlide of 2000, under the loader's 5000). The app survived with no hang and no crash; the over-cap slide is diverted to a placeholder without entering the renderer, as designed. I could not observe the placeholder directly under QT_QPA_PLATFORM=offscreen, so this is a negative result (no crash, no hang) rather than positive confirmation of the placeholder image itself.
