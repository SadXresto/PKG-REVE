// Documented file signatures found in a file prefix.
//
// A hit is evidence, not proof: confidence expresses how much validation was
// performed (exact magic at offset 0 and structural checks score high, a bare
// two-byte match embedded in data scores low). Parsers remain the authority on
// whether a file really is a supported format.
#pragma once

#include <QJsonObject>
#include <QString>

namespace pkg {

struct SignatureHit {
    QString name;
    QString category;
    quint64 offset = 0;
    quint64 length = 0;
    int confidence = 0;  // 0-100
    QString note;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static SignatureHit fromJson(const QJsonObject& object);
};

}  // namespace pkg
