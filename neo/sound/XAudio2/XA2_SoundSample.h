/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#ifndef __XA2_SOUNDSAMPLE_H__
#define __XA2_SOUNDSAMPLE_H__

/*
================================================
idSoundSample_XAudio2

This is the actual implementation class.
The base idSoundSample is defined elsewhere.
================================================
*/
class idSoundSample_XAudio2 {
public:
                    idSoundSample_XAudio2();
    virtual			~idSoundSample_XAudio2();

    // Loads and initializes the resource based on the name.
    virtual void	 LoadResource();

    void			SetName( const char * n ) { name = n; }
    const char *	GetName() const { return name; }
    ID_TIME_T		GetTimestamp() const { return timestamp; }

    // turns it into a beep
    void			MakeDefault();

    // frees all data
    void			FreeData();

    int				LengthInMsec() const { return SamplesToMsec( NumSamples(), SampleRate() ); }
    int				SampleRate() const { return format.basic.samplesPerSec; }
    int				NumSamples() const { return playLength; }
    int				NumChannels() const { return format.basic.numChannels; }
    int				BufferSize() const { return totalBufferSize; }

    bool			IsCompressed() const { return ( format.basic.formatTag != idWaveFile::FORMAT_PCM ); }

    bool			IsDefault() const { return timestamp == FILE_NOT_FOUND_TIMESTAMP; }
    bool			IsLoaded() const { return loaded; }

    void			SetNeverPurge() { neverPurge = true; }
    bool			GetNeverPurge() const { return neverPurge; }

    void			SetLevelLoadReferenced() { levelLoadReferenced = true; }
    void			ResetLevelLoadReferenced() { levelLoadReferenced = false; }
    bool			GetLevelLoadReferenced() const { return levelLoadReferenced; }

    int				GetLastPlayedTime() const { return lastPlayedTime; }
    void			SetLastPlayedTime( int t ) { lastPlayedTime = t; }

    float			GetAmplitude( int timeMS ) const;

    // Add these new member functions
    bool                    SavePCMAsWav( const idStr& filename );
    bool                    LoadXMAFile( const idStr& filename );
    bool                    ConvertPCMToXMA2( const idStr& originalFilename );
    void                    ConvertToXMA2Format( const idStr& originalFilename );

    // XAudio2 specific members
    struct sampleBuffer_t {
        void * buffer;
        int bufferSize;
        int numSamples;
    };

    idList<sampleBuffer_t, TAG_AUDIO> buffers;
    idWaveFile::waveFmt_t	format;

protected:
    friend class idSoundHardware_XAudio2;
    friend class idSoundVoice_XAudio2;

    bool			LoadWav( const idStr & filename );  // Keep this protected
    bool			LoadAmplitude( const idStr & name );
    void			WriteAllSamples( const idStr &sampleName );
    bool			LoadGeneratedSample( const idStr &name );
    void			WriteGeneratedSample( idFile *fileOut );

    idStr			name;

    ID_TIME_T		timestamp;
    bool			loaded;

    bool			neverPurge;
    bool			levelLoadReferenced;
    bool			usesMapHeap;

    uint32			lastPlayedTime;

    int				totalBufferSize;	// total size of all the buffers

    int				playBegin;
    int				playLength;

    idList<byte, TAG_AMPLITUDE> amplitude;
};

#endif
