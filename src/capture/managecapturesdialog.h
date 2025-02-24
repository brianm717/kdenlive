/*
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QPushButton>

#include <QUrl>

#include "ui_managecaptures_ui.h"

class ManageCapturesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ManageCapturesDialog(const QList<QUrl> &files, QWidget *parent = nullptr);
    ~ManageCapturesDialog() override;
    QList<QUrl> importFiles() const;

private Q_SLOTS:
    void slotRefreshButtons();
    void slotDeleteCurrent();
    void slotToggle();
    void slotCheckItemIcon();

private:
    Ui::ManageCaptures_UI m_view{};
    QPushButton *m_importButton;
};
