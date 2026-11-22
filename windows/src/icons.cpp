#include "icons.h"

#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

#include "theme.h"

namespace {

// Maps a 16 x 16 design grid onto the destination rectangle.
struct Grid {
    Canvas& c;
    int x, y, s;
    Grid(Canvas& cv, const RECT& r) : c(cv), x(r.left), y(r.top), s(r.right - r.left) {}
    int X(double v) const { return x + (int)std::lround(v * s / 16.0); }
    int Y(double v) const { return y + (int)std::lround(v * s / 16.0); }
    int L(double v) const { return (int)std::lround(v * s / 16.0); }
    void rect(double rx, double ry, double rw, double rh, COLORREF col) {
        c.fill(X(rx), Y(ry), L(rw), L(rh), col);
    }
    void line(double x1, double y1, double x2, double y2, COLORREF col) {
        c.line(X(x1), Y(y1), X(x2), Y(y2), col);
    }
    void poly(std::initializer_list<std::pair<double, double>> pts, COLORREF col, bool outline) {
        std::vector<POINT> p;
        p.reserve(pts.size());
        for (auto& q : pts) p.push_back(POINT{X(q.first), Y(q.second)});
        if (outline) c.polygonOutline(p.data(), (int)p.size(), col);
        else c.polygonFill(p.data(), (int)p.size(), col);
    }
};

bool inList(const std::wstring& ext, std::initializer_list<const wchar_t*> list) {
    for (auto* s : list)
        if (ext == s) return true;
    return false;
}

enum class Badge {
    Image, Pdf, Archive, Disk, Audio, Video, Doc, Sheet, Slide, Text, Html, Code, Data, Font,
    Program, Design, Plain
};

Badge badgeFor(const std::wstring& ext) {
    if (inList(ext, {L"png", L"jpg", L"jpeg", L"gif", L"webp", L"heic", L"heif", L"tif",
                     L"tiff", L"bmp", L"svg", L"ico", L"raw", L"psb", L"jfif", L"avif"}))
        return Badge::Image;
    if (ext == L"pdf") return Badge::Pdf;
    if (inList(ext, {L"zip", L"7z", L"rar", L"gz", L"tar", L"xz", L"bz2", L"tgz", L"cab"}))
        return Badge::Archive;
    if (inList(ext, {L"dmg", L"iso", L"img", L"toast", L"sparseimage", L"cdr", L"vhd"}))
        return Badge::Disk;
    if (inList(ext, {L"mp3", L"wav", L"aif", L"aiff", L"m4a", L"flac", L"ogg", L"aac",
                     L"opus", L"wma", L"ape"}))
        return Badge::Audio;
    if (inList(ext, {L"mp4", L"mov", L"mkv", L"avi", L"m4v", L"webm", L"flv", L"wmv", L"mpg",
                     L"mpeg"}))
        return Badge::Video;
    if (inList(ext, {L"doc", L"docx", L"rtf", L"pages", L"odt", L"wps"})) return Badge::Doc;
    if (inList(ext, {L"xls", L"xlsx", L"csv", L"tsv", L"numbers", L"ods"})) return Badge::Sheet;
    if (inList(ext, {L"ppt", L"pptx", L"key", L"odp"})) return Badge::Slide;
    if (inList(ext, {L"txt", L"md", L"markdown", L"log", L"nfo", L"tex", L"me"})) return Badge::Text;
    if (inList(ext, {L"html", L"htm", L"xhtml", L"webloc", L"url", L"mht"})) return Badge::Html;
    if (inList(ext, {L"js", L"ts", L"jsx", L"tsx", L"swift", L"py", L"rb", L"go", L"rs", L"c",
                     L"h", L"cpp", L"hpp", L"cc", L"m", L"mm", L"java", L"kt", L"php", L"sh",
                     L"zsh", L"bash", L"pl", L"lua", L"css", L"scss", L"less", L"rsrc"}))
        return Badge::Code;
    if (inList(ext, {L"json", L"xml", L"plist", L"yml", L"yaml", L"toml", L"ini", L"conf",
                     L"cfg", L"env"}))
        return Badge::Data;
    if (inList(ext, {L"ttf", L"otf", L"woff", L"woff2", L"ttc"})) return Badge::Font;
    if (inList(ext, {L"app", L"exe", L"bin", L"dylib", L"so", L"a", L"command", L"msi", L"bat",
                     L"cmd", L"com"}))
        return Badge::Program;
    if (inList(ext, {L"psd", L"ai", L"sketch", L"fig", L"xd", L"indd", L"epub"}))
        return Badge::Design;
    return Badge::Plain;
}

void page(Canvas& c, const RECT& r, const std::wstring& ext) {
    Grid g(c, r);
    const double fold = 5;
    g.poly({{1.5, 0.5}, {11, 0.5}, {15.5, 5}, {15.5, 15.5}, {1.5, 15.5}}, Theme::white, false);
    g.poly({{1.5, 0.5}, {11, 0.5}, {15.5, 5}, {15.5, 15.5}, {1.5, 15.5}}, Theme::dark, true);
    g.line(11, 0.5, 11, 5, Theme::shadow);
    g.line(11, 5, 15.5, 5, Theme::shadow);

    Badge b = badgeFor(ext);
    const double ax = 3, ay = 6, aw = 8, ah = 7;
    switch (b) {
    case Badge::Image:
        g.rect(ax, ay + ah - 4, aw, 4, Theme::green);
        g.rect(ax, ay, aw, ah - 4, Theme::imageBlue);
        g.rect(ax + 1, ay + 1, 2, 2, Theme::folderTop);
        break;
    case Badge::Pdf:
        g.rect(ax, ay + 2, aw, 4, Theme::pdfRed);
        g.rect(ax + 1, ay + 3, aw - 2, 1, Theme::white);
        break;
    case Badge::Archive:
        g.rect(ax, ay, aw, ah, Theme::folderBody);
        g.poly({{ax, ay}, {ax + aw, ay}, {ax + aw, ay + ah}, {ax, ay + ah}}, Theme::dark, true);
        for (int i = 0; i < 3; ++i) g.rect(ax + 3, ay + 1 + i * 2, 2, 1, Theme::shadow);
        break;
    case Badge::Audio:
        g.line(ax + 5, ay + 1, ax + 5, ay + 4, Theme::dark);
        g.rect(ax + 6, ay + 1, 2, 2, Theme::dark);
        g.rect(ax + 3, ay + 4, 3, 2, Theme::dark);
        break;
    case Badge::Video:
        g.rect(ax, ay, aw, 4, Theme::dark);
        for (int i = 0; i < 3; ++i) g.rect(ax + i * 3, ay + 1, 1, 2, Theme::white);
        break;
    case Badge::Disk:
        g.rect(ax, ay + 1, aw, ah - 3, Theme::face);
        g.poly({{ax, ay + 1}, {ax + aw, ay + 1}, {ax + aw, ay + ah - 2}, {ax, ay + ah - 2}},
               Theme::dark, true);
        g.rect(ax + 3, ay + 3, 2, 2, Theme::shadow);
        break;
    case Badge::Doc:
        g.rect(ax, ay, aw, 3, Theme::docBlue);
        for (int i = 0; i < 3; ++i) g.line(ax, ay + 4.5 + i * 2, ax + (i == 2 ? 5 : 8), ay + 4.5 + i * 2,
                                            Theme::shadow);
        break;
    case Badge::Sheet:
        g.rect(ax, ay, aw, ah, Theme::sheetGreen);
        g.rect(ax, ay + 3, aw, 1, Theme::white);
        g.rect(ax + 3, ay, 1, ah, Theme::white);
        break;
    case Badge::Slide:
        g.rect(ax, ay, aw, ah, Theme::slideOrange);
        g.rect(ax + 1, ay + 1, aw - 2, ah - 2, Theme::white);
        g.rect(ax + 1, ay + 2, aw - 2, 1, Theme::dark);
        break;
    case Badge::Text:
        for (int i = 0; i < 4; ++i)
            g.line(ax, ay + 0.5 + i * 1.8, ax + (i % 2 ? 6 : 8), ay + 0.5 + i * 1.8, Theme::shadow);
        break;
    case Badge::Html:
        g.rect(ax, ay, aw, ah, Theme::webBlue);
        g.line(ax + 3, ay + 3, ax + 1, ay + 5, Theme::white);
        g.line(ax + 1, ay + 5, ax + 3, ay + 7, Theme::white);
        g.line(ax + 5, ay + 3, ax + 7, ay + 5, Theme::white);
        g.line(ax + 7, ay + 5, ax + 5, ay + 7, Theme::white);
        break;
    case Badge::Code:
        g.rect(ax, ay, aw, ah, Theme::codeTeal);
        g.line(ax + 3, ay + 2, ax + 1, ay + 5, Theme::white);
        g.line(ax + 1, ay + 5, ax + 3, ay + 8, Theme::white);
        g.line(ax + 5, ay + 2, ax + 7, ay + 5, Theme::white);
        g.line(ax + 7, ay + 5, ax + 5, ay + 8, Theme::white);
        break;
    case Badge::Data:
        g.rect(ax, ay, aw, ah, Theme::dataPurple);
        g.line(ax + 2, ay + 2, ax + 1, ay + 3, Theme::white);
        g.line(ax + 1, ay + 3, ax + 1, ay + 5, Theme::white);
        g.line(ax + 1, ay + 5, ax + 2, ay + 6, Theme::white);
        g.line(ax + 6, ay + 2, ax + 7, ay + 3, Theme::white);
        g.line(ax + 7, ay + 3, ax + 7, ay + 5, Theme::white);
        g.line(ax + 7, ay + 5, ax + 6, ay + 6, Theme::white);
        break;
    case Badge::Font:
        g.poly({{ax, ay}, {ax + aw, ay}, {ax + aw, ay + ah}, {ax, ay + ah}}, Theme::dark, true);
        g.line(ax + 4, ay + 2, ax + 3, ay + 6, Theme::dark);
        g.line(ax + 4, ay + 2, ax + 5, ay + 6, Theme::dark);
        g.line(ax + 3, ay + 4.5, ax + 5, ay + 4.5, Theme::dark);
        break;
    case Badge::Program:
        g.rect(ax + 1, ay + 1, aw - 2, ah - 2, Theme::shadow);
        g.rect(ax + 2, ay + 2, aw - 4, ah - 4, Theme::face);
        g.rect(ax + 3, ay + 2, 1, 1, Theme::dark);
        g.rect(ax + 6, ay + 2, 1, 1, Theme::dark);
        g.rect(ax + 3, ay + 5, 1, 1, Theme::dark);
        g.rect(ax + 6, ay + 5, 1, 1, Theme::dark);
        break;
    case Badge::Design:
        g.rect(ax + 1, ay + 1, aw - 2, ah - 2, Theme::designPink);
        g.rect(ax + 3, ay + 4, 2, 2, Theme::white);
        g.rect(ax + 3, ay + 2, 1, 1, Theme::white);
        break;
    case Badge::Plain:
        g.rect(ax, ay + ah - 3, aw, 3, Theme::faceHi);
        break;
    }
}

}  // namespace

