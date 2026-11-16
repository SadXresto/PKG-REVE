#include "ui/ByteInspector.h"

#include "core/FormatUtil.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <bit>
#include <cmath>

namespace pkg::ui {
namespace {

QString binaryText(quint8 value)
{
    QString bits = QString::number(value, 2).rightJustified(8, QLatin1Char('0'));
    QString grouped;
    for (int i = 0; i < bits.size(); ++i) {
        if (i == 4)
            grouped += QLatin1Char(' ');
        grouped += bits.at(i);
    }
    return grouped;
}

QString printableText(const QByteArray& bytes)
{
    QString text;
    for (const char raw : bytes) {
        const auto value = static_cast<quint8>(raw);
        text += (value >= 0x20U && value < 0x7FU) ? QChar::fromLatin1(static_cast<char>(value))
                                                  : QLatin1Char('.');
    }
    return text;
}

QString floatText(double value)
{
    if (std::isnan(value))
        return QStringLiteral("nan");
    if (std::isinf(value))
        return value < 0 ? QStringLiteral("-inf") : QStringLiteral("+inf");
    return QString::number(value, 'g', 9);
}

quint16 u16(const QByteArray& bytes, bool little)
{
    const auto b0 = static_cast<quint8>(bytes.at(0));
    const auto b1 = static_cast<quint8>(bytes.at(1));
    return little ? static_cast<quint16>(b0 | (b1 << 8))
                  : static_cast<quint16>((b0 << 8) | b1);
}

quint32 u32(const QByteArray& bytes, bool little)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        const int index = little ? i : (3 - i);
        value |= static_cast<quint32>(static_cast<quint8>(bytes.at(index))) << (8 * i);
    }
    return value;
}

quint64 u64(const QByteArray& bytes, bool little)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        const int index = little ? i : (7 - i);
        value |= static_cast<quint64>(static_cast<quint8>(bytes.at(index))) << (8 * i);
    }
    return value;
}

}  // namespace

QList<QPair<QString, QString>> ByteInspector::interpretations(const QByteArray& bytes)
{
    QList<QPair<QString, QString>> rows;
    if (bytes.isEmpty())
        return rows;

    const auto first = static_cast<quint8>(bytes.at(0));
    rows.append({QStringLiteral("Hex"), QString::fromLatin1(bytes.toHex(' ').toUpper())});
    rows.append({QStringLiteral("Decimal"), QString::number(first)});
    rows.append({QStringLiteral("Binary"), binaryText(first)});
    rows.append({QStringLiteral("ASCII"), printableText(bytes)});
    rows.append({QStringLiteral("Unsigned 8"), QString::number(first)});
    rows.append({QStringLiteral("Signed 8"), QString::number(static_cast<qint8>(first))});

    const QString missing = QStringLiteral("\u2014");
    rows.append({QStringLiteral("UInt16 LE"),
                 bytes.size() >= 2 ? QString::number(u16(bytes, true)) : missing});
    rows.append({QStringLiteral("UInt16 BE"),
                 bytes.size() >= 2 ? QString::number(u16(bytes, false)) : missing});
    rows.append({QStringLiteral("Int16 LE"),
                 bytes.size() >= 2 ? QString::number(std::bit_cast<qint16>(u16(bytes, true)))
                                   : missing});
    rows.append({QStringLiteral("Int16 BE"),
                 bytes.size() >= 2 ? QString::number(std::bit_cast<qint16>(u16(bytes, false)))
                                   : missing});

    rows.append({QStringLiteral("UInt32 LE"),
                 bytes.size() >= 4 ? QString::number(u32(bytes, true)) : missing});
    rows.append({QStringLiteral("UInt32 BE"),
                 bytes.size() >= 4 ? QString::number(u32(bytes, false)) : missing});
    rows.append({QStringLiteral("Int32 LE"),
                 bytes.size() >= 4 ? QString::number(std::bit_cast<qint32>(u32(bytes, true)))
                                   : missing});
    rows.append({QStringLiteral("Int32 BE"),
                 bytes.size() >= 4 ? QString::number(std::bit_cast<qint32>(u32(bytes, false)))
                                   : missing});

    rows.append({QStringLiteral("UInt64 LE"),
                 bytes.size() >= 8 ? QString::number(u64(bytes, true)) : missing});
    rows.append({QStringLiteral("UInt64 BE"),
                 bytes.size() >= 8 ? QString::number(u64(bytes, false)) : missing});
    rows.append({QStringLiteral("Int64 LE"),
                 bytes.size() >= 8 ? QString::number(std::bit_cast<qint64>(u64(bytes, true)))
                                   : missing});
    rows.append({QStringLiteral("Int64 BE"),
                 bytes.size() >= 8 ? QString::number(std::bit_cast<qint64>(u64(bytes, false)))
                                   : missing});

    rows.append({QStringLiteral("Float32 LE"),
                 bytes.size() >= 4
                     ? floatText(static_cast<double>(std::bit_cast<float>(u32(bytes, true))))
                     : missing});
    rows.append({QStringLiteral("Float32 BE"),
                 bytes.size() >= 4
                     ? floatText(static_cast<double>(std::bit_cast<float>(u32(bytes, false))))
                     : missing});
    rows.append({QStringLiteral("Float64 LE"),
                 bytes.size() >= 8 ? floatText(std::bit_cast<double>(u64(bytes, true))) : missing});
    rows.append({QStringLiteral("Float64 BE"),
                 bytes.size() >= 8 ? floatText(std::bit_cast<double>(u64(bytes, false))) : missing});
    return rows;
}

