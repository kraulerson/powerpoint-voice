#include "loader/deck_loader.hpp"

#include <algorithm>
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

// Theme color scheme (BUG-1): maps scheme slot names (dk1/lt1/accent1/...) to
// concrete colors, parsed from ppt/theme/themeN.xml. Real decks color text via
// scheme references, so without this, themed text has no color and renders
// invisible on a dark background.
using ThemeColors = QHash<QString, Color>;

ThemeColors parseTheme(const QByteArray& xml) {
    ThemeColors theme;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return theme;
    }
    pugi::xml_node clrScheme = descendantLocal(doc.first_child(), "clrScheme");
    for (pugi::xml_node c = clrScheme.first_child(); c; c = c.next_sibling()) {
        const QString slot = localName(c.name()); // dk1, lt1, accent1, ...
        pugi::xml_node srgb = childLocal(c, "srgbClr");
        pugi::xml_node sys = childLocal(c, "sysClr");
        QString hex;
        if (srgb) {
            hex = attrLocal(srgb, "val");
        } else if (sys) {
            hex = attrLocal(sys, "lastClr"); // sysClr carries a resolved lastClr
        }
        if (auto col = parseSrgb(hex)) {
            theme.insert(slot, *col);
        }
    }
    return theme;
}

// Resolve a <a:schemeClr val="..."> against the theme, applying the default
// color map (tx1->dk1, bg1->lt1, tx2->dk2, bg2->lt2; others map by name).
std::optional<Color> resolveScheme(const QString& val, const ThemeColors& theme) {
    QString key = val;
    if (val == QLatin1String("tx1")) {
        key = QStringLiteral("dk1");
    } else if (val == QLatin1String("tx2")) {
        key = QStringLiteral("dk2");
    } else if (val == QLatin1String("bg1")) {
        key = QStringLiteral("lt1");
    } else if (val == QLatin1String("bg2")) {
        key = QStringLiteral("lt2");
    }
    if (theme.contains(key)) {
        return theme.value(key);
    }
    return std::nullopt;
}

// Resolve a <...><a:solidFill> child (srgbClr or schemeClr) to a color.
std::optional<Color> parseSolidFill(const pugi::xml_node& fill, const ThemeColors& theme) {
    if (!fill) {
        return std::nullopt;
    }
    pugi::xml_node srgb = childLocal(fill, "srgbClr");
    if (srgb) {
        return parseSrgb(attrLocal(srgb, "val"));
    }
    pugi::xml_node scheme = childLocal(fill, "schemeClr");
    if (scheme) {
        return resolveScheme(attrLocal(scheme, "val"), theme);
    }
    return std::nullopt;
}

// Default font sizes (points) inherited from the slide master's <p:txStyles>
// when a run/paragraph declares none (BUG-8). Without this, unsized text (very
// common in real decks) renders at 1px — blank/tiny slides.
struct MasterTextStyles {
    double title = 0.0;
    std::vector<double> body; // per list level (index 0 == level 0)
    double other = 0.0;
};

double defRPrSizePt(const pugi::xml_node& lvlPr) {
    const QString sz = attrLocal(childLocal(lvlPr, "defRPr"), "sz");
    return sz.isEmpty() ? 0.0 : sz.toDouble() / 100.0;
}

MasterTextStyles parseMasterStyles(const QByteArray& xml) {
    MasterTextStyles s;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return s;
    }
    pugi::xml_node txStyles = descendantLocal(doc.first_child(), "txStyles");
    pugi::xml_node title = childLocal(txStyles, "titleStyle");
    if (title) {
        s.title = defRPrSizePt(childLocal(title, "lvl1pPr"));
    }
    pugi::xml_node other = childLocal(txStyles, "otherStyle");
    if (other) {
        s.other = defRPrSizePt(childLocal(other, "lvl1pPr"));
    }
    pugi::xml_node body = childLocal(txStyles, "bodyStyle");
    for (int lvl = 1; lvl <= 9; ++lvl) {
        const QByteArray tag = QStringLiteral("lvl%1pPr").arg(lvl).toUtf8();
        pugi::xml_node lvlPr = childLocal(body, tag.constData());
        if (!lvlPr) {
            break;
        }
        s.body.push_back(defRPrSizePt(lvlPr));
    }
    return s;
}

