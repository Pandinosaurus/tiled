/*
 * propertytype.cpp
 * Copyright 2021, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of libtiled.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "propertytype.h"

#include "containerhelpers.h"
#include "properties.h"

#include <QVector>

#include <algorithm>

namespace Tiled {

int PropertyType::nextId = 0;

/**
 * This function returns a PropertyValue instance, which stores the internal
 * value along with the type.
 */
QVariant PropertyType::wrap(const QVariant &value) const
{
    return QVariant::fromValue(PropertyValue { value, id });
}

/**
 * This function is called with the value stored in a PropertyValue. It is
 * supposed to prepare the value for saving.
 *
 * The default implementation just calls ExportContext::toExportValue.
 */
ExportValue PropertyType::toExportValue(const QVariant &value, const ExportContext &context) const
{
    ExportValue result = context.toExportValue(value);
    result.propertyTypeName = name;
    return result;
}

QVariant PropertyType::toPropertyValue(const QVariant &value, const ExportContext &) const
{
    return wrap(value);
}

QVariantMap PropertyType::toVariant(const ExportContext &) const
{
    return {
        { QStringLiteral("type"), typeToString(type) },
        { QStringLiteral("id"), id },
        { QStringLiteral("name"), name },
    };
}

/**
 * Creates a PropertyType instance based on the given variant.
 *
 * After loading all property types, PropertyType::resolveDependencies should
 * be called on each of them. This two step process allows class members to
 * refer to other types, regardless of their order.
 */
std::unique_ptr<PropertyType> PropertyType::createFromVariant(const QVariantMap &variant)
{
    std::unique_ptr<PropertyType> propertyType;

    const int id = variant.value(QStringLiteral("id")).toInt();
    const QString name = variant.value(QStringLiteral("name")).toString();
    const PropertyType::Type type = PropertyType::typeFromString(variant.value(QStringLiteral("type")).toString());

    switch (type) {
    case PropertyType::PT_Invalid:
        break;
    case PropertyType::PT_Class:
        propertyType = std::make_unique<ClassPropertyType>(name);
        break;
    case PropertyType::PT_Enum:
        propertyType = std::make_unique<EnumPropertyType>(name);
        break;
    }

    if (propertyType) {
        propertyType->id = id;
        propertyType->fromVariant(variant);
        nextId = std::max(nextId, id);
    }

    return propertyType;
}

PropertyType::Type PropertyType::typeFromString(const QString &string)
{
    if (string == QLatin1String("enum") || string.isEmpty())    // empty check for compatibility
        return PT_Enum;
    if (string == QLatin1String("class"))
        return PT_Class;
    return PT_Invalid;
}

QString PropertyType::typeToString(Type type)
{
    switch (type) {
    case PT_Class:
        return QStringLiteral("class");
    case PT_Enum:
        return QStringLiteral("enum");
    case PT_Invalid:
        break;
    }
    return QStringLiteral("invalid");
}

// EnumPropertyType

ExportValue EnumPropertyType::toExportValue(const QVariant &value, const ExportContext &context) const
{
    ExportValue result;

    // Convert enum values to their string if desired
    if (value.userType() == QMetaType::Int && storageType == StringValue) {
        const int intValue = value.toInt();

        if (valuesAsFlags) {
            QString stringValue;

            for (int i = 0; i < values.size(); ++i) {
                if (intValue & (1 << i)) {
                    if (!stringValue.isEmpty())
                        stringValue.append(QLatin1Char(','));
                    stringValue.append(values.at(i));
                }
            }

            return PropertyType::toExportValue(stringValue, context);
        } else if (intValue >= 0 && intValue < values.size()) {
            return PropertyType::toExportValue(values.at(intValue), context);
        }
    }

    return PropertyType::toExportValue(value, context);
}

QVariant EnumPropertyType::toPropertyValue(const QVariant &value, const ExportContext &) const
{
    // Convert enum values stored as string, if possible
    if (value.userType() == QMetaType::QString) {
        const QString stringValue = value.toString();

        if (valuesAsFlags) {
            int flags = 0;

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
            const QVector<QStringRef> stringValues = stringValue.splitRef(QLatin1Char(','), QString::SkipEmptyParts);
#elif QT_VERSION < QT_VERSION_CHECK(6,0,0)
            const QVector<QStringRef> stringValues = stringValue.splitRef(QLatin1Char(','), Qt::SkipEmptyParts);
#else
            const QList<QStringView> stringValues = QStringView(stringValue).split(QLatin1Char(','), Qt::SkipEmptyParts);
#endif

            for (const auto &stringValue : stringValues) {
                const int index = indexOf(values, stringValue);

                // In case of any unrecognized flag name we keep the original
                // string value, to prevent silent data loss.
                if (index == -1)
                    return wrap(value);

                flags |= 1 << index;
            }

            return wrap(flags);
        }

        const int index = values.indexOf(stringValue);
        if (index != -1)
            return wrap(index);
    }

    return wrap(value);
}

QVariant EnumPropertyType::defaultValue() const
{
    return 0;
}

QVariantMap EnumPropertyType::toVariant(const ExportContext &context) const
{
    auto variant = PropertyType::toVariant(context);
    variant.insert(QStringLiteral("storageType"), storageTypeToString(storageType));
    variant.insert(QStringLiteral("values"), values);
    variant.insert(QStringLiteral("valuesAsFlags"), valuesAsFlags);
    return variant;
}

void EnumPropertyType::fromVariant(const QVariantMap &variant)
{
    storageType = storageTypeFromString(variant.value(QStringLiteral("storageType")).toString());
    values = variant.value(QStringLiteral("values")).toStringList();
    valuesAsFlags = variant.value(QStringLiteral("valuesAsFlags"), false).toBool();
}

