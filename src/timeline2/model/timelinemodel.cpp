/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "timelinemodel.hpp"
#include "clipmodel.hpp"
#include "core.h"
#include "compositionmodel.hpp"
#include "groupsmodel.hpp"
#include "snapmodel.hpp"
#include "trackmodel.hpp"

#include "doc/docundostack.hpp"

#include <QDebug>
#include <QModelIndex>
#include <klocalizedstring.h>
#include <mlt++/MltField.h>
#include <mlt++/MltProfile.h>
#include <mlt++/MltTractor.h>
#include <mlt++/MltTransition.h>
#include <queue>
#ifdef LOGGING
#include <sstream>
#include <utility>
#endif

#include "macros.hpp"

int TimelineModel::next_id = 0;

TimelineModel::TimelineModel(Mlt::Profile *profile, std::weak_ptr<DocUndoStack> undo_stack)
    : QAbstractItemModel()
    , m_tractor(new Mlt::Tractor(*profile))
    , m_snaps(new SnapModel())
    , m_undoStack(undo_stack)
    , m_profile(profile)
    , m_blackClip(new Mlt::Producer(*profile, "color:black"))
    , m_lock(QReadWriteLock::Recursive)
    , m_timelineEffectsEnabled(true)
    , m_id(getNextId())
{
    // Create black background track
    m_blackClip->set("id", "black_track");
    m_blackClip->set("mlt_type", "producer");
    m_blackClip->set("aspect_ratio", 1);
    m_blackClip->set("set.test_audio", 0);
    m_tractor->insert_track(*m_blackClip, 0);

#ifdef LOGGING
    m_logFile = std::ofstream("log.txt");
    m_logFile << "TEST_CASE(\"Regression\") {" << std::endl;
    m_logFile << "Mlt::Profile profile;" << std::endl;
    m_logFile << "std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);" << std::endl;
    m_logFile << "std::shared_ptr<TimelineModel> timeline = TimelineItemModel::construct(new Mlt::Profile(), undoStack);" << std::endl;
    m_logFile << "TimelineModel::next_id = 0;" << std::endl;
    m_logFile << "int dummy_id;" << std::endl;
#endif
}

TimelineModel::~TimelineModel()
{
    // Remove black background
    // m_tractor->remove_track(0);
    std::vector<int> all_ids;
    for (auto tracks : m_iteratorTable) {
        all_ids.push_back(tracks.first);
    }
    for (auto tracks : all_ids) {
        deregisterTrack_lambda(tracks, false)();
    }
}

int TimelineModel::getTracksCount() const
{
    READ_LOCK();
    int count = m_tractor->count();
    Q_ASSERT(count >= 0);
    // don't count the black background track
    Q_ASSERT(count - 1 == static_cast<int>(m_allTracks.size()));
    return count - 1;
}

int TimelineModel::getClipsCount() const
{
    READ_LOCK();
    int size = int(m_allClips.size());
    return size;
}

int TimelineModel::getCompositionsCount() const
{
    READ_LOCK();
    int size = int(m_allCompositions.size());
    return size;
}

int TimelineModel::getClipTrackId(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(m_allClips.count(clipId) > 0);
    const auto clip = m_allClips.at(clipId);
    int trackId = clip->getCurrentTrackId();
    return trackId;
}

int TimelineModel::getCompositionTrackId(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    return trans->getCurrentTrackId();
}

int TimelineModel::getItemTrackId(int itemId) const
{
    READ_LOCK();
    Q_ASSERT(isClip(itemId) || isComposition(itemId));
    if (isComposition(itemId)) {
        return getCompositionTrackId(itemId);
    }
    return getClipTrackId(itemId);
}

int TimelineModel::getClipPosition(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(m_allClips.count(clipId) > 0);
    const auto clip = m_allClips.at(clipId);
    int pos = clip->getPosition();
    return pos;
}

int TimelineModel::getClipPlaytime(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(isClip(clipId));
    const auto clip = m_allClips.at(clipId);
    int playtime = clip->getPlaytime();
    return playtime;
}

int TimelineModel::getTrackClipsCount(int trackId) const
{
    READ_LOCK();
    Q_ASSERT(isTrack(trackId));
    int count = getTrackById_const(trackId)->getClipsCount();
    return count;
}

int TimelineModel::getTrackPosition(int trackId) const
{
    READ_LOCK();
    Q_ASSERT(isTrack(trackId));
    auto it = m_allTracks.begin();
    int pos = (int)std::distance(it, (decltype(it))m_iteratorTable.at(trackId));
    return pos;
}

int TimelineModel::getTrackMltIndex(int trackId) const
{
    READ_LOCK();
    // Because of the black track that we insert in first position, the mlt index is the position + 1
    return getTrackPosition(trackId) + 1;
}

int TimelineModel::getNextTrackId(int trackId) const
{
    READ_LOCK();
    Q_ASSERT(isTrack(trackId));
    auto it = m_iteratorTable.at(trackId);
    ++it;
    return (it != m_allTracks.end()) ? (*it)->getId() : -1;
}

int TimelineModel::getPreviousTrackId(int trackId) const
{
    READ_LOCK();
    Q_ASSERT(isTrack(trackId));
    auto it = m_iteratorTable.at(trackId);
    if (it == m_allTracks.begin()) {
        return -1;
    }
    --it;
    return (*it)->getId();
}

