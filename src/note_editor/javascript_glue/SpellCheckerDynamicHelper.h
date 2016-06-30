#ifndef LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_SPELL_CHECKER_DYNAMIC_HELPER_H
#define LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_SPELL_CHECKER_DYNAMIC_HELPER_H

#include <quentier/utility/Qt4Helper.h>
#include <QObject>
#include <QStringList>
#include <QVariant>

namespace quentier {

class SpellCheckerDynamicHelper: public QObject
{
    Q_OBJECT
public:
    explicit SpellCheckerDynamicHelper(QObject * parent = Q_NULLPTR);

Q_SIGNALS:
    void lastEnteredWords(QStringList words);

public Q_SLOTS:

    // NOTE: workarounding https://bugreports.qt.io/browse/QTBUG-39951 - JavaScript array doesn't get automatically converted to QVariant
#ifdef USE_QT_WEB_ENGINE
    void setLastEnteredWords(QVariant words);
#else
    void setLastEnteredWords(QVariantList words);
#endif
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_JAVASCRIPT_GLUE_SPELL_CHECKER_DYNAMIC_HELPER_H