// Placeholder type of a shape ("title"/"ctrTitle"/"body"/"subTitle"/... or empty).
QString placeholderType(const pugi::xml_node& sp) {
    pugi::xml_node ph = descendantLocal(childLocal(sp, "nvSpPr"), "ph");
    return ph ? attrLocal(ph, "type") : QString();
}

// The inherited default size (points) for a placeholder type + list level.
double resolveDefaultSizePt(const QString& phType, int lvl, const MasterTextStyles& m) {
    const bool isTitle = phType == QLatin1String("title") || phType == QLatin1String("ctrTitle");
    if (isTitle && m.title > 0.0) {
        return m.title;
    }
    if (!isTitle && !m.body.empty()) {
        const int i = std::clamp(lvl, 0, static_cast<int>(m.body.size()) - 1);
        if (m.body[i] > 0.0) {
            return m.body[i];
        }
    }
    if (m.other > 0.0) {
        return m.other;
    }
    return isTitle ? 40.0 : 18.0; // last-resort fallbacks so text is never 1px
}

TextRun parseRun(const pugi::xml_node& r, int maxChars, const ThemeColors& theme,
                 double defaultSizePt) {
    TextRun run;
    run.text = QString::fromUtf8(childLocal(r, "t").text().get()).left(maxChars);
    run.fontSizePt = defaultSizePt; // inherited default unless overridden below
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
        run.color = parseSolidFill(childLocal(rPr, "solidFill"), theme);
    }
    return run;
}

TextBox parseTextBox(const pugi::xml_node& sp, const LoaderLimits& lim, const ThemeColors& theme,
                     const MasterTextStyles& master) {
    TextBox tb;
    tb.rect = parseXfrm(childLocal(sp, "spPr"));
    const QString phType = placeholderType(sp);
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
        // Paragraph properties: list level + inline bullet marker (BUG-7).
        pugi::xml_node pPr = childLocal(p, "pPr");
        if (pPr) {
            para.indentLevel = attrLocal(pPr, "lvl").toInt();
            pugi::xml_node buChar = childLocal(pPr, "buChar");
            pugi::xml_node buAutoNum = childLocal(pPr, "buAutoNum");
            if (buChar) {
                para.bulletChar = attrLocal(buChar, "char");
            } else if (buAutoNum) {
                para.bulletChar = QStringLiteral("•"); // numbered lists -> a dot (MVP)
            }
        }
        // Inherited default size for runs that declare none (BUG-8).
        const double defaultSizePt = resolveDefaultSizePt(phType, para.indentLevel, master);
        for (pugi::xml_node r = p.first_child(); r; r = r.next_sibling()) {
            const QString rn = localName(r.name());
            if (static_cast<int>(para.runs.size()) >= lim.maxRunsPerParagraph) {
                break;
            }
            if (rn == QLatin1String("r")) {
                para.runs.push_back(parseRun(r, lim.maxRunTextChars, theme, defaultSizePt));
            } else if (rn == QLatin1String("br")) {
                // Soft line break within the paragraph (BUG-6): a U+2028 line
                // separator, which the renderer's QTextLayout breaks on.
                TextRun brk;
                brk.text = QString(QChar(0x2028));
                para.runs.push_back(std::move(brk));
            }
        }
        tb.paragraphs.push_back(std::move(para));
    }
    return tb;
}

// One entry of the theme's <a:bgFillStyleLst>, which <p:bgRef idx="100N"> selects.
// Only a plain solid entry is exactly resolvable; a gradient or pattern entry is
// not, and guessing a single colour for it would be a silent wrong render.
struct ThemeBgFill {
    enum class Kind { Unsupported, SolidPhClr, SolidFixed };
    Kind kind = Kind::Unsupported;
    Color fixed{};
};

