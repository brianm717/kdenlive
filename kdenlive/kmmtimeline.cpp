/***************************************************************************
                          kmmtimeline  -  description
                             -------------------
    begin                : Wed Dec 24 2003
    copyright            : (C) 2003 by Jason Wood
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
#include "kmmtimeline.h"

KMMTimeLine::KMMTimeLine( QWidget *scrollToolWidget, QWidget *parent , const char *name) :
 					KTimeLine( scrollToolWidget, parent, name)
{
}


KMMTimeLine::~KMMTimeLine()
{
}

void KMMTimeLine::invalidateClipBuffer( DocClipRef *clip )
{
	#warning - unoptimised, should only update that part of the back buffer that needs to be updated. Current implementaion
	#warning - wipes the entire buffer.
	invalidateBackBuffer();
}