namespace Icon {

void folder(Canvas& c, const RECT& r, bool open) {
    Grid g(c, r);
    const double tabH = 3.2;
    g.poly({{0.5, tabH}, {0.5, 0.5}, {6.4, 0.5}, {8.32, tabH}}, Theme::folderTop, false);
    g.poly({{0.5, tabH}, {0.5, 0.5}, {6.4, 0.5}, {8.32, tabH}}, Theme::dark, true);
    g.poly({{0.5, tabH}, {15.5, tabH}, {15.5, 15.5}, {0.5, 15.5}}, Theme::folderBody, false);
    g.poly({{0.5, tabH}, {15.5, tabH}, {15.5, 15.5}, {0.5, 15.5}}, Theme::dark, true);
    g.rect(1, 13.5, 14, 1, Theme::folderEdge);
    if (open) g.rect(2, tabH + 2, 12, 16 - tabH - 5, Theme::faceHi);
}

void document(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{1.5, 0.5}, {11, 0.5}, {15.5, 5}, {15.5, 15.5}, {1.5, 15.5}}, Theme::white, false);
    g.poly({{1.5, 0.5}, {11, 0.5}, {15.5, 5}, {15.5, 15.5}, {1.5, 15.5}}, Theme::dark, true);
    g.line(11, 0.5, 11, 5, Theme::shadow);
    g.line(11, 5, 15.5, 5, Theme::shadow);
    for (double y = 8; y < 13; y += 3) g.rect(3.5, y, 10, 1, Theme::shadow);
}