std::vector<ThemeBgFill> parseThemeBgFills(const QByteArray& xml, const ThemeColors& theme) {
    std::vector<ThemeBgFill> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return out;
    }
    pugi::xml_node lst = descendantLocal(doc.first_child(), "bgFillStyleLst");
    for (pugi::xml_node f = lst.first_child(); f; f = f.next_sibling()) {
        ThemeBgFill entry;
        if (localName(f.name()) == QLatin1String("solidFill")) {
            pugi::xml_node scheme = childLocal(f, "schemeClr");
            if (scheme && attrLocal(scheme, "val") == QLatin1String("phClr")) {
                // Paints with whatever colour the referencing bgRef supplies.
                entry.kind = ThemeBgFill::Kind::SolidPhClr;
            } else if (auto c = parseSolidFill(f, theme)) {
                entry.kind = ThemeBgFill::Kind::SolidFixed;
                entry.fixed = *c;
            }
        }
        out.push_back(entry);
    }
    return out;
}

// The background a SINGLE part (slide, layout or master) declares.
//
// OOXML resolves a slide's background by inheritance: slide -> its layout -> that
// layout's master. A part with no <p:bg> inherits the next one up; a part that DOES
// declare one ends the search, whether or not we can paint it. Karl's real deck
// carries <p:bg> on ZERO of its 10 slides, on 12 of its 17 layouts and on its
// master — so a slide-only reader renders the whole deck white (BUG-32). Same bug
// class as BUG-1 (theme colours) and BUG-2 (placeholder geometry).
struct BackgroundSource {
    bool declared = false;   // the part has a <p:bg> element
    Background bg;           // declared && kind==None  =>  declared but not paintable
    QString unsupportedKind; // fixed text for the warning; never deck content
};

// The local name of the first *Fill child, e.g. "gradFill". Used only to name the
// fill KIND in a warning — a fixed vocabulary, never anything read from the deck.
QString fillKindOf(const pugi::xml_node& parent) {
    for (pugi::xml_node c = parent.first_child(); c; c = c.next_sibling()) {
        const QString n = localName(c.name());
        if (n.endsWith(QLatin1String("Fill"))) {
            return n;
        }
    }
    return {};
}

BackgroundSource parseBackgroundSource(const pugi::xml_node& cSld, const ThemeColors& theme,
                                       const std::vector<ThemeBgFill>& bgFills) {
    BackgroundSource src;
    pugi::xml_node bgNode = childLocal(cSld, "bg");
    if (!bgNode) {
        return src; // declares nothing -> inherit
    }
    src.declared = true;

    // <p:bgPr> carries the fill inline.
    if (pugi::xml_node bgPr = childLocal(bgNode, "bgPr")) {
        const QString kind = fillKindOf(bgPr);
        if (kind == QLatin1String("solidFill")) {
            // Resolve srgbClr OR a themed schemeClr (BUG-1) so the renderer knows a
            // themed-dark background is dark and can pick readable text.
            if (auto c = parseSolidFill(childLocal(bgPr, "solidFill"), theme)) {
                src.bg.kind = BackgroundKind::Solid;
                src.bg.solid = c;
                return src;
            }
            src.unsupportedKind = QStringLiteral("unresolvable solid fill");
            return src;
        }
        src.unsupportedKind = kind.isEmpty() ? QStringLiteral("empty fill") : kind;
        return src;
    }

    // <p:bgRef idx="100N"> selects the Nth entry of the theme's bgFillStyleLst and
    // carries the placeholder colour (phClr) that entry paints with. idx 0 is "none".
    if (pugi::xml_node ref = childLocal(bgNode, "bgRef")) {
        const int slot = attrLocal(ref, "idx").toInt() - 1001;
        if (slot >= 0 && slot < static_cast<int>(bgFills.size())) {
            const ThemeBgFill& entry = bgFills[static_cast<std::size_t>(slot)];
            if (entry.kind == ThemeBgFill::Kind::SolidFixed) {
                src.bg.kind = BackgroundKind::Solid;
                src.bg.solid = entry.fixed;
                return src;
            }
            if (entry.kind == ThemeBgFill::Kind::SolidPhClr) {
                if (auto c = parseSolidFill(ref, theme)) {
                    src.bg.kind = BackgroundKind::Solid;
                    src.bg.solid = c;
                    return src;
                }
            }
        }
        src.unsupportedKind = QStringLiteral("themed background fill");
        return src;
    }

    src.unsupportedKind = QStringLiteral("unrecognised background");
    return src;
}

