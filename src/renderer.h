/***************************************************************************
                         krender.h  -  description
                            -------------------
   begin                : Fri Nov 22 2002
   copyright            : (C) 2002 by Jason Wood
   email                : jasonwood@blueyonder.co.uk
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef RENDERER_H
#define RENDERER_H

#include "gentime.h"
#include "definitions.h"

#include <kurl.h>

#include <qdom.h>
#include <qstring.h>
#include <qmap.h>
#include <QList>
#include <QEvent>

#ifdef Q_WS_MAC
#include "videoglwidget.h"
#endif


/**Render encapsulates the client side of the interface to a renderer.
From Kdenlive's point of view, you treat the Render object as the
renderer, and simply use it as if it was local. Calls are asyncrhonous -
you send a call out, and then receive the return value through the
relevant signal that get's emitted once the call completes.
  *@author Jason Wood
  */

class Render;

class QTimer;
class QPixmap;

namespace Mlt
{
class Consumer;
class Playlist;
class Tractor;
class Transition;
class Frame;
class Producer;
class Filter;
class Profile;
class Service;
};


class MltErrorEvent : public QEvent
{
public:
    MltErrorEvent(QString message) : QEvent(QEvent::User), m_message(message) {}
    QString message() const {
        return m_message;
    }

private:
    QString m_message;
};


class Render: public QObject
{
Q_OBJECT public:

    enum FailStates { OK = 0,
                      APP_NOEXIST
                    };

    Render(const QString & rendererName, int winid, int extid, QString profile = QString(), QWidget *parent = 0);
    ~Render();

    /** Seeks the renderer clip to the given time. */
    void seek(GenTime time);
    void seekToFrame(int pos);
    void seekToFrameDiff(int diff);
    int m_isBlocked;

    //static QPixmap getVideoThumbnail(char *profile, QString file, int frame, int width, int height);
    QPixmap getImageThumbnail(KUrl url, int width, int height);

    /** Return thumbnail for color clip */
    //void getImage(int id, QString color, QPoint size);

    // static QPixmap frameThumbnail(Mlt::Frame *frame, int width, int height, bool border = false);

    /** Return thumbnail for image clip */
    //void getImage(KUrl url, QPoint size);

    /** Requests a particular frame from the given file.
     *
     * The pixmap will be returned by emitting the replyGetImage() signal.
     * */
    //void getImage(KUrl url, int frame, QPoint size);


    /** Wraps the VEML command of the same name. Sets the current scene list to
    be list. */
    int setSceneList(QDomDocument list, int position = 0);
    int setSceneList(QString playlist, int position = 0);
    int setProducer(Mlt::Producer *producer, int position);
    const QString sceneList();
    bool saveSceneList(QString path, QDomElement kdenliveData = QDomElement());

    /** Wraps the VEML command of the same name. Tells the renderer to
    play the current scene at the speed specified, relative to normal
    playback. e.g. 1.0 is normal speed, 0.0 is paused, -1.0 means play
    backwards. Does not specify start/stop times for playback.*/
    void play(double speed);
    void switchPlay();
    void pause();
    /** stop playing */
    void stop(const GenTime & startTime);
    void setVolume(double volume);

    QImage extractFrame(int frame_position, int width = -1, int height = -1);
    /** Wraps the VEML command of the same name. Tells the renderer to
    play the current scene at the speed specified, relative to normal
    playback. e.g. 1.0 is normal speed, 0.0 is paused, -1.0 means play
    backwards. Specifes the start/stop times for playback.*/
    void play(const GenTime & startTime);
    void playZone(const GenTime & startTime, const GenTime & stopTime);
    void loopZone(const GenTime & startTime, const GenTime & stopTime);

    void saveZone(KUrl url, QString desc, QPoint zone);

    /** Returns the name of the renderer. */
    const QString & rendererName() const;

    /** Returns the speed at which the renderer is currently playing, 0.0 if the renderer is
    not playing anything. */
    double playSpeed();
    /** Returns the current seek position of the renderer. */
    GenTime seekPosition() const;
    int seekFramePosition() const;

    void emitFrameNumber(double position);
    void emitConsumerStopped();

