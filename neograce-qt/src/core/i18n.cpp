#include "i18n.h"

#include <QHash>
#include <QSet>

namespace ng {

static Lang g_lang = Lang::Ru;

static const QHash<QString, QString> &dict()
{
    static const QHash<QString, QString> d = [] {
        QHash<QString, QString> h;
        static const char *const pairs[][2] = {
#include "i18n_en.inc"
        };
        for (const auto &p : pairs)
            h.insert(QString::fromUtf8(p[0]), QString::fromUtf8(p[1]));
        return h;
    }();
    return d;
}

static QSet<QString> &missing()
{
    static QSet<QString> m;
    return m;
}

void setLanguage(Lang l) { g_lang = l; }
Lang language() { return g_lang; }
bool isEnglish() { return g_lang == Lang::En; }

QString T(const QString &ru)
{
    if (g_lang == Lang::Ru || ru.isEmpty())
        return ru;
    auto it = dict().find(ru);
    if (it != dict().end())
        return *it;
    missing().insert(ru);
    return ru;
}

QString T(const char *ru) { return T(QString::fromUtf8(ru)); }

QStringList missingTranslations() { return missing().values(); }

}  // namespace ng
