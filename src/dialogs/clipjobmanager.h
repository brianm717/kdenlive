/*
    SPDX-FileCopyrightText: 2023 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "definitions.h"
#include "ui_clipjobmanager_ui.h"

#include <KConfig>
#include <QDialog>

/**
 * @class ClipJobManager
 * @brief A dialog for editing Bin Clip jobs.
 * @author Jean-Baptiste Mardelle
 */

class ClipJobManager : public QDialog, public Ui::ClipJobManager_UI
{
    Q_OBJECT

public:
    enum class JobCompletionAction { ReplaceOriginal = 0, RootFolder = 1, SubFolder = 2, NoAction = 3 };

    explicit ClipJobManager(QWidget *parent = nullptr);
    virtual ~ClipJobManager() override;
    static QMap<QString, QString> getClipJobNames();
    /** @brief Returns the {wanted action, folder name} for clip job type @jobId */
    static std::pair<JobCompletionAction, QString> getJobAction(const QString &jobId);
    /** @brief Resutrns the params list as {binary, parameters, output} for clip job type @jobId */
    static QStringList getJobParameters(const QString &jobId);

private:
    /** @brief Populate the list view with jobs */
    void loadJobs();
    /** @brief Map of clip job ids */
    QMap<QString, QString> m_ids;
    /** @brief Map of clip job params */
    QMap<QString, QString> m_params;
    /** @brief Map of clip job folder names */
    QMap<QString, QString> m_folderNames;
    /** @brief Map of clip job folder usage */
    QMap<QString, QString> m_folderUse;
    /** @brief Map of clip job binaries */
    QMap<QString, QString> m_binaries;
    /** @brief Map of clip job output pattern */
    QMap<QString, QString> m_output;
    /** @brief Sync a group to config file */
    void writeGroup(KConfig &conf, const QString &groupName, QMap<QString, QString> values);
    /** @brief Remember current changes */
    void saveCurrentPreset();
    /** @brief Save all jobs to the config file */
    void writePresetsToConfig();

private Q_SLOTS:
    /** @brief Display a job's parameters */
    void displayJob(int row);
    void setDirty();
    void validate();
    /** @brief Create a new Clip Job entry */
    void addJob();
    /** @brief Delete current Clip Job entry */
    void deleteJob();
    /** @brief Check if job name was edited and correctly store it */
    void updateName(QListWidgetItem *item);

private:
    QString m_dirty;
};
