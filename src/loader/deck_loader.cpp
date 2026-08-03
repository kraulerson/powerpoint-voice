#include "loader/deck_loader.hpp"

#include <cstring>
#include <vector>

#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <pugixml.hpp>
#include <zip.h>

// Deck loader implementation (Build Loop GREEN). Opens an untrusted .pptx with
// libzip (read-only, never writing to disk — TM containment), enforces the
// resource caps before doing real work (TM-014..018), and walks the OOXML with
// pugixml into the in-memory slide model. XML is matched by LOCAL name so a deck
// using non-conventional namespace prefixes still parses.
namespace pptv {
namespace {

// Local name = the part after the ':' prefix (OOXML uses a:/p:/r:, but the
// prefixes are only conventional — never hard-match them).
QString localName(const char* qualified) {
    const char* colon = std::strchr(qualified, ':');
    return QString::fromUtf8(colon ? colon + 1 : qualified);
}

pugi::xml_node childLocal(const pugi::xml_node& node, const char* local) {
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        if (localName(c.name()) == QLatin1String(local)) {
            return c;
        }
    }
    return {};
}

// First descendant in pre-order (depth-first) with the given local name.
// ITERATIVE (audit F1a-2): an explicit heap stack replaces recursion so a
// deeply-nested hostile deck cannot exhaust the call stack and crash the app.
pugi::xml_node descendantLocal(const pugi::xml_node& node, const char* local) {
    std::vector<pugi::xml_node> stack;
    for (pugi::xml_node c = node.last_child(); c; c = c.previous_sibling()) {
        stack.push_back(c); // pushed last..first so the leftmost is on top
    }
    while (!stack.empty()) {
        pugi::xml_node cur = stack.back();
        stack.pop_back();
        if (localName(cur.name()) == QLatin1String(local)) {
            return cur;
        }
        for (pugi::xml_node c = cur.last_child(); c; c = c.previous_sibling()) {
            stack.push_back(c);
        }
    }
    return {};
}

QString attrLocal(const pugi::xml_node& node, const char* local) {
    for (pugi::xml_attribute a = node.first_attribute(); a; a = a.next_attribute()) {
        if (localName(a.name()) == QLatin1String(local)) {
            return QString::fromUtf8(a.value());
        }
    }
    return {};
}

std::optional<Color> parseSrgb(const QString& hex) {
    if (hex.size() != 6) {
        return std::nullopt;
    }
    bool ok = false;
    const uint v = hex.toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    Color c;
    c.r = static_cast<unsigned char>((v >> 16) & 0xFF);
    c.g = static_cast<unsigned char>((v >> 8) & 0xFF);
    c.b = static_cast<unsigned char>(v & 0xFF);
    c.a = 255;
    return c;
}

// Normalize a relationship Target relative to its base directory, resolving
// leading "../" segments (e.g. base "ppt/slides" + "../media/image1.png" ->
// "ppt/media/image1.png").
QString resolveTarget(const QString& baseDir, const QString& target) {
    if (target.startsWith(QLatin1Char('/'))) {
        return target.mid(1);
    }
    QStringList parts = baseDir.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& seg : target.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (seg == QLatin1String("..")) {
            if (!parts.isEmpty()) {
                parts.removeLast();
            }
        } else if (seg != QLatin1String(".")) {
            parts.append(seg);
        }
    }
    return parts.join(QLatin1Char('/'));
}

// Reads a named part fully into memory. Returns false if the part is absent or
// exceeds maxPart. The size is validated (audit F1a-1): st.size is an unsigned
// 64-bit field from the central directory, so it is compared and cast WITHOUT a
// signed round-trip that a ZIP64 value > 2^63 could wrap negative.
bool readPart(zip_t* za, const QString& name, QByteArray& out, long long maxPart) {
    const QByteArray n = name.toUtf8();
    zip_stat_t st;
    if (zip_stat(za, n.constData(), 0, &st) != 0) {
        return false;
    }
    if (!(st.valid & ZIP_STAT_SIZE) || st.size > static_cast<zip_uint64_t>(maxPart)) {
        return false;
    }
    zip_file_t* zf = zip_fopen(za, n.constData(), 0);
    if (!zf) {
        return false;
    }
    out.resize(static_cast<qsizetype>(st.size));
    const zip_int64_t got = zip_fread(zf, out.data(), st.size);
    zip_fclose(zf);
    if (got < 0 || static_cast<zip_uint64_t>(got) != st.size) {
        return false;
    }
    return true;
}

