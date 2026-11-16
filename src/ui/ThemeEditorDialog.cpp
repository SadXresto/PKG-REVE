#include "ui/ThemeEditorDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace pkg::ui {
namespace {

QString roleLabel(const QString& role)
{
    QString label;
    bool upperNext = true;
    for (const QChar character : role) {
        if (character.isUpper()) {
            if (!label.isEmpty())
                label += QLatin1Char(' ');
            label += character;
            upperNext = false;
            continue;
        }
        label += upperNext ? character.toUpper() : character;
        upperNext = false;
    }
    return label;
}

}  // namespace

ThemeEditorDialog::ThemeEditorDialog(const Theme& base, QWidget* parent)
    : QDialog(parent), m_theme(base)
{
    setWindowTitle(QStringLiteral("Theme editor"));
    setModal(true);
    resize(640, 600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(m_theme.name, this);
    form->addRow(QStringLiteral("Name"), m_nameEdit);

    m_idEdit = new QLineEdit(m_theme.id, this);
    m_idEdit->setToolTip(
        QStringLiteral("Unique identifier, for example mytheme.dark"));
    form->addRow(QStringLiteral("Identifier"), m_idEdit);

    m_darkBox = new QCheckBox(QStringLiteral("Dark theme"), this);
    m_darkBox->setChecked(m_theme.dark);
    form->addRow(QString(), m_darkBox);
    layout->addLayout(form);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* colorHost = new QWidget(scroll);
    auto* grid = new QGridLayout(colorHost);
    grid->setContentsMargins(0, 0, 6, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);

    int row = 0;
    for (const QString& role : Theme::editableRoles()) {
        auto* label = new QLabel(roleLabel(role), colorHost);
        label->setToolTip(role);
        grid->addWidget(label, row, 0);

        auto* swatch = new QPushButton(colorHost);
        swatch->setMinimumWidth(110);
        swatch->setCursor(Qt::PointingHandCursor);
        connect(swatch, &QPushButton::clicked, this, [this, role]() { pickColor(role); });
        m_swatches.insert(role, swatch);
        grid->addWidget(swatch, row, 1);
        refreshSwatch(role);
        ++row;
    }
    grid->setColumnStretch(2, 1);
    scroll->setWidget(colorHost);
    layout->addWidget(scroll, 1);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("hintLabel"));
    m_error->setWordWrap(true);
    layout->addWidget(m_error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Save theme"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        Theme candidate = theme();
        QString reason;
        if (!candidate.isValid(&reason)) {
            m_error->setText(reason);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ThemeEditorDialog::pickColor(const QString& role)
{
    const QColor current = m_theme.color(role, QColor(QStringLiteral("#808080")));
    const QColor chosen = QColorDialog::getColor(current, this,
                                                 QStringLiteral("Colour for %1").arg(role));
    if (!chosen.isValid())
        return;
    m_theme.colors.insert(role, chosen);
    refreshSwatch(role);
}

void ThemeEditorDialog::refreshSwatch(const QString& role)
{
    QPushButton* swatch = m_swatches.value(role);
    if (swatch == nullptr)
        return;
    const QColor color = m_theme.color(role, QColor(Qt::gray));
    const bool darkText = color.lightness() > 140;
    swatch->setText(color.name(QColor::HexRgb).toUpper());
    swatch->setStyleSheet(QStringLiteral("background-color: %1; color: %2; border: 1px solid %3;")
                              .arg(color.name(QColor::HexRgb),
                                   darkText ? QStringLiteral("#101010")
                                            : QStringLiteral("#f0f0f0"),
                                   QStringLiteral("#606060")));
}

}  // namespace pkg::ui