ByteInspector::ByteInspector(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("byteInspector"));
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 10);
    layout->setSpacing(6);

    auto* titleRow = new QHBoxLayout();
    m_title = new QLabel(QStringLiteral("BYTE INSPECTOR"), this);
    m_title->setObjectName(QStringLiteral("inspectorTitle"));
    titleRow->addWidget(m_title, 1);
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("inspectorClose"));
    close->setText(QStringLiteral("\u2715"));
    close->setToolTip(QStringLiteral("Hide inspector"));
    titleRow->addWidget(close);
    layout->addLayout(titleRow);

    m_offsetLabel = new QLabel(this);
    m_offsetLabel->setObjectName(QStringLiteral("inspectorValue"));
    layout->addWidget(m_offsetLabel);

    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    const auto rows = interpretations(QByteArray(8, '\0'));
    int row = 0;
    for (const auto& entry : rows) {
        auto* key = new QLabel(entry.first, this);
        key->setObjectName(QStringLiteral("inspectorKey"));
        auto* value = new QLabel(entry.second, this);
        value->setObjectName(QStringLiteral("inspectorValue"));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_valueLabels.insert(entry.first, value);
        grid->addWidget(key, row, 0);
        grid->addWidget(value, row, 1);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    setFixedWidth(340);
    connect(close, &QToolButton::clicked, this, &ByteInspector::closeRequested);
    clear();
}

void ByteInspector::setDocument(const HexDocument* document)
{
    m_document = document;
    clear();
}

void ByteInspector::showOffset(quint64 offset)
{
    if (m_document == nullptr)
        return;
    m_offset = offset;
    m_length = 1;
    m_hasValue = true;
    updateLabels();
    show();
    raise();
}

void ByteInspector::showSelection(quint64 start, quint64 length)
{
    if (m_document == nullptr || length == 0)
        return;
    m_offset = start;
    m_length = length;
    m_hasValue = true;
    updateLabels();
    show();
    raise();
}

void ByteInspector::clear()
{
    m_hasValue = false;
    m_offset = 0;
    m_length = 0;
    m_offsetLabel->setText(QStringLiteral("Hover or select a byte"));
    for (auto it = m_valueLabels.begin(); it != m_valueLabels.end(); ++it)
        it.value()->setText(QStringLiteral("\u2014"));
    hide();
}

void ByteInspector::updateLabels()
{
    if (!m_hasValue || m_document == nullptr)
        return;

    const quint64 start = m_offset;
    const quint64 available = m_document->size() > start ? m_document->size() - start : 0;
    const quint64 wanted = m_length > 1 ? std::min<quint64>(m_length, 8) : std::min<quint64>(8, available);
    if (available == 0) {
        clear();
        return;
    }

    QString range = QStringLiteral("offset %1").arg(fmt::hexPrefixed(start, 8));
    if (m_length > 1)
        range += QStringLiteral("  \u00B7  %1 bytes").arg(m_length);
    m_offsetLabel->setText(range);

    auto bytes = m_document->readMerged(start, wanted);
    const QByteArray data = bytes ? bytes.value() : QByteArray();
    for (const auto& row : interpretations(data)) {
        if (QLabel* label = m_valueLabels.value(row.first, nullptr))
            label->setText(row.second);
    }
    adjustSize();
}

}  // namespace pkg::ui
