#ifndef LIB_QUENTIER_TYPES_TAG_H
#define LIB_QUENTIER_TYPES_TAG_H

#include "IFavoritableDataElement.h"
#include <QEverCloud.h>
#include <QSharedDataPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(TagData)

class QUENTIER_EXPORT Tag: public IFavoritableDataElement
{
public:
    QN_DECLARE_LOCAL_UID
    QN_DECLARE_DIRTY
    QN_DECLARE_LOCAL
    QN_DECLARE_FAVORITED

public:
    Tag();
    Tag(const Tag & other);
    Tag(Tag && other);
    Tag & operator=(const Tag & other);
    Tag & operator=(Tag && other);

    Tag(const qevercloud::Tag & other);
    Tag(qevercloud::Tag && other);

    virtual ~Tag();

    bool operator==(const Tag & other) const;
    bool operator!=(const Tag & other) const;

    operator const qevercloud::Tag &() const;
    operator qevercloud::Tag &();

    virtual void clear() Q_DECL_OVERRIDE;

    static bool validateName(const QString & name, QString * pErrorDescription = Q_NULLPTR);

    virtual bool hasGuid() const Q_DECL_OVERRIDE;
    virtual const QString & guid() const Q_DECL_OVERRIDE;
    virtual void setGuid(const QString & guid) Q_DECL_OVERRIDE;

    virtual bool hasUpdateSequenceNumber() const Q_DECL_OVERRIDE;
    virtual qint32 updateSequenceNumber() const Q_DECL_OVERRIDE;
    virtual void setUpdateSequenceNumber(const qint32 usn) Q_DECL_OVERRIDE;

    virtual bool checkParameters(QString & errorDescription) const Q_DECL_OVERRIDE;

    bool hasName() const;
    const QString & name() const;
    void setName(const QString & name);

    bool hasParentGuid() const;
    const QString & parentGuid() const;
    void setParentGuid(const QString & parentGuid);

    bool hasParentLocalUid() const;
    const QString & parentLocalUid() const;
    void setParentLocalUid(const QString & parentLocalUid);

    bool hasLinkedNotebookGuid() const;
    const QString & linkedNotebookGuid() const;
    void setLinkedNotebookGuid(const QString & linkedNotebookGuid);

    virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;

private:
    QSharedDataPointer<TagData> d;
};

} // namespace quentier

Q_DECLARE_METATYPE(quentier::Tag)

#endif // LIB_QUENTIER_TYPES_TAG_H