// Parse an <Relationships> part into an Id -> Target map.
QHash<QString, QString> parseRels(const QByteArray& xml) {
    QHash<QString, QString> map;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return map;
    }
    pugi::xml_node root = doc.first_child(); // <Relationships>
    for (pugi::xml_node r = root.first_child(); r; r = r.next_sibling()) {
        if (localName(r.name()) != QLatin1String("Relationship")) {
            continue;
        }
        map.insert(attrLocal(r, "Id"), attrLocal(r, "Target"));
    }
    return map;
}

RectEmu parseXfrm(const pugi::xml_node& spPr) {
    RectEmu rect;
    pugi::xml_node xfrm = childLocal(spPr, "xfrm");
    if (!xfrm) {
        return rect;
    }
    pugi::xml_node off = childLocal(xfrm, "off");
    pugi::xml_node ext = childLocal(xfrm, "ext");
    if (off) {
        rect.x = attrLocal(off, "x").toLongLong();
        rect.y = attrLocal(off, "y").toLongLong();
    }
    if (ext) {
        rect.cx = attrLocal(ext, "cx").toLongLong();
        rect.cy = attrLocal(ext, "cy").toLongLong();
    }
    return rect;
}

TextRun parseRun(const pugi::xml_node& r, int maxChars) {
    TextRun run;
    run.text = QString::fromUtf8(childLocal(r, "t").text().get()).left(maxChars);
    pugi::xml_node rPr = childLocal(r, "rPr");
    if (rPr) {
        const QString sz = attrLocal(rPr, "sz");
        if (!sz.isEmpty()) {
            run.fontSizePt = sz.toDouble() / 100.0; // hundredths of a point
        }
        run.bold = attrLocal(rPr, "b") == QLatin1String("1");
        run.italic = attrLocal(rPr, "i") == QLatin1String("1");
        pugi::xml_node latin = childLocal(rPr, "latin");
        if (latin) {
            run.fontFamily = attrLocal(latin, "typeface");
        }
        pugi::xml_node fill = childLocal(rPr, "solidFill");
        if (fill) {
            run.color = parseSrgb(attrLocal(childLocal(fill, "srgbClr"), "val"));
        }
    }
    return run;
}

TextBox parseTextBox(const pugi::xml_node& sp, const LoaderLimits& lim) {
    TextBox tb;
    tb.rect = parseXfrm(childLocal(sp, "spPr"));
    pugi::xml_node txBody = childLocal(sp, "txBody");
    for (pugi::xml_node p = txBody.first_child(); p; p = p.next_sibling()) {
        if (localName(p.name()) != QLatin1String("p")) {
            continue;
        }
        // Per-box paragraph/run caps (audit R5): bound unbounded text bodies.
        if (static_cast<int>(tb.paragraphs.size()) >= lim.maxParagraphsPerBox) {
            break;
        }
        Paragraph para;
        for (pugi::xml_node r = p.first_child(); r; r = r.next_sibling()) {
            if (localName(r.name()) == QLatin1String("r")) {
                if (static_cast<int>(para.runs.size()) >= lim.maxRunsPerParagraph) {
                    break;
                }
                para.runs.push_back(parseRun(r, lim.maxRunTextChars));
            }
        }
        tb.paragraphs.push_back(std::move(para));
    }
    return tb;
}

Background parseBackground(const pugi::xml_node& cSld) {
    Background bg;
    pugi::xml_node bgNode = childLocal(cSld, "bg");
    if (!bgNode) {
        return bg; // BackgroundKind::None
    }
    pugi::xml_node solid = descendantLocal(bgNode, "solidFill");
    if (solid) {
        pugi::xml_node clr = childLocal(solid, "srgbClr");
        if (clr) {
            bg.kind = BackgroundKind::Solid;
            bg.solid = parseSrgb(attrLocal(clr, "val"));
        }
    }
    return bg;
}

