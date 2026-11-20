#include "ui/ThemeEditorDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
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

    auto* backgroundRow = new QWidget(this);
    auto* backgroundLayout = new QHBoxLayout(backgroundRow);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    backgroundLayout->setSpacing(6);
    m_backgroundEdit = new QLineEdit(backgroundRow);
    m_backgroundEdit->setReadOnly(true);
    m_backgroundEdit->setText(m_theme.backgroundImage);
    m_backgroundEdit->setPlaceholderText(
        QStringLiteral("No image \u2014 the theme uses flat colours"));
    backgroundLayout->addWidget(m_backgroundEdit, 1);
    auto* browse = new QPushButton(QStringLiteral("Choose\u2026"), backgroundRow);
    backgroundLayout->addWidget(browse);
    auto* clear = new QPushButton(QStringLiteral("Clear"), backgroundRow);
    backgroundLayout->addWidget(clear);
    form->addRow(QStringLiteral("Background image"), backgroundRow);

    m_opacity = new QSpinBox(this);
    m_opacity->setRange(20, 100);
    m_opacity->setValue(m_theme.backgroundOpacity);
    m_opacity->setSuffix(QStringLiteral("%"));
    m_opacity->setToolTip(
        QStringLiteral("Opacity of the panels over the image. Lower values show more of the "
                       "picture; 100% hides it."));
    form->addRow(QStringLiteral("Panel opacity"), m_opacity);

    layout->addLayout(form);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Background image"), QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
        if (path.isEmpty())
            return;
        m_theme.backgroundImage = path;
        m_backgroundEdit->setText(path);
        if (m_theme.backgroundOpacity >= 100)
            m_opacity->setValue(82);
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        m_theme.backgroundImage.clear();
        m_backgroundEdit->clear();
    });
    connect(m_opacity, &QSpinBox::valueChanged, this,
            [this](int value) { m_theme.backgroundOpacity = value; });

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
        m_theme.backgroundOpacity = m_opacity->value();
        if (!m_theme.backgroundImage.isEmpty()
            && !QFileInfo::exists(m_theme.backgroundImage)) {
            m_error->setText(QStringLiteral("The background image does not exist"));
            return;
        }
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
