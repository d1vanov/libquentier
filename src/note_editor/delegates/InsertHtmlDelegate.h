#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"
#include <quentier/utility/Macros.h>
#include <quentier/types/Note.h>
#include <quentier/types/Resource.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSet>
#include <QHash>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Account)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)
QT_FORWARD_DECLARE_CLASS(ENMLConverter)
QT_FORWARD_DECLARE_CLASS(ResourceFileStorageManager)
QT_FORWARD_DECLARE_CLASS(ResourceInfo)

class InsertHtmlDelegate: public QObject
{
    Q_OBJECT
public:
    explicit InsertHtmlDelegate(const QString & inputHtml, NoteEditorPrivate & noteEditor,
                                ENMLConverter & enmlConverter,
                                ResourceFileStorageManager * pResourceFileStorageManager,
                                QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
                                ResourceInfo & resourceInfo,
                                QObject * parent = Q_NULLPTR);

    void start();

Q_SIGNALS:
    void finished(QList<Resource> addedResources, QStringList resourceFileStoragePaths);
    void notifyError(ErrorString error);

    // private signals:
    void saveResourceToStorage(QString noteLocalUid, QString resourceLocalUid, QByteArray data, QByteArray dataHash,
                               QString preferredFileSuffix, QUuid requestId, bool isImage);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);
    void onImageDataDownloadFinished(QNetworkReply * pReply);
    void onResourceSavedToStorage(QUuid requestId, QByteArray dataHash,
                                  QString fileStoragePath, int errorCode,
                                  ErrorString errorDescription);

    void onHtmlInserted(const QVariant & responseData);

private:
    void doStart();
    void checkImageResourcesReady();
    bool addResource(const QByteArray & resourceData, const QUrl & url);

    bool adjustImgTagsInHtml();
    void insertHtmlIntoEditor();

    void removeAddedResourcesFromNote();

private:
    typedef JsResultCallbackFunctor<InsertHtmlDelegate> JsCallback;

private:
    NoteEditorPrivate &             m_noteEditor;
    ENMLConverter &                 m_enmlConverter;

    ResourceFileStorageManager *    m_pResourceFileStorageManager;
    QHash<QString, QString> &       m_resourceFileStoragePathsByResourceLocalUid;
    ResourceInfo &                  m_resourceInfo;

    QString                         m_inputHtml;
    QString                         m_cleanedUpHtml;

    QSet<QUrl>                      m_imageUrls;
    QSet<QUrl>                      m_pendingImageUrls;
    QSet<QUrl>                      m_failingImageUrls;

    QHash<QUuid, Resource>          m_resourceBySaveToStorageRequestId;
    QHash<QString, QUrl>            m_sourceUrlByResourceLocalUid;

    struct ImgData
    {
        Resource    m_resource;
        QString     m_resourceFileStoragePath;
    };

    QHash<QUrl, ImgData>            m_imgDataBySourceUrl;

    QNetworkAccessManager           m_networkAccessManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_INSERT_HTML_DELEGATE_H