// Parse just the <p:bg> of a layout or master part.
BackgroundSource partBackground(const QByteArray& xml, const ThemeColors& theme,
                                const std::vector<ThemeBgFill>& bgFills) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return {};
    }
    return parseBackgroundSource(childLocal(doc.first_child(), "cSld"), theme, bgFills);
}

// Placeholder key "type:idx" for matching a shape against layout geometry (BUG-2).
QString placeholderKey(const pugi::xml_node& sp) {
    pugi::xml_node ph = descendantLocal(childLocal(sp, "nvSpPr"), "ph");
    if (!ph) {
        return {};
    }
    QString type = attrLocal(ph, "type");
    if (type.isEmpty()) {
        type = QStringLiteral("body");
    }
    return type + QLatin1Char(':') + attrLocal(ph, "idx");
}

// Map placeholder key -> geometry, parsed from a slideLayout part (BUG-2). Real
// decks put title/body position in the layout, not inline on the slide.
QHash<QString, RectEmu> parseLayoutPlaceholders(const QByteArray& xml) {
    QHash<QString, RectEmu> map;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        return map;
    }
    pugi::xml_node spTree = descendantLocal(doc.first_child(), "spTree");
    for (pugi::xml_node sp = spTree.first_child(); sp; sp = sp.next_sibling()) {
        if (localName(sp.name()) != QLatin1String("sp")) {
            continue;
        }
        const QString key = placeholderKey(sp);
        if (key.isEmpty()) {
            continue;
        }
        RectEmu rect = parseXfrm(childLocal(sp, "spPr"));
        if (rect.cx > 0 && rect.cy > 0) {
            map.insert(key, rect);
        }
    }
    return map;
}

// Forward declaration for group recursion (BUG-3).
// Nested <p:grpSp> recurses. A hostile deck can nest groups thousands deep, and a
// 5.7 KB file was measured killing the load worker with a stack overflow — no dialog,
// no stderr, instant process death (UAT-3 SEV-2). Depth is therefore capped; content
// below the cap is dropped with a warning rather than followed.
constexpr int kMaxGroupDepth = 32;

void processShapeTree(const pugi::xml_node& tree, Slide& slide, int index,
                      const QHash<QString, QString>& slideRels, const QString& slideDir,
                      const LoaderLimits& lim, const ThemeColors& theme,
                      const QHash<QString, RectEmu>& layoutPh, const MasterTextStyles& master,
                      int depth = 0);

