#include "core/FormatInfo.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

namespace pkg {

QString formatKindName(FormatKind kind)
{
    switch (kind) {
    case FormatKind::Unknown:
        return QStringLiteral("Unknown");
    case FormatKind::RawBinary:
        return QStringLiteral("Raw binary");
    case FormatKind::Elf:
        return QStringLiteral("ELF");
    case FormatKind::Pe:
        return QStringLiteral("PE");
    case FormatKind::Zip:
        return QStringLiteral("ZIP");
    case FormatKind::Apk:
        return QStringLiteral("Android APK");
    case FormatKind::VitaPkg:
        return QStringLiteral("PSP/PS Vita PKG");
    case FormatKind::Ps4Pkg:
        return QStringLiteral("PS4 PKG");
    }
    return QStringLiteral("Unknown");
}

QString PackageInfo::toPlainText() const
{
    QStringList lines;
    lines << QStringLiteral("Format: %1").arg(formatName);
    if (!parserId.isEmpty())
        lines << QStringLiteral("Parser: %1 %2").arg(parserId, parserVersion);
    if (!summary.isEmpty())
        lines << QStringLiteral("Summary: %1").arg(summary);
    lines << QString();

    for (const InfoSection& section : sections) {
        lines << QStringLiteral("== %1 ==").arg(section.title);
        for (const InfoField& field : section.fields) {
            QString line = QStringLiteral("%1: %2").arg(field.name, field.value);
            if (!field.note.isEmpty())
                line += QStringLiteral("  (%1)").arg(field.note);
            lines << line;
        }
        lines << QString();
    }

    if (!warnings.isEmpty()) {
        lines << QStringLiteral("== Warnings ==");
        for (const QString& warning : warnings)
            lines << QStringLiteral("- %1").arg(warning);
    }
    return lines.join(QLatin1Char('\n'));
}

QJsonObject PackageInfo::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), 1);
    object.insert(QStringLiteral("kind"), static_cast<int>(kind));
    object.insert(QStringLiteral("formatName"), formatName);
    object.insert(QStringLiteral("parserId"), parserId);
    object.insert(QStringLiteral("parserVersion"), parserVersion);
    object.insert(QStringLiteral("summary"), summary);
    object.insert(QStringLiteral("partial"), partial);

    QJsonArray warningArray;
    for (const QString& warning : warnings)
        warningArray.append(warning);
    object.insert(QStringLiteral("warnings"), warningArray);

    QJsonArray sectionArray;
    for (const InfoSection& section : sections) {
        QJsonObject sectionObject;
        sectionObject.insert(QStringLiteral("title"), section.title);
        QJsonArray fieldArray;
        for (const InfoField& field : section.fields) {
            QJsonObject fieldObject;
            fieldObject.insert(QStringLiteral("name"), field.name);
            fieldObject.insert(QStringLiteral("value"), field.value);
            if (!field.note.isEmpty())
                fieldObject.insert(QStringLiteral("note"), field.note);
            fieldArray.append(fieldObject);
        }
        sectionObject.insert(QStringLiteral("fields"), fieldArray);
        sectionArray.append(sectionObject);
    }
    object.insert(QStringLiteral("sections"), sectionArray);
    return object;
}

Result<PackageInfo> PackageInfo::fromJson(const QJsonObject& object)
{
    if (object.isEmpty())
        return makeError(QStringLiteral("Empty analysis object"));

    const int kindValue = object.value(QStringLiteral("kind")).toInt(-1);
    if (kindValue < static_cast<int>(FormatKind::Unknown) ||
        kindValue > static_cast<int>(FormatKind::Ps4Pkg))
        return makeError(QStringLiteral("Unknown format kind in cached analysis"));

    PackageInfo info;
    info.kind = static_cast<FormatKind>(kindValue);
    info.formatName = object.value(QStringLiteral("formatName")).toString();
    info.parserId = object.value(QStringLiteral("parserId")).toString();
    info.parserVersion = object.value(QStringLiteral("parserVersion")).toString();
    info.summary = object.value(QStringLiteral("summary")).toString();
    info.partial = object.value(QStringLiteral("partial")).toBool(false);

    for (const QJsonValue& value : object.value(QStringLiteral("warnings")).toArray())
        info.warnings << value.toString();

    for (const QJsonValue& sectionValue : object.value(QStringLiteral("sections")).toArray()) {
        const QJsonObject sectionObject = sectionValue.toObject();
        InfoSection section;
        section.title = sectionObject.value(QStringLiteral("title")).toString();
        for (const QJsonValue& fieldValue :
             sectionObject.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject fieldObject = fieldValue.toObject();
            InfoField field;
            field.name = fieldObject.value(QStringLiteral("name")).toString();
            field.value = fieldObject.value(QStringLiteral("value")).toString();
            field.note = fieldObject.value(QStringLiteral("note")).toString();
            section.fields.append(field);
        }
        info.sections.append(section);
    }
    return info;
}

}  // namespace pkg
