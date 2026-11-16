// Floating live inspector: interpretations of the byte under the cursor or the
// first bytes of the current selection. Pure data formatting lives in the static
// helper so it can be unit tested without a widget.
#pragma once

#include "core/HexDocument.h"

#include <QFrame>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

class QLabel;

namespace pkg::ui {

class ByteInspector : public QFrame {
    Q_OBJECT

public:
    explicit ByteInspector(QWidget* parent = nullptr);

    void setDocument(const HexDocument* document);
    void showOffset(quint64 offset);
    void showSelection(quint64 start, quint64 length);
    void clear();

    /// Key/value rows for up to eight bytes: hex, decimal, binary, ASCII,
    /// signed/unsigned 8/16/32/64-bit integers in both byte orders, and floats.
    [[nodiscard]] static QList<QPair<QString, QString>> interpretations(const QByteArray& bytes);

signals:
    void closeRequested();

private:
    void updateLabels();

    const HexDocument* m_document = nullptr;
    quint64 m_offset = 0;
    quint64 m_length = 0;
    bool m_hasValue = false;

    QLabel* m_title = nullptr;
    QLabel* m_offsetLabel = nullptr;
    QHash<QString, QLabel*> m_valueLabels;
};

}  // namespace pkg::ui
