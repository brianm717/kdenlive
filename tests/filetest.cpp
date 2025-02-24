/*
    SPDX-FileCopyrightText: 2018-2022 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2022 Eric Jiang
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "test_utils.hpp"
#define private public
#define protected public

#include "bin/binplaylist.hpp"
#include "doc/kdenlivedoc.h"
#include "timeline2/model/builders/meltBuilder.hpp"
#include "xml/xml.hpp"

#include <QTemporaryFile>
#include <QUndoGroup>

using namespace fakeit;

TEST_CASE("Save File", "[SF]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    SECTION("Simple insert and save")
    {
        // Create document
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
        TimelineModel::next_id = 0;
        QDir dir = QDir::temp();

        QString binId = createProducerWithSound(*timeline->getProfile(), binModel);
        QString binId2 = createProducer(*timeline->getProfile(), "red", binModel, 20, false);

        int tid1 = timeline->getTrackIndexFromPosition(2);

        // Setup timeline audio drop info
        QMap<int, QString> audioInfo;
        audioInfo.insert(1, QStringLiteral("stream1"));
        timeline->m_binAudioTargets = audioInfo;
        timeline->m_videoTarget = tid1;
        // Insert 2 clips (length=20, pos = 80 / 100)
        int cid1 = -1;
        REQUIRE(timeline->requestClipInsertion(binId2, tid1, 80, cid1, true, true, false));
        int l = timeline->getClipPlaytime(cid1);
        int cid2 = -1;
        REQUIRE(timeline->requestClipInsertion(binId2, tid1, 80 + l, cid2, true, true, false));
        // Resize first clip (length=100)
        REQUIRE(timeline->requestItemResize(cid1, 100, false) == 100);

        auto state = [&]() {
            REQUIRE(timeline->checkConsistency());
            REQUIRE(timeline->getTrackClipsCount(tid1) == 2);
            REQUIRE(timeline->getClipTrackId(cid1) == tid1);
            REQUIRE(timeline->getClipTrackId(cid2) == tid1);
            REQUIRE(timeline->getClipPosition(cid1) == 0);
            REQUIRE(timeline->getClipPosition(cid2) == 100);
            REQUIRE(timeline->getClipPlaytime(cid1) == 100);
            REQUIRE(timeline->getClipPlaytime(cid2) == 20);
        };
        state();
        REQUIRE(timeline->checkConsistency());
        mocked.testSaveFileAs(dir.absoluteFilePath(QStringLiteral("test.kdenlive")));
        // Undo resize
        undoStack->undo();
        // Undo first insert
        undoStack->undo();
        // Undo second insert
        undoStack->undo();
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }
    SECTION("Reopen and check in/out points")
    {
        // Create new document
        // We mock the project class so that the undoStack function returns our undoStack, and our mocked document
        TimelineModel::next_id = 0;
        QString saveFile = QDir::temp().absoluteFilePath(QStringLiteral("test.kdenlive"));
        QUrl openURL = QUrl::fromLocalFile(saveFile);

        Mock<ProjectManager> pmMock;
        When(Method(pmMock, undoStack)).AlwaysReturn(undoStack);
        When(Method(pmMock, cacheDir)).AlwaysReturn(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
        ProjectManager &mocked = pmMock.get();
        pCore->m_projectManager = &mocked;

        QUndoGroup *undoGroup = new QUndoGroup();
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(openURL, QDir::temp().path(), undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);

        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        When(Method(pmMock, current)).AlwaysReturn(openedDoc.get());
        mocked.m_project = openedDoc.get();
        const QUuid uuid = openedDoc->uuid();
        QDateTime documentDate = QFileInfo(openURL.toLocalFile()).lastModified();
        mocked.updateTimeline(0, false, QString(), QString(), documentDate, 0);
        std::shared_ptr<Mlt::Tractor> tc = binModel->getExtraTimeline(uuid.toString());
        std::shared_ptr<TimelineItemModel> timeline = TimelineItemModel::construct(uuid, pCore->getProjectProfile(), undoStack);
        openedDoc->addTimeline(uuid, timeline);
        constructTimelineFromTractor(timeline, nullptr, *tc.get(), nullptr, openedDoc->modifiedDecimalPoint(), QString(), QString());
        mocked.testSetActiveDocument(openedDoc.get(), timeline);

        QString hash = openedDoc->getDocumentProperty(QStringLiteral("timelineHash"));

        REQUIRE(timeline->getTracksCount() == 4);
        REQUIRE(timeline->checkConsistency());
        int tid1 = timeline->getTrackIndexFromPosition(2);
        int cid1 = timeline->getClipByStartPosition(tid1, 0);
        int cid2 = timeline->getClipByStartPosition(tid1, 100);
        REQUIRE(cid1 > -1);
        REQUIRE(cid2 > -1);
        auto state = [&]() {
            REQUIRE(timeline->checkConsistency());
            REQUIRE(timeline->getTrackClipsCount(tid1) == 2);
            REQUIRE(timeline->getClipTrackId(cid1) == tid1);
            REQUIRE(timeline->getClipTrackId(cid2) == tid1);
            REQUIRE(timeline->getClipPosition(cid1) == 0);
            REQUIRE(timeline->getClipPosition(cid2) == 100);
            REQUIRE(timeline->getClipPlaytime(cid1) == 100);
            REQUIRE(timeline->getClipPlaytime(cid2) == 20);
        };
        state();
        QByteArray updatedHex = timeline->timelineHash().toHex();
        REQUIRE(updatedHex == hash);
        // QFile::remove(dir.absoluteFilePath(QStringLiteral("test.kdenlive")));
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }
    SECTION("Open a file with AV clips")
    {
        // Create new document
        QString path = sourcesPath + "/dataset/av.kdenlive";
        QUrl openURL = QUrl::fromLocalFile(path);

        Mock<ProjectManager> pmMock;
        When(Method(pmMock, undoStack)).AlwaysReturn(undoStack);
        When(Method(pmMock, cacheDir)).AlwaysReturn(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
        ProjectManager &mocked = pmMock.get();
        pCore->m_projectManager = &mocked;

        QUndoGroup *undoGroup = new QUndoGroup();
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(openURL, QDir::temp().path(), undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);

        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        When(Method(pmMock, current)).AlwaysReturn(openedDoc.get());
        mocked.m_project = openedDoc.get();
        const QUuid uuid = openedDoc->uuid();
        QDateTime documentDate = QFileInfo(openURL.toLocalFile()).lastModified();
        mocked.updateTimeline(0, false, QString(), QString(), documentDate, 0);
        auto timeline = openedDoc->getTimeline(uuid);
        mocked.testSetActiveDocument(openedDoc.get(), timeline);

        REQUIRE(timeline->checkConsistency());
        int tid1 = timeline->getTrackIndexFromPosition(0);
        int tid2 = timeline->getTrackIndexFromPosition(1);
        int tid3 = timeline->getTrackIndexFromPosition(2);
        int tid4 = timeline->getTrackIndexFromPosition(3);
        // Check we have audio and video tracks
        REQUIRE(timeline->isAudioTrack(tid1));
        REQUIRE(timeline->isAudioTrack(tid2));
        REQUIRE(timeline->isAudioTrack(tid3) == false);
        REQUIRE(timeline->isAudioTrack(tid4) == false);
        int cid1 = timeline->getClipByStartPosition(tid1, 0);
        int cid2 = timeline->getClipByStartPosition(tid2, 0);
        int cid3 = timeline->getClipByStartPosition(tid3, 0);
        int cid4 = timeline->getClipByStartPosition(tid4, 0);
        // Check we have our clips
        REQUIRE(cid1 == -1);
        REQUIRE(cid2 > -1);
        REQUIRE(cid3 > -1);
        REQUIRE(cid4 == -1);
        REQUIRE(timeline->getClipPlaytime(cid2) == 500);
        REQUIRE(timeline->getClipPlaytime(cid3) == 500);
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }
}

TEST_CASE("Non-BMP Unicode", "[NONBMP]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();

    // A Kdenlive bug (bugzilla bug 435768) caused characters outside the Basic
    // Multilingual Plane to be lost when the file is loaded. This string
    // contains the onigiri emoji which should survive a save+open round trip.
    // If Kdenlive drops the emoji, then the result is just "testtest" which can
    // be checked for.

    const QString emojiTestString = QString::fromUtf8("test\xF0\x9F\x8D\x99test");
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    QTemporaryFile saveFile(QDir::temp().filePath("kdenlive_test_XXXXXX.kdenlive"));
    qDebug() << "Choosing temp file with template" << (QDir::temp().filePath("kdenlive_test_XXXXXX.kdenlive"));
    REQUIRE(saveFile.open());
    saveFile.close();
    qDebug() << "New temporary file:" << saveFile.fileName();

    SECTION("Save title with special chars")
    {
        // Create document
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
        QDir dir = QDir::temp();

        // create a simple title with the non-BMP test string
        const QString titleXml =
            QString("<kdenlivetitle duration=\"150\" LC_NUMERIC=\"C\" width=\"1920\" height=\"1080\" out=\"149\">\n <item type=\"QGraphicsTextItem\" "
                    "z-index=\"0\">\n  <position x=\"777\" y=\"482\">\n   <transform>1,0,0,0,1,0,0,0,1</transform>\n  </position>\n  <content "
                    "shadow=\"0;#64000000;3;3;3\" font-underline=\"0\" box-height=\"138\" font-outline-color=\"0,0,0,255\" font=\"DejaVu Sans\" "
                    "letter-spacing=\"0\" font-pixel-size=\"120\" font-italic=\"0\" typewriter=\"0;2;1;0;0\" alignment=\"0\" font-weight=\"63\" "
                    "font-outline=\"3\" box-width=\"573.25\" font-color=\"252,233,79,255\">") +
            emojiTestString +
            QString("</content>\n </item>\n <startviewport rect=\"0,0,1920,1080\"/>\n <endviewport rect=\"0,0,1920,1080\"/>\n <background "
                    "color=\"0,0,0,0\"/>\n</kdenlivetitle>\n");

        QString binId2 = createTextProducer(*timeline->getProfile(), binModel, titleXml, emojiTestString, 150);

        TrackModel::construct(timeline, -1, -1, QString(), true);
        TrackModel::construct(timeline, -1, -1, QString(), true);
        int tid1 = timeline->getTrackIndexFromPosition(2);

        // Setup timeline audio drop info
        QMap<int, QString> audioInfo;
        audioInfo.insert(1, QStringLiteral("stream1"));
        timeline->m_binAudioTargets = audioInfo;
        timeline->m_videoTarget = tid1;

        mocked.testSaveFileAs(saveFile.fileName());

        // open the file and check that it contains emojiTestString
        QFile file(saveFile.fileName());
        REQUIRE(file.open(QIODevice::ReadOnly));
        QByteArray contents = file.readAll();
        if (contents.contains(emojiTestString.toUtf8())) {
            qDebug() << "File contains test string";
        } else {
            qDebug() << "File does not contain test string:" << contents;
        }
        REQUIRE(contents.contains(emojiTestString.toUtf8()));

        // try opening the file as a Kdenlivedoc and check that the title hasn't
        // lost the emoji

        QUrl openURL = QUrl::fromLocalFile(saveFile.fileName());
        QUndoGroup *undoGroup = new QUndoGroup();
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(openURL, QDir::temp().path(),
            undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);

        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        QDomDocument *newDoc = &openedDoc->m_document;
        auto producers = newDoc->elementsByTagName(QStringLiteral("producer"));
        QDomElement textTitle;
        for (int i = 0; i < producers.size(); i++) {
            auto kdenliveid = getProperty(producers.at(i).toElement(), QStringLiteral("kdenlive:id"));
            if (kdenliveid != nullptr && kdenliveid->text() == binId2) {
                textTitle = producers.at(i).toElement();
                break;
            }
        }
        auto clipname = getProperty(textTitle, QStringLiteral("kdenlive:clipname"));
        REQUIRE(clipname != nullptr);
        CHECK(clipname->text() == emojiTestString);
        auto xmldata = getProperty(textTitle, QStringLiteral("xmldata"));
        REQUIRE(xmldata != nullptr);
        CHECK(clipname->text().contains(emojiTestString));
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }

    SECTION("Save project and check profile")
    {

        // Create document
        pCore->setCurrentProfile("atsc_1080p_25");
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

        QDir dir = QDir::temp();
        int tid1 = timeline->getTrackIndexFromPosition(2);

        // Setup timeline audio drop info
        QMap<int, QString> audioInfo;
        audioInfo.insert(1, QStringLiteral("stream1"));
        timeline->m_binAudioTargets = audioInfo;
        timeline->m_videoTarget = tid1;

        mocked.testSaveFileAs(saveFile.fileName());

        // open the file and check that it contains the correct profile info
        QFile file(saveFile.fileName());
        REQUIRE(file.open(QIODevice::ReadOnly));
        QByteArray contents = file.readAll();
        QString contentCheck("<property name=\"kdenlive:docproperties.profile\">atsc_1080p_25</property>");
        if (contents.contains(contentCheck.toUtf8())) {
            qDebug() << "File contains test string";
        } else {
            qDebug() << "File does not contain test string:" << contents;
        }
        REQUIRE(contents.contains(contentCheck.toUtf8()));
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }
}

TEST_CASE("Opening Mix", "[OPENMIX]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();

    // Check that mixes (and reverse mixes) load correctly

    const QString emojiTestString = QString::fromUtf8("test\xF0\x9F\x8D\x99test");

    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    SECTION("Load file with a mix")
    {
        // We mock the project class so that the undoStack function returns our undoStack, and our mocked document
        QUrl openURL = QUrl::fromLocalFile(sourcesPath + "/dataset/test-mix.kdenlive");

        Mock<ProjectManager> pmMock;
        When(Method(pmMock, undoStack)).AlwaysReturn(undoStack);
        When(Method(pmMock, cacheDir)).AlwaysReturn(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)));
        ProjectManager &mocked = pmMock.get();
        pCore->m_projectManager = &mocked;

        QUndoGroup *undoGroup = new QUndoGroup();
        undoGroup->addStack(undoStack.get());
        DocOpenResult openResults = KdenliveDoc::Open(openURL, QDir::temp().path(), undoGroup, false, nullptr);
        REQUIRE(openResults.isSuccessful() == true);

        std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
        When(Method(pmMock, current)).AlwaysReturn(openedDoc.get());
        mocked.m_project = openedDoc.get();
        const QUuid uuid = openedDoc->uuid();
        QDateTime documentDate = QFileInfo(openURL.toLocalFile()).lastModified();
        mocked.updateTimeline(0, false, QString(), QString(), documentDate, 0);
        auto timeline = openedDoc->getTimeline(uuid);
        mocked.testSetActiveDocument(openedDoc.get(), timeline);

        REQUIRE(timeline->getTracksCount() == 4);
        int mixtrackId = timeline->getTrackIndexFromPosition(2);
        REQUIRE(timeline->getTrackById_const(mixtrackId)->mixCount() == 2);
        int mixtrackId2 = timeline->getTrackIndexFromPosition(3);
        REQUIRE(timeline->getTrackById_const(mixtrackId2)->mixCount() == 1);

        QDomDocument *newDoc = &openedDoc->m_document;
        auto producers = newDoc->elementsByTagName(QStringLiteral("producer"));
        binModel->clean();
        pCore->m_projectManager = nullptr;
    }
    undoStack->clear();
}