bool TimelineModel::requestClipMove(int clipId, int trackId, int position, bool updateView, Fun &undo, Fun &redo)
{
    Q_ASSERT(isClip(clipId));
    std::function<bool(void)> local_undo = []() { return true; };
    std::function<bool(void)> local_redo = []() { return true; };
    bool ok = true;
    int old_trackId = getClipTrackId(clipId);
    if (old_trackId != -1) {
        ok = getTrackById(old_trackId)->requestClipDeletion(clipId, updateView, local_undo, local_redo);
        if (!ok) {
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    ok = getTrackById(trackId)->requestClipInsertion(clipId, position, updateView, local_undo, local_redo);
    if (!ok) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestClipMove(int clipId, int trackId, int position, bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipMove(" << clipId << "," << trackId << " ," << position << ", " << (updateView ? "true" : "false") << ", "
              << (logUndo ? "true" : "false") << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_allClips.count(clipId) > 0);
    if (m_allClips[clipId]->getPosition() == position && getClipTrackId(clipId) == trackId) {
        return true;
    }
    if (m_groups->isInGroup(clipId)) {
        // element is in a group.
        int groupId = m_groups->getRootId(clipId);
        int current_trackId = getClipTrackId(clipId);
        int track_pos1 = getTrackPosition(trackId);
        int track_pos2 = getTrackPosition(current_trackId);
        int delta_track = track_pos1 - track_pos2;
        int delta_pos = position - m_allClips[clipId]->getPosition();
        return requestGroupMove(clipId, groupId, delta_track, delta_pos, updateView, logUndo);
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    bool res = requestClipMove(clipId, trackId, position, updateView, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move clip"));
    }
    return res;
}

int TimelineModel::suggestClipMove(int clipId, int trackId, int position)
{
#ifdef LOGGING
    m_logFile << "timeline->suggestClipMove(" << clipId << "," << trackId << " ," << position << "); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(clipId));
    Q_ASSERT(isTrack(trackId));
    int currentPos = getClipPosition(clipId);
    int currentTrack = getClipTrackId(clipId);
    if (currentPos == position || currentTrack != trackId) {
        return position;
    }

    // For snapping, we must ignore all in/outs of the clips of the group being moved
    std::vector<int> ignored_pts;
    if (m_groups->isInGroup(clipId)) {
        int groupId = m_groups->getRootId(clipId);
        auto all_clips = m_groups->getLeaves(groupId);
        for (int current_clipId : all_clips) {
            int in = getClipPosition(current_clipId);
            int out = in + getClipPlaytime(current_clipId) - 1;
            ignored_pts.push_back(in);
            ignored_pts.push_back(out);
        }
    } else {
        int in = getClipPosition(clipId);
        int out = in + getClipPlaytime(clipId) - 1;
        ignored_pts.push_back(in);
        ignored_pts.push_back(out);
    }

    int snapped = requestBestSnapPos(position, m_allClips[clipId]->getPlaytime(), ignored_pts);
    qDebug() << "Starting suggestion " << clipId << position << currentPos << "snapped to " << snapped;
    if (snapped >= 0) {
        position = snapped;
    }
    // we check if move is possible
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool possible = requestClipMove(clipId, trackId, position, false, undo, redo);
    qDebug() << "Original move success" << possible;
    if (possible) {
        undo();
        return position;
    }
    bool after = position > currentPos;
    int blank_length = getTrackById(trackId)->getBlankSizeNearClip(clipId, after);
    qDebug() << "Found blank" << blank_length;
    if (blank_length < INT_MAX) {
        if (after) {
            return currentPos + blank_length;
        }
        return currentPos - blank_length;
    }
    return position;
}

int TimelineModel::suggestCompositionMove(int compoId, int trackId, int position)
{
#ifdef LOGGING
    m_logFile << "timeline->suggestCompositionMove(" << compoId << "," << trackId << " ," << position << "); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isComposition(compoId));
    Q_ASSERT(isTrack(trackId));
    int currentPos = getCompositionPosition(compoId);
    int currentTrack = getCompositionTrackId(compoId);
    if (currentPos == position || currentTrack != trackId) {
        return position;
    }

    // For snapping, we must ignore all in/outs of the clips of the group being moved
    std::vector<int> ignored_pts;
    if (m_groups->isInGroup(compoId)) {
        int groupId = m_groups->getRootId(compoId);
        auto all_clips = m_groups->getLeaves(groupId);
        for (int current_compoId : all_clips) {
            // TODO: fix for composition
            int in = getClipPosition(current_compoId);
            int out = in + getClipPlaytime(current_compoId) - 1;
            ignored_pts.push_back(in);
            ignored_pts.push_back(out);
        }
    } else {
        int in = currentPos;
        int out = in + getCompositionPlaytime(compoId) - 1;
        qDebug() << " * ** IGNORING SNAP PTS: " << in << "-" << out;
        ignored_pts.push_back(in);
        ignored_pts.push_back(out);
    }

    int snapped = requestBestSnapPos(position, m_allCompositions[compoId]->getPlaytime(), ignored_pts);
    qDebug() << "Starting suggestion " << compoId << position << currentPos << "snapped to " << snapped;
    if (snapped >= 0) {
        position = snapped;
    }
    // we check if move is possible
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool possible = requestCompositionMove(compoId, trackId, position, false, undo, redo);
    qDebug() << "Original move success" << possible;
    if (possible) {
        bool undone = undo();
        Q_ASSERT(undone);
        return position;
    }
    bool after = position > currentPos;
    int blank_length = getTrackById(trackId)->getBlankSizeNearComposition(compoId, after);
    qDebug() << "Found blank" << blank_length;
    if (blank_length < INT_MAX) {
        if (after) {
            return currentPos + blank_length;
        }
        return currentPos - blank_length;
    }
    return position;
}

bool TimelineModel::requestClipInsertion(const QString &binClipId, int trackId, int position, int &id, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipInsertion(" << binClipId.toStdString() << "," << trackId << " ," << position << ", dummy_id );" << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestClipInsertion(binClipId, trackId, position, id, undo, redo);
    if (result && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Insert Clip"));
    }
    return result;
}

bool TimelineModel::requestClipInsertion(const QString &binClipId, int trackId, int position, int &id, Fun &undo, Fun &redo)
{
    int clipId = TimelineModel::getNextId();
    id = clipId;
    Fun local_undo = deregisterClip_lambda(clipId);
    ClipModel::construct(shared_from_this(), binClipId, clipId);
    auto clip = m_allClips[clipId];
    Fun local_redo = [clip, this]() {
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is
        // sufficient to register it.
        registerClip(clip);
        clip->refreshProducerFromBin();
        return true;
    };
    bool res = requestClipMove(clipId, trackId, position, true, local_undo, local_redo);
    if (!res) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        id = -1;
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestItemDeletion(int itemId, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestItemDeletion(" << itemId << "); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(itemId) || isComposition(itemId));
    if (m_groups->isInGroup(itemId)) {
        return requestGroupDeletion(itemId);
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool res = false;
    if (isClip(itemId)) {
        res = requestClipDeletion(itemId, undo, redo);
    } else {
        res = requestCompositionDeletion(itemId, undo, redo);
    }
    if (res && logUndo) {
        if (isClip(itemId)) {
            PUSH_UNDO(undo, redo, i18n("Delete Clip"));
        } else {
            PUSH_UNDO(undo, redo, i18n("Delete Composition"));
        }
    }
    return res;
}

bool TimelineModel::requestClipDeletion(int clipId, Fun &undo, Fun &redo)
{
    int trackId = getClipTrackId(clipId);
    if (trackId != -1) {
        bool res = getTrackById(trackId)->requestClipDeletion(clipId, true, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    auto operation = deregisterClip_lambda(clipId);
    auto clip = m_allClips[clipId];
    auto reverse = [this, clip]() {
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is
        // sufficient to register it.
        registerClip(clip);
        clip->refreshProducerFromBin();
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        return true;
    }
    undo();
    return false;
}
bool TimelineModel::requestCompositionDeletion(int compositionId, Fun &undo, Fun &redo)
{
    int trackId = getCompositionTrackId(compositionId);
    if (trackId != -1) {
        bool res = getTrackById(trackId)->requestCompositionDeletion(compositionId, true, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    auto operation = deregisterComposition_lambda(compositionId);
    auto composition = m_allCompositions[compositionId];
    auto reverse = [this, composition]() {
        // We capture a shared_ptr to the composition, which means that as long as this undo object lives, the composition object is not deleted. To insert it
        // back it is sufficient to register it.
        registerComposition(composition);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        return true;
    }
    undo();
    return false;
}

bool TimelineModel::requestGroupMove(int clipId, int groupId, int delta_track, int delta_pos, bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestGroupMove(" << clipId << "," << groupId << " ," << delta_track << ", " << delta_pos << ", "
              << (updateView ? "true" : "false") << ", " << (logUndo ? "true" : "false") << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    Q_ASSERT(m_allGroups.count(groupId) > 0);
    bool ok = true;
    auto all_clips = m_groups->getLeaves(groupId);
    std::vector<int> sorted_clips(all_clips.begin(), all_clips.end());
    // we have to sort clip in an order that allows to do the move without self conflicts
    // If we move up, we move first the clips on the upper tracks (and conversely).
    // If we move left, we move first the leftmost clips (and conversely).
    std::sort(sorted_clips.begin(), sorted_clips.end(), [delta_track, delta_pos, this](int clipId1, int clipId2) {
        int trackId1 = getClipTrackId(clipId1);
        int trackId2 = getClipTrackId(clipId2);
        int track_pos1 = getTrackPosition(trackId1);
        int track_pos2 = getTrackPosition(trackId2);
        if (trackId1 == trackId2) {
            int p1 = m_allClips[clipId1]->getPosition();
            int p2 = m_allClips[clipId2]->getPosition();
            return !(p1 <= p2) == !(delta_pos <= 0);
        }
        return !(track_pos1 <= track_pos2) == !(delta_track <= 0);
    });
    for (int clip : sorted_clips) {
        int current_track_id = getClipTrackId(clip);
        int current_track_position = getTrackPosition(current_track_id);
        int target_track_position = current_track_position + delta_track;
        if (target_track_position >= 0 && target_track_position < getTracksCount()) {
            auto it = m_allTracks.cbegin();
            std::advance(it, target_track_position);
            int target_track = (*it)->getId();
            int target_position = m_allClips[clip]->getPosition() + delta_pos;
            ok = requestClipMove(clip, target_track, target_position, updateView || (clip != clipId), undo, redo);
        } else {
            ok = false;
        }
        if (!ok) {
            bool undone = undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    if (logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move group"));
    }
    return true;
}

bool TimelineModel::requestGroupDeletion(int clipId)
{
#ifdef LOGGING
    m_logFile << "timeline->requestGroupDeletion(" << clipId << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    // we do a breadth first exploration of the group tree, ungroup (delete) every inner node, and then delete all the leaves.
    std::queue<int> group_queue;
    group_queue.push(m_groups->getRootId(clipId));
    std::unordered_set<int> all_clips;
    while (!group_queue.empty()) {
        int current_group = group_queue.front();
        group_queue.pop();
        Q_ASSERT(isGroup(current_group));
        auto children = m_groups->getDirectChildren(current_group);
        int one_child = -1; // we need the id on any of the indices of the elements of the group
        for (int c : children) {
            if (isClip(c)) {
                all_clips.insert(c);
                one_child = c;
            } else {
                Q_ASSERT(isGroup(c));
                one_child = c;
                group_queue.push(c);
            }
        }
        if (one_child != -1) {
            bool res = m_groups->ungroupItem(one_child, undo, redo);
            if (!res) {
                undo();
                return false;
            }
        }
    }
    for (int clip : all_clips) {
        bool res = requestClipDeletion(clip, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    PUSH_UNDO(undo, redo, i18n("Remove group"));
    return true;
}

bool TimelineModel::requestItemResize(int itemId, int size, bool right, bool logUndo, bool snapping)
{
#ifdef LOGGING
    m_logFile << "timeline->requestItemResize(" << itemId << "," << size << " ," << (right ? "true" : "false") << ", " << (logUndo ? "true" : "false") << ", "
              << (snapping ? "true" : "false") << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(itemId) || isComposition(itemId));
    if (snapping) {
        Fun temp_undo = []() { return true; };
        Fun temp_redo = []() { return true; };
        int in, out;
        if (isClip(itemId)) {
            in = getClipPosition(itemId);
            out = in + getClipPlaytime(itemId) - 1;
        } else {
            in = getCompositionPosition(itemId);
            out = in + getCompositionPlaytime(itemId) - 1;
        }
        // TODO Make the number of frames of snapping adjustable
        int proposed_size = m_snaps->proposeSize(in, out, size, right, 10);
        if (proposed_size < 0) {
            proposed_size = size;
        }
        bool success = false;
        if (isClip(itemId)) {
            success = m_allClips[itemId]->requestResize(proposed_size, right, temp_undo, temp_redo);
        } else {
            success = m_allCompositions[itemId]->requestResize(proposed_size, right, temp_undo, temp_redo);
        }
        if (success) {
            temp_undo(); // undo temp move
            size = proposed_size;
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestItemResize(itemId, size, right, logUndo, undo, redo);
    if (result && logUndo) {
        if (isClip(itemId)) {
            PUSH_UNDO(undo, redo, i18n("Resize clip"));
        } else {
            PUSH_UNDO(undo, redo, i18n("Resize composition"));
        }
    }
    return result;
}

bool TimelineModel::requestItemResize(int itemId, int size, bool right, bool logUndo, Fun &undo, Fun &redo)
{
    Fun update_model = [itemId, right, logUndo, this]() {
        if (getItemTrackId(itemId) != -1) {
            QModelIndex modelIndex = isClip(itemId) ? makeClipIndexFromID(itemId) : makeCompositionIndexFromID(itemId);
            if (right) {
                notifyChange(modelIndex, modelIndex, false, true, logUndo);
            } else {
                notifyChange(modelIndex, modelIndex, true, true, logUndo);
            }
        }
        return true;
    };
    bool result = false;
    if (isClip(itemId)) {
        result = m_allClips[itemId]->requestResize(size, right, undo, redo);
    } else {
        result = m_allCompositions[itemId]->requestResize(size, right, undo, redo);
    }
    if (result) {
        PUSH_LAMBDA(update_model, undo);
        PUSH_LAMBDA(update_model, redo);
        update_model();
    }
    return result;
}

bool TimelineModel::requestClipTrim(int clipId, int delta, bool right, bool ripple, bool logUndo)
{
    return requestItemResize(clipId, m_allClips[clipId]->getPlaytime() - delta, right, logUndo);
}

bool TimelineModel::requestClipsGroup(const std::unordered_set<int> &ids)
{
#ifdef LOGGING
    std::stringstream group;
    m_logFile << "{" << std::endl;
    m_logFile << "auto group = {";
    bool deb = true;
    for (int clipId : ids) {
        if (deb)
            deb = false;
        else
            group << ", ";
        group << clipId;
    }
    m_logFile << group.str() << "};" << std::endl;
    m_logFile << "timeline->requestClipsGroup(group);" << std::endl;
    m_logFile << std::endl << "}" << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    for (int id : ids) {
        if (isClip(id)) {
            if (getClipTrackId(id) == -1) {
                return false;
            }
        } else if (!isGroup(id)) {
            return false;
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    int groupId = m_groups->groupItems(ids, undo, redo);
    if (groupId != -1) {
        PUSH_UNDO(undo, redo, i18n("Group clips"));
    }
    return (groupId != -1);
}

bool TimelineModel::requestClipUngroup(int id)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipUngroup(" << id << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestClipUngroup(id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Ungroup clips"));
    }
    return result;
}

bool TimelineModel::requestClipUngroup(int id, Fun &undo, Fun &redo)
{
    return m_groups->ungroupItem(id, undo, redo);
}

bool TimelineModel::requestTrackInsertion(int position, int &id)
{
#ifdef LOGGING
    m_logFile << "timeline->requestTrackInsertion(" << position << ", dummy_id ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestTrackInsertion(position, id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Insert Track"));
    }
    return result;
}

bool TimelineModel::requestTrackInsertion(int position, int &id, Fun &undo, Fun &redo)
{
    if (position == -1) {
        position = (int)(m_allTracks.size());
    }
    if (position < 0 || position > (int)m_allTracks.size()) {
        return false;
    }
    int trackId = TimelineModel::getNextId();
    id = trackId;
    Fun local_undo = deregisterTrack_lambda(trackId);
    TrackModel::construct(shared_from_this(), trackId, position);
    auto track = getTrackById(trackId);
    Fun local_redo = [track, position, this]() {
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is
        // sufficient to register it.
        registerTrack(track, position);
        return true;
    };
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestTrackDeletion(int trackId)
{
#ifdef LOGGING
    m_logFile << "timeline->requestTrackDeletion(" << trackId << "); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestTrackDeletion(trackId, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Delete Track"));
    }
    return result;
}

bool TimelineModel::requestTrackDeletion(int trackId, Fun &undo, Fun &redo)
{
    Q_ASSERT(isTrack(trackId));
    std::vector<int> clips_to_delete;
    for (const auto &it : getTrackById(trackId)->m_allClips) {
        clips_to_delete.push_back(it.first);
    }
    Fun local_undo = []() { return true; };
    Fun local_redo = []() { return true; };
    for (int clip : clips_to_delete) {
        bool res = true;
        while (res && m_groups->isInGroup(clip)) {
            res = requestClipUngroup(clip, local_undo, local_redo);
        }
        if (res) {
            res = requestClipDeletion(clip, local_undo, local_redo);
        }
        if (!res) {
            bool u = local_undo();
            Q_ASSERT(u);
            return false;
        }
    }
    int old_position = getTrackPosition(trackId);
    auto operation = deregisterTrack_lambda(trackId, true);
    std::shared_ptr<TrackModel> track = getTrackById(trackId);
    auto reverse = [this, track, old_position]() {
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is
        // sufficient to register it.
        registerTrack(track, old_position);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, local_undo, local_redo);
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
        return true;
    }
    local_undo();
    return false;
}

void TimelineModel::registerTrack(std::shared_ptr<TrackModel> track, int pos)
{
    int id = track->getId();
    if (pos == -1) {
        pos = static_cast<int>(m_allTracks.size());
    }
    Q_ASSERT(pos >= 0);
    Q_ASSERT(pos <= static_cast<int>(m_allTracks.size()));

    // effective insertion (MLT operation), add 1 to account for black background track
    int error = m_tractor->insert_track(*track, pos + 1);
    Q_ASSERT(error == 0); // we might need better error handling...

    // we now insert in the list
    auto posIt = m_allTracks.begin();
    std::advance(posIt, pos);
    auto it = m_allTracks.insert(posIt, std::move(track));
    // it now contains the iterator to the inserted element, we store it
    Q_ASSERT(m_iteratorTable.count(id) == 0); // check that id is not used (shouldn't happen)
    m_iteratorTable[id] = it;
    _resetView();
}

void TimelineModel::registerClip(const std::shared_ptr<ClipModel> &clip)
{
    int id = clip->getId();
    Q_ASSERT(m_allClips.count(id) == 0);
    m_allClips[id] = clip;
    m_groups->createGroupItem(id);
    clip->setTimelineEffectsEnabled(m_timelineEffectsEnabled);
}

void TimelineModel::registerGroup(int groupId)
{
    Q_ASSERT(m_allGroups.count(groupId) == 0);
    m_allGroups.insert(groupId);
}

Fun TimelineModel::deregisterTrack_lambda(int id, bool updateView)
{
    return [this, id, updateView]() {
        auto it = m_iteratorTable[id];    // iterator to the element
        int index = getTrackPosition(id); // compute index in list
        if (updateView) {
            QModelIndex root;
            _resetView();
        }
        m_tractor->remove_track(static_cast<int>(index + 1)); // melt operation, add 1 to account for black background track
        // send update to the model
        m_allTracks.erase(it);     // actual deletion of object
        m_iteratorTable.erase(id); // clean table
        return true;
    };
}

Fun TimelineModel::deregisterClip_lambda(int clipId)
{
    return [this, clipId]() {
        Q_ASSERT(m_allClips.count(clipId) > 0);
        Q_ASSERT(getClipTrackId(clipId) == -1); // clip must be deleted from its track at this point
        Q_ASSERT(!m_groups->isInGroup(clipId)); // clip must be ungrouped at this point
        m_allClips.erase(clipId);
        m_groups->destructGroupItem(clipId);
        return true;
    };
}

void TimelineModel::deregisterGroup(int id)
{
    Q_ASSERT(m_allGroups.count(id) > 0);
    m_allGroups.erase(id);
}

std::shared_ptr<TrackModel> TimelineModel::getTrackById(int trackId)
{
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    return *m_iteratorTable[trackId];
}

const std::shared_ptr<TrackModel> TimelineModel::getTrackById_const(int trackId) const
{
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    return *m_iteratorTable.at(trackId);
}

std::shared_ptr<ClipModel> TimelineModel::getClipPtr(int clipId) const
{
    Q_ASSERT(m_allClips.count(clipId) > 0);
    return m_allClips.at(clipId);
}

std::shared_ptr<CompositionModel> TimelineModel::getCompositionPtr(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    return m_allCompositions.at(compoId);
}

int TimelineModel::getNextId()
{
    return TimelineModel::next_id++;
}

bool TimelineModel::isClip(int id) const
{
    return m_allClips.count(id) > 0;
}

bool TimelineModel::isComposition(int id) const
{
    return m_allCompositions.count(id) > 0;
}

bool TimelineModel::isTrack(int id) const
{
    return m_iteratorTable.count(id) > 0;
}

bool TimelineModel::isGroup(int id) const
{
    return m_allGroups.count(id) > 0;
}

int TimelineModel::duration() const
{
    return m_tractor->get_playtime();
}

std::unordered_set<int> TimelineModel::getGroupElements(int clipId)
{
    int groupId = m_groups->getRootId(clipId);
    return m_groups->getLeaves(groupId);
}

Mlt::Profile *TimelineModel::getProfile()
{
    return m_profile;
}

bool TimelineModel::requestReset(Fun &undo, Fun &redo)
{
    std::vector<int> all_ids;
    for (const auto &track : m_iteratorTable) {
        all_ids.push_back(track.first);
    }
    bool ok = true;
    for (int trackId : all_ids) {
        ok = ok && requestTrackDeletion(trackId, undo, redo);
    }
    return ok;
}

void TimelineModel::setUndoStack(std::weak_ptr<DocUndoStack> undo_stack)
{
    m_undoStack = std::move(undo_stack);
}

int TimelineModel::requestBestSnapPos(int pos, int length, const std::vector<int> &pts)
{
    if (!pts.empty()) {
        m_snaps->ignore(pts);
    }
    int snapped_start = m_snaps->getClosestPoint(pos);
    qDebug() << "snapping start suggestion" << snapped_start;
    int snapped_end = m_snaps->getClosestPoint(pos + length);
    m_snaps->unIgnore();

    int startDiff = qAbs(pos - snapped_start);
    int endDiff = qAbs(pos + length - snapped_end);
    if (startDiff < endDiff && snapped_start >= 0) {
        // snap to start
        if (startDiff < 10) {
            return snapped_start;
        }
    } else {
        // snap to end
        if (endDiff < 10 && snapped_end >= 0) {
            return snapped_end - length;
        }
    }
    return -1;
}

int TimelineModel::requestNextSnapPos(int pos)
{
    return m_snaps->getNextPoint(pos);
}

int TimelineModel::requestPreviousSnapPos(int pos)
{
    return m_snaps->getPreviousPoint(pos);
}

void TimelineModel::registerComposition(const std::shared_ptr<CompositionModel> &composition)
{
    int id = composition->getId();
    Q_ASSERT(m_allCompositions.count(id) == 0);
    m_allCompositions[id] = composition;
    m_groups->createGroupItem(id);
}

bool TimelineModel::requestCompositionInsertion(const QString &transitionId, int trackId, int position, int length, int &id, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestCompositionInsertion(\"composite\"," << trackId << " ," << position << "," << length << ", dummy_id );" << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestCompositionInsertion(transitionId, trackId, position, length, id, undo, redo);
    if (result && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Insert Composition"));
    }
    _resetView();
    return result;
}

bool TimelineModel::requestCompositionInsertion(const QString &transitionId, int trackId, int position, int length, int &id, Fun &undo, Fun &redo)
{
    qDebug() << "Inserting compo track" << trackId << "pos" << position << "length" << length;
    int compositionId = TimelineModel::getNextId();
    id = compositionId;
    Fun local_undo = deregisterComposition_lambda(compositionId);
    CompositionModel::construct(shared_from_this(), transitionId, compositionId);
    auto composition = m_allCompositions[compositionId];
    Fun local_redo = [composition, this]() {
        // We capture a shared_ptr to the composition, which means that as long as this undo object lives, the composition object is not deleted. To insert it
        // back it is sufficient to register it.
        registerComposition(composition);
        return true;
    };
    bool res = requestCompositionMove(compositionId, trackId, position, true, local_undo, local_redo);
    qDebug() << "trying to move" << trackId << "pos" << position << "succes " << res;
    if (res) {
        res = requestItemResize(compositionId, length, true, true, local_undo, local_redo);
        qDebug() << "trying to resize" << compositionId << "length" << length << "succes " << res;
    }
    if (!res) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        id = -1;
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

Fun TimelineModel::deregisterComposition_lambda(int compoId)
{
    return [this, compoId]() {
        Q_ASSERT(m_allCompositions.count(compoId) > 0);
        Q_ASSERT(!m_groups->isInGroup(compoId)); // composition must be ungrouped at this point
        m_allCompositions.erase(compoId);
        m_groups->destructGroupItem(compoId);
        return true;
    };
}

int TimelineModel::getCompositionPosition(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    return trans->getPosition();
}

int TimelineModel::getCompositionPlaytime(int compoId) const
{
    READ_LOCK();
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    int playtime = trans->getPlaytime();
    return playtime;
}

int TimelineModel::getTrackCompositionsCount(int compoId) const
{
    return getTrackById_const(compoId)->getCompositionsCount();
}

bool TimelineModel::requestCompositionMove(int compoId, int trackId, int position, bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestCompositionMove(" << compoId << "," << trackId << " ," << position << ", " << (updateView ? "true" : "false") << ", "
              << (logUndo ? "true" : "false") << " ); " << std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isComposition(compoId));
    if (m_allCompositions[compoId]->getPosition() == position && getCompositionTrackId(compoId) == trackId) {
        return true;
    }
    if (m_groups->isInGroup(compoId)) {
        // element is in a group.
        int groupId = m_groups->getRootId(compoId);
        int current_trackId = getCompositionTrackId(compoId);
        int track_pos1 = getTrackPosition(trackId);
        int track_pos2 = getTrackPosition(current_trackId);
        int delta_track = track_pos1 - track_pos2;
        int delta_pos = position - m_allCompositions[compoId]->getPosition();
        return requestGroupMove(compoId, groupId, delta_track, delta_pos, updateView, logUndo);
    }
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    bool res = requestCompositionMove(compoId, trackId, position, updateView, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move composition"));
    }
    return res;
}

bool TimelineModel::requestCompositionMove(int compoId, int trackId, int position, bool updateView, Fun &undo, Fun &redo)
{
    qDebug() << "Requesting composition move" << trackId << "," << position;
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isComposition(compoId));
    Q_ASSERT(isTrack(trackId));
    int previousTrack = getPreviousTrackId(trackId);
    if (previousTrack == -1) {
        // it doesn't make sense to insert a composition on the last track
        qDebug() << "Move failed because of last track";
        return false;
    }
    Fun local_undo = []() { return true; };
    Fun local_redo = []() { return true; };
    bool ok = true;
    int old_trackId = getCompositionTrackId(compoId);
    if (old_trackId != -1) {
        Fun delete_operation = []() { return true; };
        Fun delete_reverse = []() { return true; };
        if (old_trackId != trackId) {
            delete_operation = [this, compoId]() {
                bool res = unplantComposition(compoId);
                if (res) m_allCompositions[compoId]->setATrack(-1);
                return res;
            };
            int oldAtrack = m_allCompositions[compoId]->getATrack();
            delete_reverse = [this, compoId, oldAtrack]() {
                m_allCompositions[compoId]->setATrack(oldAtrack);
                return replantCompositions(compoId);
            };
        }
        ok = delete_operation();
        if (!ok) qDebug() << "Move failed because of first delete operation";

        if (ok) {
            UPDATE_UNDO_REDO(delete_operation, delete_reverse, local_undo, local_redo);
            ok = getTrackById(old_trackId)->requestCompositionDeletion(compoId, updateView, local_undo, local_redo);
        }
        if (!ok) {
            qDebug() << "Move failed because of first deletion request";
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    ok = getTrackById(trackId)->requestCompositionInsertion(compoId, position, updateView, local_undo, local_redo);
    if (!ok) qDebug() << "Move failed because of second insertion request";
    if (ok) {
        Fun insert_operation = []() { return true; };
        Fun insert_reverse = []() { return true; };
        if (old_trackId != trackId) {
            insert_operation = [this, compoId, trackId, previousTrack]() {
                m_allCompositions[compoId]->setATrack(previousTrack);
                return replantCompositions(compoId);
            };
            insert_reverse = [this, compoId]() {
                bool res = unplantComposition(compoId);
                if (res) m_allCompositions[compoId]->setATrack(-1);
                return res;
            };
        }
        ok = insert_operation();
        if (!ok) qDebug() << "Move failed because of second insert operation";
        if (ok) {
            UPDATE_UNDO_REDO(insert_operation, insert_reverse, local_undo, local_redo);
        }
    }
    if (!ok) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::replantCompositions(int currentCompo)
{
    // We ensure that the compositions are planted in a decreasing order of b_track.
    // For that, there is no better option than to disconnect every composition and then reinsert everything in the correct order.

    std::vector<std::pair<int, int>> compos;
    for (const auto &compo : m_allCompositions) {
        int trackId = compo.second->getCurrentTrackId();
        if (trackId == -1 || compo.second->getATrack() == -1) {
            continue;
        }
        // Note: we need to retrieve the position of the track, that is its melt index.
        int trackPos = getTrackMltIndex(trackId);
        compos.push_back({trackPos, compo.first});
        if (compo.first != currentCompo) {
            unplantComposition(compo.first);
        }
    }

    // sort by decreasing b_track
    std::sort(compos.begin(), compos.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) { return a.first > b.first; });

    // replant
    QScopedPointer<Mlt::Field> field(m_tractor->field());
    for (const auto &compo : compos) {
        int aTrack = m_allCompositions[compo.second]->getATrack();
        Q_ASSERT(aTrack != -1);
        aTrack = getTrackMltIndex(aTrack);
        int ret = field->plant_transition(*m_allCompositions[compo.second].get(), aTrack, compo.first);
        qDebug() << "Planting composition " << compo.second << "in " << aTrack << "/" << compo.first << "IN = " << m_allCompositions[compo.second]->getIn()
                 << "OUT = " << m_allCompositions[compo.second]->getOut() << "ret=" << ret;

        Mlt::Transition &transition = *m_allCompositions[compo.second].get();
        mlt_service consumer = mlt_service_consumer(transition.get_service());
        Q_ASSERT(consumer != nullptr);
        if (ret != 0) {
            return false;
        }
    }
    QModelIndex modelIndex = makeCompositionIndexFromID(currentCompo);
    QVector<int> roles;
    roles.push_back(ItemATrack);
    notifyChange(modelIndex, modelIndex, roles);
    return true;
}

bool TimelineModel::unplantComposition(int compoId)
{
    qDebug() << "Unplanting" << compoId;
    Mlt::Transition &transition = *m_allCompositions[compoId].get();
    mlt_service consumer = mlt_service_consumer(transition.get_service());
    Q_ASSERT(consumer != nullptr);
    m_tractor->field()->disconnect_service(transition);
    int ret = transition.disconnect_all_producers();

    mlt_service nextservice = mlt_service_get_producer(transition.get_service());
    // mlt_service consumer = mlt_service_consumer(transition.get_service());
    Q_ASSERT(nextservice == nullptr);
    // Q_ASSERT(consumer == nullptr);
    return ret != 0;
}

bool TimelineModel::checkConsistency()
{
    for (const auto &track : m_iteratorTable) {
        if (!(*track.second)->checkConsistency()) {
            qDebug() << "Constistency check failed for track" << track.first;
            return false;
        }
    }

    // We now check consistenty of the compositions. For that, we list all compositions of the tractor, and see if we have a matching one in our
    // m_allCompositions
    std::unordered_set<int> remaining_compo;
    for (const auto &compo : m_allCompositions) {
        if (getCompositionTrackId(compo.first) != -1 && m_allCompositions[compo.first]->getATrack() != -1) {
            remaining_compo.insert(compo.first);

            // check validity of the consumer
            Mlt::Transition &transition = *m_allCompositions[compo.first].get();
            mlt_service consumer = mlt_service_consumer(transition.get_service());
            Q_ASSERT(consumer != nullptr);
        }
    }
    QScopedPointer<Mlt::Field> field(m_tractor->field());
    field->lock();

    mlt_service nextservice = mlt_service_get_producer(field->get_service());
    mlt_service_type mlt_type = mlt_service_identify(nextservice);
    while (nextservice != nullptr) {
        if (mlt_type == transition_type) {
            mlt_transition tr = (mlt_transition)nextservice;
            int currentTrack = mlt_transition_get_b_track(tr);
            int currentATrack = mlt_transition_get_a_track(tr);

            int currentIn = (int)mlt_transition_get_in(tr);
            int currentOut = (int)mlt_transition_get_out(tr);

            qDebug() << "looking composition IN: " << currentIn << ", OUT: " << currentOut << ", TRACK: " << currentTrack << " / " << currentATrack;
            int foundId = -1;
            // we iterate to try to find a matching compo
            for (int compoId : remaining_compo) {
                if (getTrackMltIndex(getCompositionTrackId(compoId)) == currentTrack &&
                    getTrackMltIndex(m_allCompositions[compoId]->getATrack()) == currentATrack && m_allCompositions[compoId]->getIn() == currentIn &&
                    m_allCompositions[compoId]->getOut() == currentOut) {
                    foundId = compoId;
                    break;
                }
            }
            if (foundId == -1) {
                qDebug() << "Error, we didn't find matching composition IN: " << currentIn << ", OUT: " << currentOut << ", TRACK: " << currentTrack << " / "
                         << currentATrack;
                field->unlock();
                return false;
            }
            qDebug() << "Found";

            remaining_compo.erase(foundId);
        }
        nextservice = mlt_service_producer(nextservice);
        if (nextservice == nullptr) {
            break;
        }
        mlt_type = mlt_service_identify(nextservice);
    }
    field->unlock();

    if (!remaining_compo.empty()) {
        qDebug() << "Error: We found less compositions than expected. Compositions that have not been found:";
        for (int compoId : remaining_compo) {
            qDebug() << compoId;
        }
        return false;
    }
    return true;
}

bool TimelineModel::requestItemResizeToPos(int itemId, int position, bool right)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(itemId) || isComposition(itemId));
    int in, out;
    if (isClip(itemId)) {
        in = getClipPosition(itemId);
        out = in + getClipPlaytime(itemId) - 1;
    } else {
        in = getCompositionPosition(itemId);
        out = in + getCompositionPlaytime(itemId) - 1;
    }
    int size = 0;
    if (!right) {
        if (position < out) {
            size = out - position;
        }
    } else {
        if (position > in) {
            size = position - in;
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool result = requestItemResize(itemId, size, right, true, undo, redo);
    if (result) {
        if (isClip(itemId)) {
            PUSH_UNDO(undo, redo, i18n("Resize clip"));
        } else {
            PUSH_UNDO(undo, redo, i18n("Resize composition"));
        }
    }
    return result;
}

void TimelineModel::setTimelineEffectsEnabled(bool enabled)
{
    m_timelineEffectsEnabled = enabled;
    // propagate info to clips
    for (const auto &clip : m_allClips) {
        clip.second->setTimelineEffectsEnabled(enabled);
    }

    // TODO if we support track effects, they should be disabled here too
}

Mlt::Producer *TimelineModel::producer()
{
    auto *prod = new Mlt::Producer(tractor());
    return prod;
}

void TimelineModel::checkRefresh(int start, int end) const
{
    int currentPos = tractor()->position();
    if (currentPos > start && currentPos < end) {
        pCore->requestMonitorRefresh();
    }
}