Slide parseSlide(const QByteArray& xml, int index, const QHash<QString, QString>& slideRels,
                 const QString& slideDir, const LoaderLimits& lim, bool& xmlOk) {
    Slide slide;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        xmlOk = false;
        return slide;
    }
    xmlOk = true;

    pugi::xml_node sld = doc.first_child(); // <p:sld>
    pugi::xml_node cSld = childLocal(sld, "cSld");
    slide.background = parseBackground(cSld);
    pugi::xml_node spTree = childLocal(cSld, "spTree");

    for (pugi::xml_node node = spTree.first_child(); node; node = node.next_sibling()) {
        // Per-slide shape cap (audit F1a-4): a legal-but-pathological slide with
        // millions of shapes must not exhaust memory. Stop and warn at the cap.
        if (static_cast<int>(slide.elements.size()) >= lim.maxShapesPerSlide) {
            LoadWarning w;
            w.slideIndex = index;
            w.elementType = QStringLiteral("shape-cap");
            w.detail = QStringLiteral("slide has more than %1 shapes; remainder skipped")
                           .arg(lim.maxShapesPerSlide);
            slide.warnings.push_back(std::move(w));
            break;
        }
        const QString name = localName(node.name());
        if (name == QLatin1String("sp")) {
            if (childLocal(node, "txBody")) {
                ShapeElement e;
                e.kind = ElementKind::TextBox;
                e.textBox = parseTextBox(node, lim);
                slide.elements.push_back(std::move(e));
            }
        } else if (name == QLatin1String("pic")) {
            ShapeElement e;
            e.kind = ElementKind::Image;
            e.image.rect = parseXfrm(childLocal(node, "spPr"));
            pugi::xml_node blip = descendantLocal(node, "blip");
            const QString rid = attrLocal(blip, "embed");
            if (!rid.isEmpty() && slideRels.contains(rid)) {
                e.image.mediaPart = resolveTarget(slideDir, slideRels.value(rid));
            }
            slide.elements.push_back(std::move(e));
        } else if (name == QLatin1String("graphicFrame") || name == QLatin1String("grpSp") ||
                   name == QLatin1String("cxnSp")) {
            // Unsupported in the text+images tier — record, never render wrong.
            LoadWarning w;
            w.slideIndex = index;
            pugi::xml_node gd = descendantLocal(node, "graphicData");
            const QString uri = attrLocal(gd, "uri");
            if (uri.contains(QLatin1String("table"))) {
                w.elementType = QStringLiteral("table");
            } else if (uri.contains(QLatin1String("chart"))) {
                w.elementType = QStringLiteral("chart");
            } else if (uri.contains(QLatin1String("smartArt"))) {
                w.elementType = QStringLiteral("smartArt");
            } else {
                w.elementType = name;
            }
            w.detail = uri.isEmpty() ? name : uri;
            const QString unsupportedType = w.elementType;
            slide.warnings.push_back(std::move(w));

            // Also record a positioned placeholder element so the renderer can
            // draw a VISIBLE marker where the unsupported content was. Its xfrm
            // may live directly under the frame (graphicFrame) rather than in an
            // spPr, so resolve off/ext by descendant search.
            ShapeElement ph;
            ph.kind = ElementKind::Unsupported;
            ph.unsupported.type = unsupportedType;
            pugi::xml_node off = descendantLocal(node, "off");
            pugi::xml_node ext = descendantLocal(node, "ext");
            if (off) {
                ph.unsupported.rect.x = attrLocal(off, "x").toLongLong();
                ph.unsupported.rect.y = attrLocal(off, "y").toLongLong();
            }
            if (ext) {
                ph.unsupported.rect.cx = attrLocal(ext, "cx").toLongLong();
                ph.unsupported.rect.cy = attrLocal(ext, "cy").toLongLong();
            }
            slide.elements.push_back(std::move(ph));
        }
    }
    return slide;
}

LoadResult fail(LoadErrorKind kind, const QString& msg) {
    LoadResult r;
    r.ok = false;
    r.error = LoadError{kind, msg};
    return r;
}

} // namespace