void computer(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.rect(1.5, 2.5, 13, 11, Theme::faceHi);
    g.poly({{1.5, 2.5}, {14.5, 2.5}, {14.5, 13.5}, {1.5, 13.5}}, Theme::dark, true);
    g.rect(3, 4, 10, 8, Theme::navy);
    g.rect(4.5, 5.5, 4, 1, Theme::dark);
    g.rect(4.5, 7.5, 6, 1, Theme::dark);
    g.rect(5, 0.5, 6, 2, Theme::faceHi);
    g.poly({{5, 0.5}, {11, 0.5}, {11, 2.5}, {5, 2.5}}, Theme::dark, true);
}

void views(Canvas& c, const RECT& r) {
    Grid g(c, r);
    struct Box { double x, y; COLORREF col; };
    const Box boxes[4] = {{0.5, 0.5, Theme::select}, {9, 0.5, Theme::folderBody},
                          {0.5, 9, Theme::folderBody}, {9, 9, Theme::folderBody}};
    for (auto& b : boxes) {
        g.rect(b.x, b.y, 6.5, 6.5, b.col);
        g.poly({{b.x, b.y}, {b.x + 6.5, b.y}, {b.x + 6.5, b.y + 6.5}, {b.x, b.y + 6.5}},
               Theme::dark, true);
    }
}

void file(Canvas& c, const RECT& r, const std::wstring& extensionLower) {
    page(c, r, extensionLower);
}