    /** Gives the aspect ratio of the consumer */
    double consumerRatio() const;

    void doRefresh();

    /** Save current producer frame as image */
    void exportCurrentFrame(KUrl url, bool notify);

    /** Turn on or off on screen display */
    void refreshDisplay();
    int resetProfile(const QString profileName);
    double fps() const;
    int renderWidth() const;
    int renderHeight() const;
    /** get display aspect ratio */
    double dar() const;

    /** Playlist manipulation */
    int mltInsertClip(ItemInfo info, QDomElement element, Mlt::Producer *prod, bool overwrite = false, bool push = false);
    bool mltUpdateClip(ItemInfo info, QDomElement element, Mlt::Producer *prod);
    void mltCutClip(int track, GenTime position);
    void mltInsertSpace(QMap <int, int> trackClipStartList, QMap <int, int> trackTransitionStartList, int track, const GenTime duration, const GenTime timeOffset);
    int mltGetSpaceLength(const GenTime pos, int track, bool fromBlankStart);
    int mltTrackDuration(int track);
    bool mltResizeClipEnd(ItemInfo info, GenTime clipDuration);
    bool mltResizeClipStart(ItemInfo info, GenTime diff);
    bool mltResizeClipCrop(ItemInfo info, GenTime diff);
    bool mltMoveClip(int startTrack, int endTrack, GenTime pos, GenTime moveStart, Mlt::Producer *prod, bool overwrite = false, bool insert = false);
    bool mltMoveClip(int startTrack, int endTrack, int pos, int moveStart, Mlt::Producer *prod, bool overwrite = false, bool insert = false);
    bool mltRemoveClip(int track, GenTime position);
    /** Delete an effect to a clip in MLT's playlist */
    bool mltRemoveEffect(int track, GenTime position, QString index, bool updateIndex, bool doRefresh = true);
    /** Add an effect to a clip in MLT's playlist */
    bool mltAddEffect(int track, GenTime position, EffectsParameterList params, bool doRefresh = true);
    /** Edit an effect parameters in MLT */
    bool mltEditEffect(int track, GenTime position, EffectsParameterList params);
    /** This only updates the "kdenlive_ix" (index) value of an effect */
    void mltUpdateEffectPosition(int track, GenTime position, int oldPos, int newPos);
    /** This changes the order of effects in MLT, inverting effects from oldPos and newPos, also updating the kdenlive_ix value */
    void mltMoveEffect(int track, GenTime position, int oldPos, int newPos);
    /** This changes the state of a track, enabling / disabling audio and video */
    void mltChangeTrackState(int track, bool mute, bool blind);
    bool mltMoveTransition(QString type, int startTrack,  int newTrack, int newTransitionTrack, GenTime oldIn, GenTime oldOut, GenTime newIn, GenTime newOut);
    bool mltAddTransition(QString tag, int a_track, int b_track, GenTime in, GenTime out, QDomElement xml, bool refresh = true);
    void mltDeleteTransition(QString tag, int a_track, int b_track, GenTime in, GenTime out, QDomElement xml, bool refresh = true);
    void mltUpdateTransition(QString oldTag, QString tag, int a_track, int b_track, GenTime in, GenTime out, QDomElement xml);
    void mltUpdateTransitionParams(QString type, int a_track, int b_track, GenTime in, GenTime out, QDomElement xml);
    void mltAddClipTransparency(ItemInfo info, int transitiontrack, int id);
    void mltMoveTransparency(int startTime, int endTime, int startTrack, int endTrack, int id);
    void mltDeleteTransparency(int pos, int track, int id);
    void mltResizeTransparency(int oldStart, int newStart, int newEnd, int track, int id);
    void mltInsertTrack(int ix, bool videoTrack);
    void mltDeleteTrack(int ix);
    bool mltUpdateClipProducer(int track, int pos, Mlt::Producer *prod);

