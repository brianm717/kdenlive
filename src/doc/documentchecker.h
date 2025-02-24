/*
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "definitions.h"
#include "ui_missingclips_ui.h"

#include <QDir>
#include <QDomElement>
#include <QUrl>

class DocumentChecker : public QObject
{
    Q_OBJECT

public:
    explicit DocumentChecker(QUrl url, const QDomDocument &doc);
    ~DocumentChecker() override;
    /**
     * @brief checks for problems with the clips in the project
     * Checks for missing proxies, wrong duration clips, missing fonts, missing images, missing source clips
     * Calls DocumentChecker::checkMissingImagesAndFonts () /n
     * Called by KdenliveDoc::checkDocumentClips ()        /n
     * @return
     */
    bool hasErrorInClips();
    QString fixLuma(const QString &file);
    QString searchLuma(const QDir &dir, const QString &file);

private Q_SLOTS:
    void acceptDialog();
    void slotCheckClips();
    void slotSearchClips(const QString &newpath);
    void slotEditItem(QTreeWidgetItem *item, int);
    void slotPlaceholders();
    void slotDeleteSelected();
    QString getProperty(const QDomElement &effect, const QString &name);
    void updateProperty(const QDomElement &effect, const QString &name, const QString &value);
    void setProperty(QDomElement &effect, const QString &name, const QString &value);
    /** @brief Check if images and fonts in this clip exists, returns a list of images that do exist so we don't check twice. */
    void checkMissingImagesAndFonts(const QStringList &images, const QStringList &fonts, const QString &id, const QString &baseClip);
    void slotCheckButtons();

private:
    QUrl m_url;
    QDomDocument m_doc;
    QString m_documentid;
    Ui::MissingClips_UI m_ui;
    QDialog *m_dialog;
    QPair<QString, QString> m_rootReplacement;
    QString searchPathRecursively(const QDir &dir, const QString &fileName, ClipType::ProducerType type = ClipType::Unknown);
    QString searchFileRecursively(const QDir &dir, const QString &matchSize, const QString &matchHash, const QString &fileName);
    QString searchDirRecursively(const QDir &dir, const QString &matchHash, const QString &fullName);
    void checkStatus();
    QMap<QString, QString> m_missingTitleImages;
    QMap<QString, QString> m_missingTitleFonts;
    QList<QDomElement> m_missingClips;
    QStringList m_missingFilters;
    QStringList m_missingFonts;
    QStringList m_safeImages;
    QStringList m_safeFonts;
    QStringList m_missingProxyIds;
    QStringList m_changedClips;
    // List clips whose proxy is missing
    QList<QDomElement> m_missingProxies;
    // List clips who have a working proxy but no source clip
    QList<QDomElement> m_missingSources;
    bool m_abortSearch;
    bool m_checkRunning;

    void fixClipItem(QTreeWidgetItem *child, const QDomNodeList &producers, const QDomNodeList &trans);
    void fixSourceClipItem(QTreeWidgetItem *child, const QDomNodeList &producers);
    void fixProxyClip(const QString &id, const QString &oldUrl, const QString &newUrl);
    void doFixProxyClip(QDomElement &e, const QString &oldUrl, const QString &newUrl);
    /** @brief Returns list of transitions containing luma files */
    QMap<QString, QString> getLumaPairs() const;
    /** @brief Remove _missingsourcec flag in fixed clips */
    void fixMissingSource(const QString &id, const QDomNodeList &producers);
    /** @brief Check for various missing elements */
    QString getMissingProducers(const QDomElement &e, const QDomNodeList &entries, const QStringList &verifiedPaths, QStringList missingPaths, const QStringList &serviceToCheck, const QString &root, const QString &storageFolder);
    /** @brief If project path changed, try to relocate its resources */
    const QString relocateResource(QString sourceResource);

Q_SIGNALS:
    void showScanning(const QString);
};
