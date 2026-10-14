// Язык интерфейса.  Исходные строки — русские; для английского интерфейса
// они переводятся по словарю (i18n_en.inc).  Язык выбирается при запуске:
// по настройке пользователя, иначе по языку системы.
#pragma once

#include <QString>

namespace ng {

enum class Lang { Ru, En };

void setLanguage(Lang l);
Lang language();
bool isEnglish();
// Перевод строки: T("Открыть…") → "Open…" в английском интерфейсе
QString T(const char *ru);
QString T(const QString &ru);
// Строки без перевода (для проверки полноты словаря в тестах)
QStringList missingTranslations();

}  // namespace ng
