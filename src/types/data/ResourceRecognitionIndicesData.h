#ifndef LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H
#define LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H

#include <quentier/types/ResourceRecognitionIndexItem.h>
#include "ResourceRecognitionIndexItemData.h"
#include <QString>
#include <QByteArray>
#include <QSharedData>

QT_FORWARD_DECLARE_CLASS(QXmlStreamAttributes)

namespace quentier {

class ResourceRecognitionIndicesData: public QSharedData
{
public:
    ResourceRecognitionIndicesData();

    bool isValid() const;
    bool setData(const QByteArray & rawRecognitionIndicesData);

    bool        m_isNull;

    QString     m_objectId;
    QString     m_objectType;
    QString     m_recoType;
    QString     m_engineVersion;
    QString     m_docType;
    QString     m_lang;
    int         m_objectHeight;
    int         m_objectWidth;

    QVector<ResourceRecognitionIndexItem>   m_items;

private:
    void clear();
    void restoreFrom(const ResourceRecognitionIndicesData & data);

    void parseRecoIndexAttributes(const QXmlStreamAttributes & attributes);
    void parseCommonItemAttributes(const QXmlStreamAttributes & attributes, ResourceRecognitionIndexItem & item) const;
    void parseTextItemAttributesAndData(const QXmlStreamAttributes & attributes, const QString & data, ResourceRecognitionIndexItem & item) const;
    void parseObjectItemAttributes(const QXmlStreamAttributes & attributes, ResourceRecognitionIndexItem & item) const;
    void parseShapeItemAttributes(const QXmlStreamAttributes & attributes, ResourceRecognitionIndexItem & item) const;
    void parseBarcodeItemAttributesAndData(const QXmlStreamAttributes & attributes, const QString & data, ResourceRecognitionIndexItem & item) const;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_RESOURCE_RECOGNITION_INDICES_DATA_H

