/*
    SPDX-FileCopyrightText: 2022 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2022 Eric Jiang
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "catch.hpp"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "test_utils.hpp"

#include <QString>
#include <cmath>
#include <iostream>
#include <tuple>
#include <unordered_set>

#include "definitions.h"
#define private public
#define protected public
#include "core.h"
#include "utils/thumbnailcache.hpp"

TEST_CASE("Cache insert-remove", "[Cache]")
{
    // Create timeline
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    // Here we do some trickery to enable testing.
    // We mock the project class so that the undoStack function returns our undoStack
    KdenliveDoc document(undoStack);
    Mock<KdenliveDoc> docMock(document);
    KdenliveDoc &mockedDoc = docMock.get();

    // We mock the project class so that the undoStack function returns our undoStack, and our mocked document
    Mock<ProjectManager> pmMock;
    When(Method(pmMock, undoStack)).AlwaysReturn(undoStack);
    When(Method(pmMock, cacheDir)).AlwaysReturn(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
    When(Method(pmMock, current)).AlwaysReturn(&mockedDoc);
    ProjectManager &mocked = pmMock.get();
    pCore->m_projectManager = &mocked;
    mocked.m_project = &mockedDoc;
    QDateTime documentDate = QDateTime::currentDateTime();
    mocked.updateTimeline(0, false, QString(), QString(), documentDate, 0);
    auto timeline = mockedDoc.getTimeline(mockedDoc.uuid());
    mocked.m_activeTimelineModel = timeline;

    mocked.testSetActiveDocument(&mockedDoc, timeline);

    // Create bin clip
    QString binId = createProducer(*timeline->getProfile(), "red", binModel, 20, false);

    SECTION("Insert and remove thumbnail")
    {
        QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::red);
        ThumbnailCache::get()->storeThumbnail(binId, 0, img, false);
        REQUIRE(ThumbnailCache::get()->checkIntegrity());
        ThumbnailCache::get()->storeThumbnail(binId, 0, img, false);
        REQUIRE(ThumbnailCache::get()->checkIntegrity());
    }
    binModel->clean();
    pCore->m_projectManager = nullptr;
}

TEST_CASE("getAudioKey() should dereference `ok` param", "ThumbnailCache") {
    // Create timeline
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    // Here we do some trickery to enable testing.
    // We mock the project class so that the undoStack function returns our undoStack
    KdenliveDoc document(undoStack);
    Mock<KdenliveDoc> docMock(document);
    KdenliveDoc &mockedDoc = docMock.get();

    // We mock the project class so that the undoStack function returns our undoStack, and our mocked document
    Mock<ProjectManager> pmMock;
    When(Method(pmMock, undoStack)).AlwaysReturn(undoStack);
    When(Method(pmMock, cacheDir)).AlwaysReturn(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
    When(Method(pmMock, current)).AlwaysReturn(&mockedDoc);
    ProjectManager &mocked = pmMock.get();
    pCore->m_projectManager = &mocked;
    mocked.m_project = &mockedDoc;
    QDateTime documentDate = QDateTime::currentDateTime();
    mocked.updateTimeline(0, false, QString(), QString(), documentDate, 0);
    auto timeline = mockedDoc.getTimeline(mockedDoc.uuid());
    mocked.m_activeTimelineModel = timeline;

    mocked.testSetActiveDocument(&mockedDoc, timeline);

    // Create bin clip
    QString binId = createProducer(*timeline->getProfile(), "red", binModel, 20, false);

    SECTION("Request invalid id")
    {
        // Catches a bug where, after setting *ok, the code checks
        //     if (ok) {
        // instead of
        //     if (*ok) {
        bool ok = true;
        ThumbnailCache::getAudioKey(QStringLiteral("nonexistent-key"), &ok);
        REQUIRE(ok == false);
    }
    SECTION("Request valid id")
    {
        bool ok = false;
        ThumbnailCache::getAudioKey(binId, &ok);
        REQUIRE(ok == true);
    }
    binModel->clean();
    pCore->m_projectManager = nullptr;
}
