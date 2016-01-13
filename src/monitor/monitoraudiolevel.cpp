/*
Copyright (C) 2016  Jean-Baptiste Mardelle <jb@kdenlive.org>
This file is part of Kdenlive. See www.kdenlive.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License or (at your option) version 3 or any later version
accepted by the membership of KDE e.V. (or its successor approved
by the membership of KDE e.V.), which shall act as a proxy 
defined in Section 14 of version 3 of the license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "monitoraudiolevel.h"

#include <QPainter>
#include <QPaintEvent>
#include <QStylePainter>
#include <QVBoxLayout>

//----------------------------------------------------------------------------
// IEC standard dB scaling -- as borrowed from meterbridge (c) Steve Harris

static inline double IEC_Scale(double dB)
{
    double fScale = 1.0f;

    if (dB < -70.0f)
        fScale = 0.0f;
    else if (dB < -60.0f)
        fScale = (dB + 70.0f) * 0.0025f;
    else if (dB < -50.0f)
        fScale = (dB + 60.0f) * 0.005f + 0.025f;
    else if (dB < -40.0)
        fScale = (dB + 50.0f) * 0.0075f + 0.075f;
    else if (dB < -30.0f)
        fScale = (dB + 40.0f) * 0.015f + 0.15f;
    else if (dB < -20.0f)
        fScale = (dB + 30.0f) * 0.02f + 0.3f;
    else if (dB < -0.001f || dB > 0.001f)  /* if (dB < 0.0f) */
        fScale = (dB + 20.0f) * 0.025f + 0.5f;

    return fScale;
}

static inline double IEC_ScaleMax(double dB, double max)
{
    return IEC_Scale(dB) / IEC_Scale(max);
}


MyAudioWidget::MyAudioWidget(int height, QWidget *parent) : QWidget(parent)
{
    setMaximumHeight(height);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
}


void MyAudioWidget::resizeEvent ( QResizeEvent * event )
{
    drawBackground(m_peaks.size());
    QWidget::resizeEvent(event);
}

void MyAudioWidget::refreshPixmap()
{
    drawBackground(m_peaks.size());
}

void MyAudioWidget::drawBackground(int channels)
{
    QSize newSize = QWidget::size();
    QLinearGradient gradient(0, 0, newSize.width(), 0);
    gradient.setColorAt(0.0, QColor(Qt::darkGreen));
    gradient.setColorAt(0.7142, QColor(Qt::green));
    gradient.setColorAt(0.7143, Qt::yellow);
    gradient.setColorAt(0.881, Qt::darkYellow);
    gradient.setColorAt(0.9525, Qt::red);
    m_pixmap = QPixmap(newSize);
    m_pixmap.fill(Qt::transparent);
    int totalHeight;
    if (channels < 2) {
        m_channelHeight = newSize.height() / 2;
        totalHeight = m_channelHeight;
    } else {
        m_channelHeight = (newSize.height() - 2 * (channels -1)) / channels;
        totalHeight = channels * m_channelHeight + (channels - 1) * 2;
    }
    QRect rect(0, 0, newSize.width(), totalHeight);
    QPainter p(&m_pixmap);
    p.setOpacity(0.4);
    p.fillRect(rect, QBrush(gradient));
    p.setOpacity(1);
    double steps = rect.width() / 12;
    p.setPen(palette().dark().color());
    for (int i = 1; i < 12; i++) {
        p.drawLine(i * steps, 0, i * steps, totalHeight - 1);
    }
    p.setCompositionMode(QPainter::CompositionMode_Source);
    for (int i = 0; i < channels; i++) {
        p.drawRect(0, i * m_channelHeight + (i * 2), rect.width() - 1, m_channelHeight - 1);
        if (i > 0) p.fillRect(0, i * m_channelHeight + 2 * (i - 1), rect.width(), 2, Qt::transparent);
    }
    p.end();
}

void MyAudioWidget::setAudioValues(const QList <int>& values)
{
    m_values = values;
    if (m_peaks.size() != m_values.size()) {
        m_peaks = values;
        drawBackground(values.size());
    } else {
        for (int i = 0; i < m_values.size(); i++) {
            m_peaks[i] --;
            if (m_values.at(i) > m_peaks.at(i)) {
                m_peaks[i] = m_values.at(i);
            }
        }
    }
    update();
}

void MyAudioWidget::paintEvent(QPaintEvent *pe)
{
    QPainter p(this);
    p.setClipRect(pe->rect());
    QRect rect(0, 0, width(), height());
    QList <int> vals = m_values;
    if (vals.isEmpty()) {
        p.setOpacity(0.2);
        p.drawPixmap(rect, m_pixmap);
        return;
    }
    p.drawPixmap(rect, m_pixmap);
    p.setPen(palette().dark().color());
    p.setOpacity(0.9);
    for (int i = 0; i < m_values.count(); i++) {
        if (m_values.at(i) >= 100) continue;
        p.fillRect(m_values.at(i) / 100.0 * rect.width(), i * m_channelHeight + (i * 2), rect.width(), m_channelHeight, palette().dark());
        p.fillRect(m_peaks.at(i) / 100.0 * rect.width(), i * m_channelHeight + (i * 2), 1, m_channelHeight, palette().text());
    }
}


MonitorAudioLevel::MonitorAudioLevel(QObject *parent) : QObject(parent)
  , m_pBar1(0)
{
}

QWidget *MonitorAudioLevel::createProgressBar(int height, QWidget *parent)
{
    QWidget *w = new QWidget(parent);
    w->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    QVBoxLayout *lay = new QVBoxLayout;
    w->setLayout(lay);
    m_pBar1 = new MyAudioWidget(height / 1.2, w);
    lay->addWidget(m_pBar1);
    return w;
}

void MonitorAudioLevel::slotAudioLevels(const QVector<double> &dbLevels)
{
    QList <int> levels;
    if (!dbLevels.isEmpty()) {
        for (int i = 0; i < dbLevels.count(); i++) {
            levels << (int) (IEC_Scale(dbLevels.at(i)) * 100);
        }
    }
    m_pBar1->setAudioValues(levels);
}

void MonitorAudioLevel::setMonitorVisible(bool visible)
{
    if (m_pBar1) {
        m_pBar1->setVisible(visible);
    }
}

void MonitorAudioLevel::refreshPixmap()
{
    if (m_pBar1) {
        m_pBar1->refreshPixmap();
    }
}
