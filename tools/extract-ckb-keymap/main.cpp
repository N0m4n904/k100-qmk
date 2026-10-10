// Dumps a ckb-next keymap as JSON.
//
// The K100 map is produced by a chain of C++ transformations off the K95 Platinum
// (see KeyMap::getMap in ckb-next's src/gui/keymap.cpp), so reimplementing it elsewhere
// would invite transcription errors. This links against ckb-next's own implementation and
// asks it for the finished map instead.
//
// Usage: extract-ckb-keymap <model> <layout>   e.g. extract-ckb-keymap k100 de

#include <cstdio>
#include <cstring>

#include <QString>
#include <QTextStream>

#include "keymap.h"

// Some labels carry embedded newlines, e.g. the Pause key is "Pause\nBreak".
static QString escape(const QString& value) {
    QString out;
    for (const QChar c : value) {
        switch (c.unicode()) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c.unicode() < 0x20)
                    out += QString("\\u%1").arg(static_cast<int>(c.unicode()), 4, 16, QChar('0'));
                else
                    out += c;
        }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <model> <layout>\n", argv[0]);
        return 2;
    }

    const KeyMap::Model model = KeyMap::getModel(argv[1]);
    const KeyMap::Layout layout = KeyMap::getLayout(argv[2]);

    if (model == KeyMap::NO_MODEL) {
        std::fprintf(stderr, "unknown model '%s'\n", argv[1]);
        return 1;
    }
    if (layout == KeyMap::NO_LAYOUT) {
        std::fprintf(stderr, "unknown layout '%s'\n", argv[2]);
        return 1;
    }

    const KeyMap map(model, layout);

    QTextStream out(stdout);
    out << "{\n";
    out << "  \"model\": \"" << escape(KeyMap::getModel(model)) << "\",\n";
    out << "  \"layout\": \"" << escape(KeyMap::getLayout(layout)) << "\",\n";
    out << "  \"width\": " << map.width() << ",\n";
    out << "  \"height\": " << map.height() << ",\n";
    out << "  \"count\": " << map.count() << ",\n";
    out << "  \"keys\": [\n";

    // Sorted so the output is stable across runs; QHash iteration order is not.
    QStringList names = map.keys();
    names.sort();

    for (int i = 0; i < names.size(); i++) {
        const Key key = map.key(names[i]);
        out << "    {"
            << "\"name\": \"" << escape(names[i]) << "\", "
            << "\"label\": \"" << escape(key.friendlyName(false)) << "\", "
            << "\"storage\": \"" << escape(key.storageName()) << "\", "
            << "\"x\": " << key.x << ", "
            << "\"y\": " << key.y << ", "
            << "\"width\": " << key.width << ", "
            << "\"height\": " << key.height << ", "
            << "\"has_led\": " << (key.hasLed ? "true" : "false") << ", "
            << "\"has_scan\": " << (key.hasScan ? "true" : "false")
            << "}" << (i + 1 < names.size() ? "," : "") << "\n";
    }

    out << "  ]\n}\n";
    return 0;
}