void drive(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{0.5, 3.5}, {15.5, 3.5}, {15.5, 12.5}, {0.5, 12.5}}, Theme::face, false);
    g.poly({{0.5, 3.5}, {15.5, 3.5}, {15.5, 12.5}, {0.5, 12.5}}, Theme::dark, true);
    g.rect(1.5, 4.5, 13, 1, Theme::white);
    g.rect(1.5, 5.5, 13, 2, Theme::shadow);
    g.rect(2.5, 8.5, 2, 2, Theme::green);
}

void arrowLeft(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{1, 8}, {8.8, 2}, {8.8, 5.44}, {15, 5.44}, {15, 10.56}, {8.8, 10.56}, {8.8, 14}},
           Theme::green, false);
    g.poly({{1, 8}, {8.8, 2}, {8.8, 5.44}, {15, 5.44}, {15, 10.56}, {8.8, 10.56}, {8.8, 14}},
           Theme::dark, true);
}

void arrowRight(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{15, 8}, {7.2, 2}, {7.2, 5.44}, {1, 5.44}, {1, 10.56}, {7.2, 10.56}, {7.2, 14}},
           Theme::green, false);
    g.poly({{15, 8}, {7.2, 2}, {7.2, 5.44}, {1, 5.44}, {1, 10.56}, {7.2, 10.56}, {7.2, 14}},
           Theme::dark, true);
}

void arrowUp(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{8, 1}, {14, 8}, {10.56, 8}, {10.56, 15}, {5.44, 15}, {5.44, 8}, {2, 8}},
           Theme::green, false);
    g.poly({{8, 1}, {14, 8}, {10.56, 8}, {10.56, 15}, {5.44, 15}, {5.44, 8}, {2, 8}},
           Theme::dark, true);
}

void refresh(Canvas& c, const RECT& r) {
    Grid g(c, r);
    const double cx = 8, cy = 8, rad = 5.3;
    POINT prev{};
    bool first = true;
    for (int deg = 50; deg <= 330; deg += 10) {
        double a = deg * 3.14159265358979 / 180.0;
        POINT p{g.X(cx + rad * std::cos(a)), g.Y(cy + rad * std::sin(a))};
        if (!first) c.line(prev.x, prev.y, p.x, p.y, Theme::green);
        prev = p;
        first = false;
    }
    const int tipX = g.X(cx + rad * std::cos(3.14159265358979 * 50 / 180));
    const int tipY = g.Y(cy + rad * std::sin(3.14159265358979 * 50 / 180));
    POINT tri[3] = {{tipX - g.L(1), tipY - g.L(5)}, {tipX + g.L(3), tipY + g.L(1)},
                    {tipX - g.L(3), tipY + g.L(3)}};
    c.polygonFill(tri, 3, Theme::dark);
}

void hidden(Canvas& c, const RECT& r) {
    // A dimmed folder with an eye: "show hidden files".
    Grid g(c, r);
    const double tabH = 3.2;
    g.poly({{0.5, tabH}, {0.5, 0.5}, {6.4, 0.5}, {8.32, tabH}}, Theme::faceHi, false);
    g.poly({{0.5, tabH}, {0.5, 0.5}, {6.4, 0.5}, {8.32, tabH}}, Theme::shadow, true);
    g.poly({{0.5, tabH}, {15.5, tabH}, {15.5, 15.5}, {0.5, 15.5}}, Theme::faceHi, false);
    g.poly({{0.5, tabH}, {15.5, tabH}, {15.5, 15.5}, {0.5, 15.5}}, Theme::shadow, true);

    RECT eye{g.X(8), g.Y(8.5), g.X(15), g.Y(13)};
    c.ellipseFill(eye, Theme::white);
    c.ellipseOutline(eye, Theme::dark);
    const int cx = (eye.left + eye.right) / 2, cy = (eye.top + eye.bottom) / 2;
    c.fill(cx - g.L(1), cy - g.L(1), g.L(2), g.L(2), Theme::dark);
}

void sortArrow(Canvas& c, const RECT& r, bool up) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    POINT p[3];
    if (up) {
        p[0] = {r.left, r.bottom - 1};
        p[1] = {r.left + w / 2, r.top + 1};
        p[2] = {r.right - 1, r.bottom - 1};
    } else {
        p[0] = {r.left, r.top + 1};
        p[1] = {r.left + w / 2, r.bottom - 1};
        p[2] = {r.right - 1, r.top + 1};
    }
    c.polygonFill(p, 3, Theme::dark);
}

void shortcut(Canvas& c, const RECT& r) {
    Grid g(c, r);
    g.poly({{3, 1}, {13, 1}, {15, 4}, {13, 8}, {8, 15}, {3, 8}, {1, 4}}, Theme::white, false);
    g.poly({{3, 1}, {13, 1}, {15, 4}, {13, 8}, {8, 15}, {3, 8}, {1, 4}}, Theme::dark, true);
    g.line(4, 4, 12, 4, Theme::shadow);
}

}  // namespace Icon