EnumPropertyType::StorageType EnumPropertyType::storageTypeFromString(const QString &string)
{
    if (string == QLatin1String("int"))
        return IntValue;
    return StringValue;
}

QString EnumPropertyType::storageTypeToString(StorageType type)
{
    switch (type) {
    case IntValue:
        return QStringLiteral("int");
    case StringValue:
        break;
    }
    return QStringLiteral("string");
}

// ClassPropertyType

ExportValue ClassPropertyType::toExportValue(const QVariant &value, const ExportContext &context) const
{
    ExportValue result;
    Properties properties = value.toMap();

    QMutableMapIterator<QString, QVariant> it(properties);
    while (it.hasNext()) {
        it.next();

        ExportValue exportValue = context.toExportValue(it.value());
        it.setValue(exportValue.value);
    }

    return PropertyType::toExportValue(properties, context);
}

QVariant ClassPropertyType::toPropertyValue(const QVariant &value, const ExportContext &context) const
{
    Properties properties = value.toMap();

    QMutableMapIterator<QString, QVariant> it(properties);
    while (it.hasNext()) {
        it.next();

        const QVariant classMember = members.value(it.key());
        if (!classMember.isValid())  // ignore removed members
            continue;

        QVariant propertyValue = context.toPropertyValue(it.value(), classMember.userType());

        // Wrap the value in its custom property type when applicable
        if (classMember.userType() == propertyValueId()) {
            const PropertyValue classMemberValue = classMember.value<PropertyValue>();
            if (const PropertyType *propertyType = context.types().findTypeById(classMemberValue.typeId))
                propertyValue = propertyType->toPropertyValue(propertyValue, context);
        }

        it.setValue(propertyValue);
    }

    return wrap(properties);
}

QVariant ClassPropertyType::defaultValue() const
{
    return QVariantMap();
}

QVariantMap ClassPropertyType::toVariant(const ExportContext &context) const
{
    QVariantList members;

    QMapIterator<QString,QVariant> it(this->members);
    while (it.hasNext()) {
        it.next();

        const auto exportValue = context.toExportValue(it.value());

        QVariantMap member {
            { QStringLiteral("name"), it.key() },
            { QStringLiteral("type"), exportValue.typeName },
            { QStringLiteral("value"), exportValue.value },
        };

        if (!exportValue.propertyTypeName.isEmpty())
            member.insert(QStringLiteral("propertyType"), exportValue.propertyTypeName);

        members.append(member);
    }

    auto variant = PropertyType::toVariant(context);
    variant.insert(QStringLiteral("members"), members);
    return variant;
}

void ClassPropertyType::fromVariant(const QVariantMap &variant)
{
    const auto membersList = variant.value(QStringLiteral("members")).toList();
    for (const auto &member : membersList) {
        const QVariantMap map = member.toMap();
        const QString name = map.value(QStringLiteral("name")).toString();

        members.insert(name, map);
    }
}

void ClassPropertyType::resolveDependencies(const ExportContext &context)
{
    for (auto &member : members) {
        const QVariantMap map = member.toMap();

        ExportValue exportValue;
        exportValue.value = map.value(QStringLiteral("value"));
        exportValue.typeName = map.value(QStringLiteral("type")).toString();
        exportValue.propertyTypeName = map.value(QStringLiteral("propertyType")).toString();

        member = context.toPropertyValue(exportValue);
    }
}

bool ClassPropertyType::canAddMemberOfType(const PropertyType *propertyType) const
{
    if (propertyType == this)
        return false;   // Can't add class as member of itself

    if (propertyType->type != PropertyType::PT_Class)
        return true;    // Can always add non-class members

    // Can't add if any member of the added class can't be added to this type
    auto classType = static_cast<const ClassPropertyType*>(propertyType);
    for (auto &member : classType->members) {
        if (member.userType() != propertyValueId())
            continue;

        auto propertyType = member.value<PropertyValue>().type();
        if (!propertyType)
            continue;

        if (!canAddMemberOfType(propertyType))
            return false;
    }

    return true;
}

// PropertyTypes

PropertyTypes::~PropertyTypes()
{
    qDeleteAll(mTypes);
}

size_t PropertyTypes::count(PropertyType::Type type) const
{
    return std::count_if(mTypes.begin(), mTypes.end(), [&] (const PropertyType *propertyType) {
        return propertyType->type == type;
    });
}

/**
 * Returns a pointer to the PropertyType matching the given \a typeId, or
 * nullptr if it can't be found.
 */
const PropertyType *PropertyTypes::findTypeById(int typeId) const
{
    auto it = std::find_if(mTypes.begin(), mTypes.end(), [&] (const PropertyType *type) {
        return type->id == typeId;
    });
    return it == mTypes.end() ? nullptr : *it;
}

/**
 * Returns a pointer to the PropertyType matching the given \a name, or
 * nullptr if it can't be found.
 */
const PropertyType *PropertyTypes::findTypeByName(const QString &name) const
{
    auto it = std::find_if(mTypes.begin(), mTypes.end(), [&] (const PropertyType *type) {
        return type->name == name;
    });
    return it == mTypes.end() ? nullptr : *it;
}

void PropertyTypes::loadFrom(const QVariantList &list, const QString &path)
{
    clear();

    const ExportContext context(*this, path);

    for (const QVariant &typeValue : list)
        if (auto propertyType = PropertyType::createFromVariant(typeValue.toMap()))
            add(std::move(propertyType));

    for (auto propertyType : mTypes)
        propertyType->resolveDependencies(context);
}

} // namespace Tiled
