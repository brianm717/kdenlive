/*
    SPDX-FileCopyrightText: 2008 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2011 Marco Gittler <marco@gitma.de>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "clipstabilize.h"
#include "assets/model/assetparametermodel.hpp"
#include "assets/view/assetparameterview.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "effects/effectsrepository.hpp"
#include "widgets/doublewidget.h"
#include "widgets/positionwidget.h"

#include "kdenlivesettings.h"
#include <KIO/RenameDialog>
#include <KMessageBox>
#include <QFontDatabase>
#include <QPushButton>
#include <memory>
#include <mlt++/Mlt.h>

ClipStabilize::ClipStabilize(const std::vector<QString> &binIds, QString filterName, QWidget *parent)
    : QDialog(parent)
    , m_filtername(std::move(filterName))
    , m_binIds(binIds)
    , m_vbox(nullptr)
    , m_assetModel(nullptr)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    setupUi(this);
    setWindowTitle(i18nc("@title:window", "Stabilize Clip"));
    // QString stylesheet = EffectStackView2::getStyleSheet();
    // setStyleSheet(stylesheet);

    Q_ASSERT(binIds.size() > 0);
    auto firstBinClip = pCore->projectItemModel()->getClipByBinID(m_binIds.front().section(QLatin1Char('/'), 0, 0));
    auto firstUrl = firstBinClip->url();
    m_vbox = new QVBoxLayout(optionsbox);
    if (m_filtername == QLatin1String("vidstab")) {
        m_view = std::make_unique<AssetParameterView>(this);
        qDebug() << "// Fetching effect: " << m_filtername;
        std::unique_ptr<Mlt::Filter> asset = EffectsRepository::get()->getEffect(m_filtername);
        auto prop = std::make_unique<Mlt::Properties>(asset->get_properties());
        QDomElement xml = EffectsRepository::get()->getXml(m_filtername);
        m_assetModel.reset(new AssetParameterModel(std::move(prop), xml, m_filtername, {ObjectType::NoItem, -1}));
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/effects/presets/"));
        const QString presetFile = dir.absoluteFilePath(QString("%1.json").arg(m_assetModel->getAssetId()));
        const QVector<QPair<QString, QVariant>> params = m_assetModel->loadPreset(presetFile, i18n("Last setting"));
        if (!params.isEmpty()) {
            m_assetModel->setParameters(params);
        }
        m_view->setModel(m_assetModel, QSize(1920, 1080));
        m_vbox->addWidget(m_view.get());
        // Presets
        preset_button->setMenu(m_view->presetMenu());
    }

    connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &QDialog::accept);
    adjustSize();
}

ClipStabilize::~ClipStabilize() {}

std::unordered_map<QString, QVariant> ClipStabilize::filterParams() const
{
    QVector<QPair<QString, QVariant>> result = m_assetModel->getAllParameters();
    std::unordered_map<QString, QVariant> params;

    for (const auto &it : qAsConst(result)) {
        params[it.first] = it.second;
    }
    return params;
}

QString ClipStabilize::filterName() const
{
    return m_filtername;
}

QString ClipStabilize::desc() const
{
    return i18nc("Description", "Stabilize clip");
}
