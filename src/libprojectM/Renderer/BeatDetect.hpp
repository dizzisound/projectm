/**
 * projectM -- Milkdrop-esque visualisation SDK
 * Copyright (C)2003-2007 projectM Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * See 'LICENSE.txt' included within this release
 *
 */
/**
 * $Id$
 *
 * Beat detection class. Takes decompressed sound buffers and returns
 * various characteristics
 *
 * $Log$
 *
 */

#ifndef _BEAT_DETECT_H
#define _BEAT_DETECT_H

#include "../PCM.hpp"
#include "../dlldefs.h"
#include <algorithm>
#include <cmath>

#include "../Common.hpp"

//begin: qfix
#if defined(_WIN32) && defined(__GNUC__) 
#ifndef projectM_isnan
#define projectM_isnan std::isnan
#endif
#endif
//end: qfix

class DLLEXPORT BeatDetect
{
	public:
		float treb ;
		float mid ;
		float bass ;
		float vol_old ;
		float beat_sensitivity;
		float treb_att ;
		float mid_att ;
		float bass_att ;
		float vol;
        float vol_att ;

		PCM *pcm;

		/** Methods */
		BeatDetect(PCM *pcm);
		~BeatDetect();
		void initBeatDetect();
		void reset();
		void detectFromSamples();
		void getBeatVals ( float *vdataL, float *vdataR );
		float getPCMScale()
		{
			// added to address https://github.com/projectM-visualizer/projectm/issues/161
			// Returning 1.0 results in using the raw PCM data, which can make the presets look pretty unresponsive
			// if the application volume is low.
#ifdef WIN32
// this is broken?
#undef max
			//work0around
#endif /** WIN32 */
			return 0.5f / std::max(0.0001f,sqrtf(vol_history));
		}

	private:
		/** Vars */
		float beat_buffer[32][80],
		beat_instant[32],
		beat_history[32];
		float beat_val[32],
		beat_att[32],
		beat_variance[32];
		int beat_buffer_pos;
		float vol_buffer[80],
		vol_instant;
		float vol_history;
};

#endif /** !_BEAT_DETECT_H */
