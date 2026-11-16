// Format independent analysis result produced by a parser.
#pragma once

#include "core/Result.h"
#include "core/Signature.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace pkg {

enum class FormatKind {
    Unknown = 0,
    RawBinary,
    Elf,
    Pe,
    Zip,
    Apk,
    VitaPkg,   // PSP / PS Vita / PS3 PKG container family
    Ps4Pkg,
};

[[nodiscard]] QString formatKindName(FormatKind kind);

struct InfoField {
    QString name;
    QString value;
    QString note;
};

struct InfoSection {
    QString title;
    QList<InfoField> fields;
};

struct PackageInfo {
    FormatKind kind = FormatKind::Unknown;
    QString formatName = QStringLiteral("Unknown");
    QString parserId;
    QString parserVersion;
    QString summary;
    QList<InfoSection> sections;
    QStringList warnings;
    /// Documented signatures found by the bounded scanner (including embedded ones).
    QVector<SignatureHit> signatures;

    /// True when only part of the structure could be described (size caps,
    /// truncated input, unsupported revisions).
    bool partial = false;

    [[nodiscard]] QString toPlainText() const;
    [[nodiscard]] bool isEmpty() const { return sections.isEmpty(); }

    /// Used by the analysis cache and future report interchange.
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Result<PackageInfo> fromJson(const QJsonObject& object);
};

}  // namespace pkg