LoadResult DeckLoader::load(const QString& path, const LoaderLimits& limits) {
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return fail(LoadErrorKind::FileNotFound, QStringLiteral("file not found: %1").arg(path));
    }
    if (info.size() > limits.maxFileBytes) {
        return fail(LoadErrorKind::FileTooLarge,
                    QStringLiteral("archive exceeds %1 bytes").arg(limits.maxFileBytes));
    }

    int zerr = 0;
    zip_t* za = zip_open(path.toUtf8().constData(), ZIP_RDONLY, &zerr);
    if (!za) {
        return fail(LoadErrorKind::NotAZip, QStringLiteral("not a valid .pptx (zip) archive"));
    }

    // Enforce decompression caps from the central directory BEFORE reading any
    // part (zip-bomb defense, TM-014/017).
    const zip_int64_t n = zip_get_num_entries(za, 0);
    long long total = 0;
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_stat_t st;
        if (zip_stat_index(za, static_cast<zip_uint64_t>(i), 0, &st) != 0) {
            continue;
        }
        // Compare UNSIGNED (audit F1a-1): st.size is zip_uint64_t. Casting it to
        // signed first would let a ZIP64 declared size > 2^63 wrap negative and
        // slip past the cap. maxPartUncompressed is non-negative, so the widening
        // cast to zip_uint64_t is exact.
        if (!(st.valid & ZIP_STAT_SIZE) ||
            st.size > static_cast<zip_uint64_t>(limits.maxPartUncompressed)) {
            zip_close(za);
            return fail(LoadErrorKind::PartTooLarge,
                        QStringLiteral("part '%1' exceeds per-part size cap")
                            .arg((st.valid & ZIP_STAT_NAME) ? QString::fromUtf8(st.name)
                                                            : QStringLiteral("<unknown>")));
        }
        total += static_cast<long long>(st.size); // st.size now known <= 128 MB
        if (total > limits.maxTotalUncompressed) {
            zip_close(za);
            return fail(LoadErrorKind::DecompressionLimit,
                        QStringLiteral("archive decompresses beyond the size cap"));
        }
    }

    QByteArray presXml;
    if (!readPart(za, QStringLiteral("ppt/presentation.xml"), presXml,
                  limits.maxPartUncompressed)) {
        zip_close(za);
        return fail(LoadErrorKind::MissingPresentationPart,
                    QStringLiteral("ppt/presentation.xml is missing"));
    }

    pugi::xml_document presDoc;
    if (!presDoc.load_buffer(presXml.constData(), static_cast<size_t>(presXml.size()))) {
        zip_close(za);
        return fail(LoadErrorKind::MalformedXml,
                    QStringLiteral("ppt/presentation.xml is malformed"));
    }

    Presentation pres;
    pugi::xml_node presRoot = presDoc.first_child(); // <p:presentation>
    pugi::xml_node sz = childLocal(presRoot, "sldSz");
    if (sz) {
        pres.slideWidth = attrLocal(sz, "cx").toLongLong();
        pres.slideHeight = attrLocal(sz, "cy").toLongLong();
    }
    // A missing or non-numeric slide size would become 0 and later divide-by-zero
    // in the renderer (audit F1a-5). Surface it as a warning; F1b treats a
    // non-positive slide size as a safe default rather than dividing by it.
    if (pres.slideWidth <= 0 || pres.slideHeight <= 0) {
        LoadWarning w;
        w.slideIndex = 0;
        w.elementType = QStringLiteral("slide-size");
        w.detail = QStringLiteral("presentation has no valid slide size (sldSz)");
        pres.warnings.push_back(std::move(w));
    }

    // Ordered slide relationship ids from <p:sldIdLst>. Each <p:sldId> carries
    // both a plain "id" (the slide's numeric id) and an r-namespaced "id" (the
    // relationship id that maps to the part). We want the latter: the attribute
    // whose qualified name is prefixed (e.g. "r:id").
    QStringList slideRids;
    pugi::xml_node idLst = childLocal(presRoot, "sldIdLst");
    for (pugi::xml_node s = idLst.first_child(); s; s = s.next_sibling()) {
        if (localName(s.name()) != QLatin1String("sldId")) {
            continue;
        }
        QString rid;
        for (pugi::xml_attribute a = s.first_attribute(); a; a = a.next_attribute()) {
            const QString an = QString::fromUtf8(a.name());
            if (an.contains(QLatin1Char(':')) && an.endsWith(QLatin1String(":id"))) {
                rid = QString::fromUtf8(a.value());
            }
        }
        if (!rid.isEmpty()) {
            slideRids.append(rid);
        }
    }

    if (slideRids.size() > limits.maxSlides) {
        zip_close(za);
        return fail(LoadErrorKind::TooManySlides, QStringLiteral("deck has %1 slides (cap %2)")
                                                      .arg(slideRids.size())
                                                      .arg(limits.maxSlides));
    }

    // presentation rels: rId -> slides/slideN.xml (relative to ppt/).
    QByteArray presRelsXml;
    QHash<QString, QString> presRels;
    if (readPart(za, QStringLiteral("ppt/_rels/presentation.xml.rels"), presRelsXml,
                 limits.maxPartUncompressed)) {
        presRels = parseRels(presRelsXml);
    }

    // Insert a placeholder slide that preserves slide numbering (audit F1a-3).
    // Silently dropping an unresolvable slide would compact the vector and make
    // "go to slide N" land on the wrong slide during a live talk.
    auto placeholder = [&pres](int idx, const QString& reason) {
        Slide ph;
        ph.placeholder = true;
        LoadWarning w;
        w.slideIndex = idx;
        w.elementType = QStringLiteral("missing-slide");
        w.detail = reason;
        ph.warnings.push_back(w);
        pres.warnings.push_back(w);
        pres.slides.push_back(std::move(ph));
    };

    int index = 0;
    for (const QString& rid : slideRids) {
        ++index;
        const QString target = presRels.value(rid);
        if (target.isEmpty()) {
            placeholder(index, QStringLiteral("no relationship for %1").arg(rid));
            continue;
        }
        const QString slidePart = resolveTarget(QStringLiteral("ppt"), target);

        QByteArray slideXml;
        if (!readPart(za, slidePart, slideXml, limits.maxPartUncompressed)) {
            placeholder(index, QStringLiteral("missing part %1").arg(slidePart));
            continue;
        }

        // Slide's own rels (for image resolution), if present.
        const int slash = slidePart.lastIndexOf(QLatin1Char('/'));
        const QString slideDir = slidePart.left(slash);
        const QString slideFile = slidePart.mid(slash + 1);
        const QString slideRelsPart =
            slideDir + QStringLiteral("/_rels/") + slideFile + QStringLiteral(".rels");
        QHash<QString, QString> slideRels;
        QByteArray slideRelsXml;
        if (readPart(za, slideRelsPart, slideRelsXml, limits.maxPartUncompressed)) {
            slideRels = parseRels(slideRelsXml);
        }

        bool xmlOk = true;
        Slide slide = parseSlide(slideXml, index, slideRels, slideDir, limits, xmlOk);
        if (!xmlOk) {
            zip_close(za);
            return fail(LoadErrorKind::MalformedXml,
                        QStringLiteral("slide part '%1' is malformed").arg(slidePart));
        }
        // Load raw image bytes for each resolved picture (decoded at render, F1b).
        // Governed by the same per-part cap; a media part over the cap or absent
        // leaves imageData empty and the renderer draws a missing-image placeholder.
        for (ShapeElement& e : slide.elements) {
            if (e.kind == ElementKind::Image && !e.image.mediaPart.isEmpty()) {
                QByteArray bytes;
                if (readPart(za, e.image.mediaPart, bytes, limits.maxPartUncompressed)) {
                    e.image.imageData = std::move(bytes);
                }
            }
        }
        for (const LoadWarning& w : slide.warnings) {
            pres.warnings.push_back(w);
        }
        pres.slides.push_back(std::move(slide));
    }

    zip_close(za);

    LoadResult result;
    result.ok = true;
    result.presentation = std::move(pres);
    return result;
}

} // namespace pptv