void processShapeTree(const pugi::xml_node& tree, Slide& slide, int index,
                      const QHash<QString, QString>& slideRels, const QString& slideDir,
                      const LoaderLimits& lim, const ThemeColors& theme,
                      const QHash<QString, RectEmu>& layoutPh, const MasterTextStyles& master,
                      int depth) {
    for (pugi::xml_node node = tree.first_child(); node; node = node.next_sibling()) {
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
                e.textBox = parseTextBox(node, lim, theme, master);
                // BUG-2: a placeholder with no inline geometry inherits position
                // from the slide layout.
                if (e.textBox.rect.cx <= 0 || e.textBox.rect.cy <= 0) {
                    const QString key = placeholderKey(node);
                    if (!key.isEmpty() && layoutPh.contains(key)) {
                        e.textBox.rect = layoutPh.value(key);
                    }
                }
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
        } else if (name == QLatin1String("grpSp")) {
            // Recurse into the group so its text/images render instead of a
            // placeholder (BUG-3). Group child-coordinate transforms are not
            // applied (MVP approximation) — positions are read from child xfrms.
            if (depth >= kMaxGroupDepth) {
                LoadWarning w;
                w.slideIndex = index;
                w.elementType = QStringLiteral("grpSp");
                w.detail = QStringLiteral("group nesting exceeds the depth cap; content below "
                                          "this level was not loaded");
                slide.warnings.push_back(w);
                continue;
            }
            processShapeTree(node, slide, index, slideRels, slideDir, lim, theme, layoutPh, master,
                             depth + 1);
        } else if (name == QLatin1String("graphicFrame") || name == QLatin1String("cxnSp")) {
            // Genuinely unsupported (table/chart/SmartArt) -> warning + visible
            // placeholder box.
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
}

// A slideLayout part, parsed once and reused by every slide that references it.
struct LayoutInfo {
    QHash<QString, RectEmu> placeholders; // BUG-2
    BackgroundSource bg;                  // BUG-32
    QString masterPart;                   // resolved from the layout's own rels
};

// Everything a slide inherits from the parts above it in the OOXML hierarchy.
struct InheritedFromParents {
    QHash<QString, RectEmu> layoutPlaceholders; // BUG-2
    BackgroundSource layoutBg;                  // BUG-32
    BackgroundSource masterBg;                  // BUG-32
    std::vector<ThemeBgFill> themeBgFills;
};

Slide parseSlide(const QByteArray& xml, int index, const QHash<QString, QString>& slideRels,
                 const QString& slideDir, const LoaderLimits& lim, const ThemeColors& theme,
                 const InheritedFromParents& inherited, const MasterTextStyles& master,
                 bool& xmlOk) {
    Slide slide;
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.constData(), static_cast<size_t>(xml.size()))) {
        xmlOk = false;
        return slide;
    }
    xmlOk = true;

    pugi::xml_node sld = doc.first_child(); // <p:sld>
    pugi::xml_node cSld = childLocal(sld, "cSld");

    // BUG-32 — walk the chain: the slide's own declaration wins, then the layout's,
    // then the master's. The FIRST part that declares a background ends the walk even
    // if we cannot paint what it declared; falling through to the next level would
    // paint a colour the deck does not specify (Manifesto F1: never a silent wrong
    // render). Whatever we cannot paint is reported instead.
    BackgroundSource resolved = parseBackgroundSource(cSld, theme, inherited.themeBgFills);
    if (!resolved.declared) {
        resolved = inherited.layoutBg;
    }
    if (!resolved.declared) {
        resolved = inherited.masterBg;
    }
    slide.background = resolved.bg;
    if (resolved.declared && resolved.bg.kind == BackgroundKind::None) {
        LoadWarning w;
        w.slideIndex = index;
        w.elementType = QStringLiteral("background");
        w.detail = resolved.unsupportedKind;
        slide.warnings.push_back(std::move(w));
    }

    pugi::xml_node spTree = childLocal(cSld, "spTree");
    processShapeTree(spTree, slide, index, slideRels, slideDir, lim, theme,
                     inherited.layoutPlaceholders, master);
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
            // Copy the name BEFORE zip_close: libzip owns st.name and frees it in
            // zip_close, so formatting afterwards read freed heap and printed those
            // bytes into a user-facing dialog (audit C4 — an attacker-triggered
            // use-after-free AND an information-disclosure channel).
            const QString partName = (st.valid & ZIP_STAT_NAME) ? QString::fromUtf8(st.name)
                                                                : QStringLiteral("<unknown>");
            zip_close(za);
            return fail(LoadErrorKind::PartTooLarge,
                        QStringLiteral("part '%1' exceeds per-part size cap").arg(partName));
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

    // Theme colors for resolving scheme-colored text/backgrounds (BUG-1). The
    // common single-theme case (ppt/theme/theme1.xml) covers most decks.
    ThemeColors theme;
    std::vector<ThemeBgFill> themeBgFills;
    QByteArray themeXml;
    if (readPart(za, QStringLiteral("ppt/theme/theme1.xml"), themeXml,
                 limits.maxPartUncompressed)) {
        theme = parseTheme(themeXml);
        themeBgFills = parseThemeBgFills(themeXml, theme);
    }

    // Master text styles for inherited font sizes (BUG-8). The common
    // single-master case (ppt/slideMasters/slideMaster1.xml) covers most decks.
    MasterTextStyles master;
    QByteArray masterXml;
    if (readPart(za, QStringLiteral("ppt/slideMasters/slideMaster1.xml"), masterXml,
                 limits.maxPartUncompressed)) {
        master = parseMasterStyles(masterXml);
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

    // Media parts are read ONCE and shared by every element that references them
    // (audit C5), and their cumulative size is charged against the decompression cap.
    QHash<QString, QByteArray> mediaCache;
    long long mediaBytes = 0;

    // Layouts and masters are likewise parsed once each, however many slides use them.
    QHash<QString, LayoutInfo> layoutCache;
    QHash<QString, BackgroundSource> masterBgCache;

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

        // The slide's layout supplies placeholder geometry (BUG-2) AND, far more
        // often than the slide does, the background (BUG-32). Decks reuse layouts
        // heavily — Karl's 10 slides share 9 layouts, two of them twice — so each
        // layout (and each master) is read and parsed at most once.
        InheritedFromParents inherited;
        inherited.themeBgFills = themeBgFills;
        QString masterPart = QStringLiteral("ppt/slideMasters/slideMaster1.xml");
        for (auto it = slideRels.constBegin(); it != slideRels.constEnd(); ++it) {
            if (!it.value().contains(QLatin1String("slideLayout"))) {
                continue;
            }
            const QString layoutPart = resolveTarget(slideDir, it.value());
            if (!layoutCache.contains(layoutPart)) {
                LayoutInfo info;
                QByteArray layoutXml;
                if (readPart(za, layoutPart, layoutXml, limits.maxPartUncompressed)) {
                    info.placeholders = parseLayoutPlaceholders(layoutXml);
                    info.bg = partBackground(layoutXml, theme, themeBgFills);
                    // The layout names its own master; only fall back to
                    // slideMaster1 when it does not (or the rels are absent).
                    const int lslash = layoutPart.lastIndexOf(QLatin1Char('/'));
                    const QString layoutRelsPart =
                        layoutPart.left(lslash) + QStringLiteral("/_rels/") +
                        layoutPart.mid(lslash + 1) + QStringLiteral(".rels");
                    QByteArray layoutRelsXml;
                    if (readPart(za, layoutRelsPart, layoutRelsXml, limits.maxPartUncompressed)) {
                        const QHash<QString, QString> lrels = parseRels(layoutRelsXml);
                        for (auto lt = lrels.constBegin(); lt != lrels.constEnd(); ++lt) {
                            if (lt.value().contains(QLatin1String("slideMaster"))) {
                                info.masterPart =
                                    resolveTarget(layoutPart.left(lslash), lt.value());
                                break;
                            }
                        }
                    }
                }
                layoutCache.insert(layoutPart, info);
            }
            const LayoutInfo& info = layoutCache[layoutPart];
            inherited.layoutPlaceholders = info.placeholders;
            inherited.layoutBg = info.bg;
            if (!info.masterPart.isEmpty()) {
                masterPart = info.masterPart;
            }
            break;
        }
        if (!masterBgCache.contains(masterPart)) {
            BackgroundSource mbg;
            QByteArray mXml;
            if (readPart(za, masterPart, mXml, limits.maxPartUncompressed)) {
                mbg = partBackground(mXml, theme, themeBgFills);
            }
            masterBgCache.insert(masterPart, mbg);
        }
        inherited.masterBg = masterBgCache[masterPart];

        bool xmlOk = true;
        Slide slide = parseSlide(slideXml, index, slideRels, slideDir, limits, theme, inherited,
                                 master, xmlOk);
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
                // CACHE by part name (audit C5). The same media entry can be
                // referenced by any number of <p:pic> elements, and each reference
                // previously re-read and re-allocated it while the cumulative
                // decompression cap — which walks the central directory — counted
                // the entry only ONCE. A 63 MB file with 40 references measured
                // 2.4 GB of resident image bytes, and the ceiling was ~640 GB.
                // QByteArray is copy-on-write, so sharing here is free.
                auto it = mediaCache.find(e.image.mediaPart);
                if (it == mediaCache.end()) {
                    QByteArray bytes;
                    if (!readPart(za, e.image.mediaPart, bytes, limits.maxPartUncompressed)) {
                        bytes.clear(); // absent/over-cap: renderer draws a placeholder
                    }
                    mediaBytes += bytes.size();
                    if (mediaBytes > limits.maxTotalUncompressed) {
                        zip_close(za);
                        return fail(LoadErrorKind::DecompressionLimit,
                                    QStringLiteral("media parts exceed the cumulative "
                                                   "decompression cap"));
                    }
                    it = mediaCache.insert(e.image.mediaPart, bytes);
                }
                e.image.imageData = it.value();
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