    /** Change speed of a clip in playlist. To do this, we create a new "framebuffer" producer.
    This new producer must have its "resource" param set to: video.mpg?0.6 where video.mpg is the path
    to the clip and 0.6 is the speed in percents. The newly created producer will have it's
    "id" parameter set to: "slowmotion:parentid:speed", where parentid is the id of the original clip
    in the ClipManager list and speed is the current speed */
    int mltChangeClipSpeed(ItemInfo info, ItemInfo speedIndependantInfo, double speed, double oldspeed, int strobe, Mlt::Producer *prod);

    const QList <Mlt::Producer *> producersList();
    void updatePreviewSettings();
    void setDropFrames(bool show);
    QString updateSceneListFps(double current_fps, double new_fps, QString scene);
#ifdef Q_WS_MAC
    void showFrame(Mlt::Frame&);
#endif
    QList <int> checkTrackSequence(int);

private:   // Private attributes & methods
    /** The name of this renderer - useful to identify the renderes by what they do - e.g. background rendering, workspace monitor, etc... */
    QString m_name;
    Mlt::Consumer * m_mltConsumer;
    Mlt::Producer * m_mltProducer;
    Mlt::Profile *m_mltProfile;
    double m_framePosition;
    double m_fps;

    /** true if we are playing a zone (ie the in and out properties have been temporarily changed) */
    bool m_isZoneMode;
    bool m_isLoopMode;
    GenTime m_loopStart;
    int m_originalOut;

    /** true when monitor is in split view (several tracks at the same time) */
    bool m_isSplitView;

    Mlt::Producer *m_blackClip;
    QString m_activeProfile;

    QTimer *m_osdTimer;

    /** A human-readable description of this renderer. */
    int m_winid;

#ifdef Q_WS_MAC
    VideoGLWidget *m_glWidget;
#endif

    /** Sets the description of this renderer to desc. */
    void closeMlt();
    void mltCheckLength(Mlt::Tractor *tractor);
    void mltPasteEffects(Mlt::Producer *source, Mlt::Producer *dest);
    QMap<QString, QString> mltGetTransitionParamsFromXml(QDomElement xml);
    QMap<QString, Mlt::Producer *> m_slowmotionProducers;
    void buildConsumer(const QString profileName);
    void resetZoneMode();
    void fillSlowMotionProducers();

private slots:  // Private slots
    /** refresh monitor display */
    void refresh();
    void slotOsdTimeout();
    int connectPlaylist();
    //void initSceneList();

signals:   // Signals
    /** emitted when the renderer recieves a reply to a getFileProperties request. */
    void replyGetFileProperties(const QString &clipId, Mlt::Producer*, const QMap < QString, QString > &, const QMap < QString, QString > &, bool);

    /** emitted when the renderer recieves a reply to a getImage request. */
    void replyGetImage(const QString &, const QPixmap &);

    /** Emitted when the renderer stops, either playing or rendering. */
    void stopped();
    /** Emitted when the renderer starts playing. */
    void playing(double);
    /** Emitted when the renderer is rendering. */
    void rendering(const GenTime &);
    /** Emitted when rendering has finished */
    void renderFinished();
    /** Emitted when the current seek position has been changed by the renderer. */
//    void positionChanged(const GenTime &);
    /** Emitted when an error occurs within this renderer. */
    void error(const QString &, const QString &);
    void durationChanged(int);
    void rendererPosition(int);
    void rendererStopped(int);
    void removeInvalidClip(const QString &, bool replaceProducer);
    void refreshDocumentProducers();
    /** Used on OS X - emitted when a frame's image is to be shown. */
    void showImageSignal(QImage);

public slots:  // Public slots
    /** Start Consumer */
    void start();
    /** Stop Consumer */
    void stop();
    int getLength();
    /** If the file is readable by mlt, return true, otherwise false */
    bool isValid(KUrl url);

    /** Wraps the VEML command of the same name. Requests the file properties
    for the specified url from the renderer. Upon return, the result will be emitted
    via replyGetFileProperties(). */
    void getFileProperties(const QDomElement xml, const QString &clipId, int imageHeight, bool replaceProducer = true);

    void exportFileToFirewire(QString srcFileName, int port, GenTime startTime, GenTime endTime);
    static char *decodedString(QString str);
    void mltSavePlaylist();
    void slotSplitView(bool doit);
    void slotSwitchFullscreen();
};

#endif
