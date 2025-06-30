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
#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../snd_local.h"
#include <process.h>
#include <direct.h>  // Add this for _getcwd

extern idCVar s_useCompression;
extern idCVar s_noSound;

#define GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( x ) x

const uint32 SOUND_MAGIC_IDMSA = 0x6D7A7274;

extern idCVar sys_lang;

/*
========================
AllocBuffer
========================
*/
static void * AllocBuffer( int size, const char * name ) {
    common->Printf("DEBUG: AllocBuffer - Allocating %d bytes for '%s'\n", size, name);
    return Mem_Alloc( size, TAG_AUDIO );
}

/*
========================
FreeBuffer
========================
*/
static void FreeBuffer( void * p ) {
    common->Printf("DEBUG: FreeBuffer - Freeing buffer at %p\n", p);
    return Mem_Free( p );
}

/*
========================
SavePCMAsWav - Save PCM data as standard WAV file
========================
*/
bool idSoundSample_XAudio2::SavePCMAsWav( const idStr& filename ) {
    common->Printf("DEBUG: SavePCMAsWav - Saving PCM data to '%s'\n", filename.c_str());
    
    // Validate that we have data to save
    if (buffers.Num() == 0 || totalBufferSize == 0) {
        common->Printf("ERROR: SavePCMAsWav - No audio data to save\n");
        return false;
    }
    
    FILE* file = fopen( filename.c_str(), "wb" );
    if (!file) {
        common->Printf("ERROR: SavePCMAsWav - Failed to create file '%s'\n", filename.c_str());
        return false;
    }
    
    // Get actual format data from the loaded sample
    uint16 channels = format.basic.numChannels;
    uint32 sampleRate = format.basic.samplesPerSec;
    uint16 bitsPerSample = format.basic.bitsPerSample;
    
    // For non-PCM formats, assume 16-bit output
    if (format.basic.formatTag != idWaveFile::FORMAT_PCM) {
        bitsPerSample = 16;
        common->Printf("DEBUG: SavePCMAsWav - Converting from format %d to 16-bit PCM\n", format.basic.formatTag);
    }
    
    // Calculate actual data size from buffers
    uint32 dataSize = totalBufferSize;
    uint32 fileSize = 36 + dataSize;
    
    common->Printf("DEBUG: SavePCMAsWav - Format: %d channels, %d Hz, %d bits, %d bytes data\n", 
        channels, sampleRate, bitsPerSample, dataSize);
    
    // Write WAV header
    if (fwrite("RIFF", 1, 4, file) != 4U) {  // Fix signed/unsigned mismatch
        common->Printf("ERROR: SavePCMAsWav - Failed to write RIFF header\n");
        fclose(file);
        return false;
    }
    fwrite(&fileSize, 4, 1, file);
    fwrite("WAVE", 1, 4, file);
    
    // fmt chunk
    fwrite("fmt ", 1, 4, file);
    uint32 fmtSize = 16;
    fwrite(&fmtSize, 4, 1, file);
    uint16 formatTag = 1; // PCM
    fwrite(&formatTag, 2, 1, file);
    fwrite(&channels, 2, 1, file);
    fwrite(&sampleRate, 4, 1, file);
    uint32 avgBytesPerSec = sampleRate * channels * (bitsPerSample / 8);
    fwrite(&avgBytesPerSec, 4, 1, file);
    uint16 blockAlign = channels * (bitsPerSample / 8);
    fwrite(&blockAlign, 2, 1, file);
    fwrite(&bitsPerSample, 2, 1, file);
    
    // data chunk
    fwrite("data", 1, 4, file);
    fwrite(&dataSize, 4, 1, file);
    
    // Write actual audio data from buffers
    uint32 totalWritten = 0;
    for ( int i = 0; i < buffers.Num(); i++ ) {
        size_t written = fwrite( buffers[i].buffer, 1, buffers[i].bufferSize, file );
        totalWritten += (uint32)written;  // Cast to fix signed/unsigned mismatch
        if (written != (size_t)buffers[i].bufferSize) {  // Cast to fix signed/unsigned mismatch
            common->Printf("ERROR: SavePCMAsWav - Failed to write buffer %d (wrote %zu of %d bytes)\n", 
                i, written, buffers[i].bufferSize);
            fclose(file);
            return false;
        }
    }
    
    fclose(file);
    
    common->Printf("DEBUG: SavePCMAsWav - WAV file saved successfully, %d bytes written\n", totalWritten);
    return true;
}

/*
========================
LoadXMAFile - Load XMA2 file and update sample properly - FIXED VERSION
========================
*/
bool idSoundSample_XAudio2::LoadXMAFile( const idStr& filename ) {
    common->Printf("DEBUG: LoadXMAFile - Loading XMA file '%s'\n", filename.c_str());
    
    // CRITICAL FIX: Clear existing data BEFORE loading new XMA2 file
    // This ensures we don't have leftover PCM buffers mixed with XMA2 data
    FreeData();
    
    // Load the XMA2 file using standard LoadWav function
    bool result = LoadWav( filename );
    
    if (result) {
        common->Printf("DEBUG: LoadXMAFile - XMA file loaded successfully\n");
        common->Printf("DEBUG: LoadXMAFile - Format tag: %d\n", format.basic.formatTag);
        common->Printf("DEBUG: LoadXMAFile - Total buffers: %d\n", buffers.Num());
        common->Printf("DEBUG: LoadXMAFile - Total buffer size: %u\n", totalBufferSize);
        
        // Log buffer information
        for (int i = 0; i < buffers.Num(); i++) {
            common->Printf("DEBUG: LoadXMAFile - Buffer[%d]: size=%u, samples=%u\n", 
                i, buffers[i].bufferSize, buffers[i].numSamples);
        }
        
        // Force format tag to XMA2 if it's not already
        if (format.basic.formatTag != idWaveFile::FORMAT_XMA2) {
            common->Printf("DEBUG: LoadXMAFile - Converting format tag from %d to XMA2\n", format.basic.formatTag);
            format.basic.formatTag = idWaveFile::FORMAT_XMA2;
        }
        
    } else {
        common->Printf("ERROR: LoadXMAFile - Failed to load XMA file\n");
    }
    
    return result;
}

/*
========================
ConvertPCMToXMA2 - Real conversion using external encoder - FINAL VERSION WITH CORRECT PLAY VALUES
========================
*/
bool idSoundSample_XAudio2::ConvertPCMToXMA2( const idStr& originalFilename ) {
    common->Printf("DEBUG: ConvertPCMToXMA2 - Starting conversion for '%s'\n", originalFilename.c_str());
    
    // Validate input parameters
    if (originalFilename.IsEmpty()) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Empty filename\n");
        return false;
    }
    
    // Validate that we have audio data
    if (buffers.Num() == 0 || totalBufferSize == 0) {
        common->Printf("ERROR: ConvertPCMToXMA2 - No audio data to convert\n");
        return false;
    }
    
    // CRITICAL: Store ORIGINAL playBegin and playLength from the source audio
    // These will be preserved in the final .idxma file even though the XMA2 file has them as 0
    uint32 originalPlayBegin = playBegin;
    uint32 originalPlayLength = playLength;
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Original audio info:\n");
    common->Printf("  PlayBegin: %u samples\n", originalPlayBegin);
    common->Printf("  PlayLength: %u samples\n", originalPlayLength);
    common->Printf("  Total buffers: %d\n", buffers.Num());
    common->Printf("  Total buffer size: %u bytes\n", totalBufferSize);
    
    // Store original data before conversion - DEEP COPY
    uint32 originalTotalBufferSize = totalBufferSize;
    idWaveFile::waveFmt_t originalFormat = format;
    bool originalLoaded = loaded;
    
    // Create backup buffers with deep copy
    idList<sampleBuffer_t> originalBuffers;
    originalBuffers.SetNum(buffers.Num());
    for (int i = 0; i < buffers.Num(); i++) {
        originalBuffers[i].numSamples = buffers[i].numSamples;
        originalBuffers[i].bufferSize = buffers[i].bufferSize;
        originalBuffers[i].buffer = AllocBuffer(buffers[i].bufferSize, "backup");
        memcpy(originalBuffers[i].buffer, buffers[i].buffer, buffers[i].bufferSize);
    }
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Backed up: %d buffers\n", originalBuffers.Num());
    
    // Get base path from filesystem using RelativePathToOSPath
    const char* basePath = fileSystem->RelativePathToOSPath("", "fs_basepath");
    if (!basePath || strlen(basePath) == 0) {
        // Fallback to current directory
        char currentDir[MAX_OSPATH];
        memset(currentDir, 0, sizeof(currentDir));
        if (_getcwd(currentDir, sizeof(currentDir) - 1) == NULL) {
            common->Printf("ERROR: ConvertPCMToXMA2 - Failed to get base path\n");
            return false;
        }
        basePath = currentDir;
    }
    
    // Create the proper directory structure for the XMA file
    idStr relativePath = originalFilename;
    relativePath.StripFileExtension(); // Remove .wav extension
    
    // Create full paths for both temp WAV and final XMA
    idStrStatic< MAX_OSPATH > tempWavFile;
    idStrStatic< MAX_OSPATH > xmaFile;
    
    // Temp WAV in base directory (for conversion)
    idStr tempBaseName = relativePath;
    tempBaseName.Replace( "/", "_" );
    tempBaseName.Replace( "\\", "_" );
    tempBaseName.Replace( ":", "_" );
    tempWavFile.Format("%s\\temp_%s.wav", basePath, tempBaseName.c_str());
    
    // Final XMA in proper directory structure
    xmaFile.Format("%s\\%s.xma", basePath, relativePath.c_str());
    
    // Create directory structure for XMA file
    idStr xmaDir = xmaFile;
    xmaDir.StripFilename();
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Base path: '%s'\n", basePath);
    common->Printf("DEBUG: ConvertPCMToXMA2 - Temp WAV: '%s'\n", tempWavFile.c_str());
    common->Printf("DEBUG: ConvertPCMToXMA2 - Final XMA: '%s'\n", xmaFile.c_str());
    
    // Create directory structure if it doesn't exist
    fileSystem->CreateOSPath(xmaDir);
    
    // First, save current PCM as temporary WAV file
    if (!SavePCMAsWav( tempWavFile )) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Failed to save temp WAV file\n");
        return false;
    }
    
    // Check if the WAV file was actually created and has content
    FILE* testWav = fopen(tempWavFile.c_str(), "rb");
    if (!testWav) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Temp WAV file was not created\n");
        return false;
    }
    
    fseek(testWav, 0, SEEK_END);
    long wavSize = ftell(testWav);
    fclose(testWav);
    testWav = NULL;
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Temp WAV file size: %ld bytes\n", wavSize);
    
    if (wavSize < 44) { // Minimum WAV header size
        common->Printf("ERROR: ConvertPCMToXMA2 - Temp WAV file too small (corrupted)\n");
        remove(tempWavFile.c_str());
        return false;
    }
    
    // Try to find xma2encoder.exe in various locations
    idStrStatic< MAX_OSPATH > encoderPath;
    encoderPath = "C:\\Program Files (x86)\\Microsoft Xbox 360 SDK\\bin\\win32\\xma2encode.exe";
    FILE* testEncoder = fopen(encoderPath.c_str(), "rb");
    if (!testEncoder) {
        encoderPath = "tools\\xma2encode.exe";
        testEncoder = fopen(encoderPath.c_str(), "rb");
        if (!testEncoder) {
            encoderPath = "xma2encode.exe";
        } else {
            fclose(testEncoder);
        }
    } else {
        fclose(testEncoder);
    }
    
    // Execute xma2encoder.exe
    idStrStatic< 1024 > commandLine;
    commandLine.Format("cmd /c \"\"%s\" \"%s\" /quality 60 /BlockSize 2 /FilterHighFrequencies\"", 
        encoderPath.c_str(), tempWavFile.c_str());
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Executing: %s\n", commandLine.c_str());
    
    int result = system(commandLine.c_str());
    common->Printf("DEBUG: ConvertPCMToXMA2 - system() result: %d\n", result);
    
    if (result != 0) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Encoder failed with result: %d\n", result);
        remove(tempWavFile.c_str());
        return false;
    }
    
    // The encoder creates the XMA file next to the WAV file with same name
    idStrStatic< MAX_OSPATH > tempXmaFile = tempWavFile;
    tempXmaFile.SetFileExtension("xma");
    
    // Check if temp XMA file was generated
    FILE* testXma = fopen(tempXmaFile.c_str(), "rb");
    if (!testXma) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Temp XMA output file was not generated: '%s'\n", tempXmaFile.c_str());
        remove(tempWavFile.c_str());
        return false;
    }
    fseek(testXma, 0, SEEK_END);
    long xmaSize = ftell(testXma);
    fclose(testXma);
    testXma = NULL;
    
    common->Printf("DEBUG: ConvertPCMToXMA2 - Temp XMA file generated: '%s', size: %ld bytes\n", tempXmaFile.c_str(), xmaSize);
    
    if (xmaSize < 44) { // Minimum for a valid XMA file
        common->Printf("ERROR: ConvertPCMToXMA2 - XMA output file too small: %ld bytes\n", xmaSize);
        remove(tempWavFile.c_str());
        remove(tempXmaFile.c_str());
        return false;
    }
    
    // Move the temp XMA file to the proper location
    common->Printf("DEBUG: ConvertPCMToXMA2 - Moving XMA from '%s' to '%s'\n", tempXmaFile.c_str(), xmaFile.c_str());
    
    // Copy temp XMA to final location
    FILE* srcFile = fopen(tempXmaFile.c_str(), "rb");
    FILE* dstFile = fopen(xmaFile.c_str(), "wb");
    
    if (!srcFile || !dstFile) {
        common->Printf("ERROR: ConvertPCMToXMA2 - Failed to open files for moving XMA\n");
        if (srcFile) fclose(srcFile);
        if (dstFile) fclose(dstFile);
        remove(tempWavFile.c_str());
        remove(tempXmaFile.c_str());
        return false;
    }
    
    // Copy file content
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), srcFile)) > 0) {
        fwrite(buffer, 1, bytes, dstFile);
    }
    
    fclose(srcFile);
    fclose(dstFile);
    
    // Clean up temporary files
    remove(tempWavFile.c_str());
    remove(tempXmaFile.c_str());
    
    // CRITICAL PART: Load the XMA2 file properly
    common->Printf("DEBUG: ConvertPCMToXMA2 - Loading converted XMA2 file (this will clear existing data first)\n");
    
    bool loadSuccess = LoadXMAFile( xmaFile );
    
    if (loadSuccess) {
        common->Printf("DEBUG: ConvertPCMToXMA2 - XMA2 file loaded, now restoring original play values\n");
        
        // CRITICAL FIX: Restore the original playBegin and playLength values
        // The XMA2 file has these as 0, but we need the original values for proper playback
        playBegin = originalPlayBegin;
        playLength = originalPlayLength;
        
        common->Printf("DEBUG: ConvertPCMToXMA2 - Restored play values:\n");
        common->Printf("  PlayBegin: %u samples (was %u from XMA2)\n", playBegin, format.extra.xma2.loopBegin);
        common->Printf("  PlayLength: %u samples (was %u from XMA2)\n", playLength, format.extra.xma2.loopLength);
        
        // If we still don't have a playLength, calculate it from total samples
        if (playLength == 0) {
            uint32 totalSamples = 0;
            for (int i = 0; i < buffers.Num(); i++) {
                totalSamples += buffers[i].numSamples;
            }
            playLength = totalSamples - playBegin;
            common->Printf("DEBUG: ConvertPCMToXMA2 - Calculated PlayLength: %u (total: %u, begin: %u)\n", 
                playLength, totalSamples, playBegin);
        }
        
        common->Printf("DEBUG: ConvertPCMToXMA2 - Conversion successful with correct play values:\n");
        common->Printf("  Final PlayBegin: %u samples\n", playBegin);
        common->Printf("  Final PlayLength: %u samples\n", playLength);
        common->Printf("  Total buffers: %d\n", buffers.Num());
        common->Printf("  Total buffer size: %u bytes\n", totalBufferSize);
        
        // Verify we have all the data
        for (int i = 0; i < buffers.Num(); i++) {
            common->Printf("DEBUG: ConvertPCMToXMA2 - Final Buffer[%d]: size=%u, samples=%u\n", 
                i, buffers[i].bufferSize, buffers[i].numSamples);
        }
        
        // Free the backup data since conversion was successful
        for (int i = 0; i < originalBuffers.Num(); i++) {
            FreeBuffer(originalBuffers[i].buffer);
        }
        
    } else {
        common->Printf("ERROR: ConvertPCMToXMA2 - Failed to load converted XMA file from '%s'\n", xmaFile.c_str());
        
        // Restore original data if conversion failed
        common->Printf("DEBUG: ConvertPCMToXMA2 - Restoring original data\n");
        
        // Clear any partial data from failed load
        FreeData();
        
        // Restore original data
        buffers = originalBuffers;
        totalBufferSize = originalTotalBufferSize;
        playLength = originalPlayLength;
        playBegin = originalPlayBegin;
        format = originalFormat;
        loaded = originalLoaded;
    }
    
    return loadSuccess;
}

/*
========================
ConvertToXMA2Format - Convert sample to XMA2 format
========================
*/
void idSoundSample_XAudio2::ConvertToXMA2Format( const idStr& originalFilename ) {
    common->Printf("DEBUG: ConvertToXMA2Format - Converting sample to XMA2\n");
    
    if (!ConvertPCMToXMA2( originalFilename )) {
        common->Printf("ERROR: ConvertToXMA2Format - XMA2 conversion failed\n");
    }
}

/*
========================
idSoundSample_XAudio2::idSoundSample_XAudio2
========================
*/
idSoundSample_XAudio2::idSoundSample_XAudio2() {
    timestamp = FILE_NOT_FOUND_TIMESTAMP;
    loaded = false;
    neverPurge = false;
    levelLoadReferenced = false;

    memset( &format, 0, sizeof( format ) );

    totalBufferSize = 0;

    playBegin = 384;
    playLength = 0;

    lastPlayedTime = 0;
}

/*
========================
idSoundSample_XAudio2::~idSoundSample_XAudio2
========================
*/
idSoundSample_XAudio2::~idSoundSample_XAudio2() {
    FreeData();
}

/*
========================
idSoundSample_XAudio2::WriteGeneratedSample
========================
*/
void idSoundSample_XAudio2::WriteGeneratedSample( idFile *fileOut ) {
    common->Printf("DEBUG: WriteGeneratedSample - Writing generated sample with %d buffers\n", buffers.Num());
    common->Printf("DEBUG: WriteGeneratedSample - Magic: 0x%08X, Timestamp: %u, Loaded: %s\n", 
        SOUND_MAGIC_IDMSA, timestamp, loaded ? "true" : "false");
    common->Printf("DEBUG: WriteGeneratedSample - Original PlayBegin: %u, PlayLength: %u, TotalBufferSize: %u\n", 
        playBegin, playLength, totalBufferSize);
    
    fileOut->WriteBig( SOUND_MAGIC_IDMSA );
    fileOut->WriteBig( timestamp );
    fileOut->WriteBig( loaded );
    fileOut->WriteBig( (uint32)384 );  // FORCE playBegin to always be 384
    fileOut->WriteBig( playLength );
    
    common->Printf("DEBUG: WriteGeneratedSample - Forced PlayBegin to 384 in file\n");
    
    idWaveFile::WriteWaveFormatDirect( format, fileOut );
    fileOut->WriteBig( ( int )amplitude.Num() );
    fileOut->Write( amplitude.Ptr(), amplitude.Num() );
    fileOut->WriteBig( totalBufferSize );
    fileOut->WriteBig( ( int )buffers.Num() );
    
    for ( int i = 0; i < buffers.Num(); i++ ) {
        common->Printf("DEBUG: WriteGeneratedSample - Buffer[%d]: NumSamples=%u, Size=%u\n", 
            i, buffers[i].numSamples, buffers[i].bufferSize);
        fileOut->WriteBig( buffers[ i ].numSamples );
        fileOut->WriteBig( buffers[ i ].bufferSize );
        fileOut->Write( buffers[ i ].buffer, buffers[ i ].bufferSize );
    }
    
    // CUSTOM OFFSETS: Write specific data at exact byte positions
    common->Printf("DEBUG: WriteGeneratedSample - Writing custom data at specific offsets\n");
    
    // Get current position
    int currentPos = fileOut->Tell();
    common->Printf("DEBUG: WriteGeneratedSample - Current file position: 0x%X (%d)\n", currentPos, currentPos);
    
    // Write playBegin at offset 0x3D-0x40 (4 bytes, little-endian)
    fileOut->Seek( 0x3D, FS_SEEK_SET );
    uint32 playBeginLE = 384;
    // Convert to little-endian (swap bytes)
    fileOut->Write( &playBeginLE, 4 );
    common->Printf("DEBUG: WriteGeneratedSample - Written playBegin=%u (0x%08X) at offset 0x3D-0x40 (little-endian)\n", 
        playBegin, playBeginLE);
    
    // Write playLength at offset 0x41-0x44 (4 bytes, little-endian)
    fileOut->Seek( 0x41, FS_SEEK_SET );
    uint32 playLengthLE = playLength;
    // Convert to little-endian (swap bytes)
    fileOut->Write( &playLengthLE, 4 );
    common->Printf("DEBUG: WriteGeneratedSample - Written playLength=%u (0x%08X) at offset 0x41-0x44 (little-endian)\n", 
        playLength, playLengthLE);
    
    // Write 0xFF at offset 0x45 (1 byte)
    fileOut->Seek( 0x45, FS_SEEK_SET );
    uint8 markerByte = 0xFF;
    fileOut->Write( &markerByte, 1 );
    common->Printf("DEBUG: WriteGeneratedSample - Written marker byte 0xFF at offset 0x45\n");
    
    // Restore file position to end
    fileOut->Seek( currentPos, FS_SEEK_SET );
    
    common->Printf("DEBUG: WriteGeneratedSample - Successfully written generated sample with custom offsets\n");
    common->Printf("  - PlayBegin %u at offset 0x3D (little-endian)\n", playBegin);
    common->Printf("  - PlayLength %u at offset 0x41 (little-endian)\n", playLength);
    common->Printf("  - Marker 0xFF at offset 0x45\n");
}

/*
========================
idSoundSample_XAudio2::WriteAllSamples
========================
*/
void idSoundSample_XAudio2::WriteAllSamples( const idStr &sampleName ) {
    common->Printf("DEBUG: WriteAllSamples - Starting for sample '%s'\n", sampleName.c_str());
    
    // ========== PC VERSION (.idwav) ==========
    idSoundSample_XAudio2 * samplePC = new idSoundSample_XAudio2();
    {
        idStrStatic< MAX_OSPATH > inName = sampleName;
        inName.Append( ".msadpcm" );
        idStrStatic< MAX_OSPATH > inName2 = sampleName;
        inName2.Append( ".wav" );

        idStrStatic< MAX_OSPATH > outName = "generated/";
        outName.Append( sampleName );
        outName.Append( ".idwav" );

        common->Printf("DEBUG: WriteAllSamples PC - Input: '%s' or '%s'\n", inName.c_str(), inName2.c_str());
        common->Printf("DEBUG: WriteAllSamples PC - Output: '%s'\n", outName.c_str());

        bool loaded1 = samplePC->LoadWav( inName );
        bool loaded2 = false;
        if (!loaded1) {
            loaded2 = samplePC->LoadWav( inName2 );
        }
        
        if ( loaded1 || loaded2 ) {
            common->Printf("DEBUG: WriteAllSamples PC - Creating PC output file\n");
            idFile *fileOut = fileSystem->OpenFileWrite( outName, "fs_basepath" );
            if (fileOut) {
                common->Printf("DEBUG: WriteAllSamples PC - PC file opened successfully\n");
                samplePC->WriteGeneratedSample( fileOut );
                delete fileOut;
                common->Printf("DEBUG: WriteAllSamples PC - PC sample written successfully\n");
            } else {
                common->Printf("ERROR: WriteAllSamples PC - Failed to open PC output file '%s'\n", outName.c_str());
            }
        } else {
            common->Printf("ERROR: WriteAllSamples PC - Failed to load PC input files\n");
        }
    }
    delete samplePC;

    // ========== XBOX VERSION (.idxma) ==========
    idSoundSample_XAudio2 * sampleXBOX = new idSoundSample_XAudio2();
    {
        idStrStatic< MAX_OSPATH > inName = sampleName;
        inName.Append( ".xma" );  // Xbox format
        idStrStatic< MAX_OSPATH > inName2 = sampleName;
        inName2.Append( ".wav" ); // Fallback 

        idStrStatic< MAX_OSPATH > outName = "generated/";
        outName.Append( sampleName );
        outName.Append( ".idxma" );  // Compiled Xbox format

        common->Printf("DEBUG: WriteAllSamples XBOX - Input: '%s' or '%s'\n", inName.c_str(), inName2.c_str());
        common->Printf("DEBUG: WriteAllSamples XBOX - Output: '%s'\n", outName.c_str());

        bool loaded1 = sampleXBOX->LoadWav( inName );
        bool loaded2 = false;
        idStrStatic< MAX_OSPATH > sourceFile = inName;
        
        if (!loaded1) {
            loaded2 = sampleXBOX->LoadWav( inName2 );
            sourceFile = inName2;
            
            // If loaded WAV, need to convert to XMA2
            if (loaded2) {
                common->Printf("DEBUG: WriteAllSamples XBOX - Converting WAV to XMA2 format\n");
                sampleXBOX->ConvertToXMA2Format( sourceFile );
            }
        }
        
        if ( loaded1 || loaded2 ) {
            common->Printf("DEBUG: WriteAllSamples XBOX - Creating Xbox output file\n");
            idFile *fileOut = fileSystem->OpenFileWrite( outName, "fs_basepath" );
            if (fileOut) {
                common->Printf("DEBUG: WriteAllSamples XBOX - Xbox file opened successfully\n");
                sampleXBOX->WriteGeneratedSample( fileOut );
                delete fileOut;
                common->Printf("DEBUG: WriteAllSamples XBOX - Xbox sample written successfully\n");
            } else {
                common->Printf("ERROR: WriteAllSamples XBOX - Failed to open Xbox output file '%s'\n", outName.c_str());
            }
        } else {
            common->Printf("ERROR: WriteAllSamples XBOX - Failed to load Xbox input files\n");
        }
    }
    delete sampleXBOX;
    
    common->Printf("DEBUG: WriteAllSamples - Completed for sample '%s'\n", sampleName.c_str());
}

/*
========================
idSoundSample_XAudio2::LoadGeneratedSample
========================
*/
bool idSoundSample_XAudio2::LoadGeneratedSample( const idStr &filename ) {
    common->Printf("DEBUG: LoadGeneratedSample - Attempting to load '%s'\n", filename.c_str());
    
    idFileLocal fileIn( fileSystem->OpenFileReadMemory( filename ) );
    if ( fileIn != NULL ) {
        common->Printf("DEBUG: LoadGeneratedSample - File opened successfully\n");
        
        uint32 magic;
        fileIn->ReadBig( magic );
        common->Printf("DEBUG: LoadGeneratedSample - Magic read: 0x%08X (expected: 0x%08X)\n", magic, SOUND_MAGIC_IDMSA);
        
        if (magic != SOUND_MAGIC_IDMSA) {
            common->Printf("ERROR: LoadGeneratedSample - Invalid magic number in file '%s'\n", filename.c_str());
            return false;
        }
        
        fileIn->ReadBig( timestamp );
        fileIn->ReadBig( loaded );
        fileIn->ReadBig( playBegin );
        fileIn->ReadBig( playLength );
        common->Printf("DEBUG: LoadGeneratedSample - Timestamp: %u, Loaded: %s, PlayBegin: %u, PlayLength: %u\n", 
            timestamp, loaded ? "true" : "false", playBegin, playLength);
        
        idWaveFile::ReadWaveFormatDirect( format, fileIn );
        int num;
        fileIn->ReadBig( num );
        amplitude.Clear();
        amplitude.SetNum( num );
        fileIn->Read( amplitude.Ptr(), amplitude.Num() );
        common->Printf("DEBUG: LoadGeneratedSample - Amplitude data: %d entries\n", num);
        
        fileIn->ReadBig( totalBufferSize );
        fileIn->ReadBig( num );
        common->Printf("DEBUG: LoadGeneratedSample - TotalBufferSize: %u, Number of buffers: %d\n", totalBufferSize, num);
        
        buffers.SetNum( num );
        for ( int i = 0; i < num; i++ ) {
            fileIn->ReadBig( buffers[ i ].numSamples );
            fileIn->ReadBig( buffers[ i ].bufferSize );
            common->Printf("DEBUG: LoadGeneratedSample - Buffer[%d]: NumSamples=%u, Size=%u\n", 
                i, buffers[i].numSamples, buffers[i].bufferSize);
            buffers[ i ].buffer = AllocBuffer( buffers[ i ].bufferSize, GetName() );
            fileIn->Read( buffers[ i ].buffer, buffers[ i ].bufferSize );
            buffers[ i ].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[ i ].buffer );
        }
        common->Printf("DEBUG: LoadGeneratedSample - Successfully loaded generated sample '%s'\n", filename.c_str());
        return true;
    } else {
        common->Printf("DEBUG: LoadGeneratedSample - Failed to open file '%s'\n", filename.c_str());
    }
    return false;
}

/*
========================
idSoundSample_XAudio2::LoadResource
========================
*/
void idSoundSample_XAudio2::LoadResource() {
    common->Printf("DEBUG: LoadResource - Starting for sample '%s'\n", GetName());
    
    FreeData();

    if ( idStr::Icmpn( GetName(), "_default", 8 ) == 0 ) {
        common->Printf("DEBUG: LoadResource - Creating default sample for '%s'\n", GetName());
        MakeDefault();
        return;
    }

    if ( s_noSound.GetBool() ) {
        common->Printf("DEBUG: LoadResource - s_noSound is true, creating default sample for '%s'\n", GetName());
        MakeDefault();
        return;
    }

    loaded = false;

    for ( int i = 0; i < 2; i++ ) {
        common->Printf("DEBUG: LoadResource - Iteration %d for sample '%s'\n", i, GetName());
        
        idStrStatic< MAX_OSPATH > sampleName = GetName();
        if ( ( i == 0 ) && !sampleName.Replace( "/vo/", va( "/vo/%s/", sys_lang.GetString() ) ) ) {
            common->Printf("DEBUG: LoadResource - No voice localization needed, skipping to i=1\n");
            i++;
        }
        idStrStatic< MAX_OSPATH > generatedName = "generated/";
        generatedName.Append( sampleName );

        {
            if ( s_useCompression.GetBool() ) {
                common->Printf("DEBUG: LoadResource - Using compression, looking for .msadpcm\n");
                sampleName.Append( ".msadpcm" );
            } else {
                common->Printf("DEBUG: LoadResource - Not using compression, looking for .wav\n");
                sampleName.Append( ".wav" );
            }
            generatedName.Append( ".idwav" );
        }
        
        common->Printf("DEBUG: LoadResource - Trying generated file: '%s'\n", generatedName.c_str());
        common->Printf("DEBUG: LoadResource - Trying source file: '%s'\n", sampleName.c_str());
        
        loaded = LoadGeneratedSample( generatedName ) || LoadWav( sampleName );

        if ( !loaded && s_useCompression.GetBool() ) {
            common->Printf("DEBUG: LoadResource - Compression failed, trying .wav fallback\n");
            sampleName.SetFileExtension( "wav" );
            common->Printf("DEBUG: LoadResource - Fallback file: '%s'\n", sampleName.c_str());
            loaded = LoadWav( sampleName );
        }

        if ( loaded ) {
            common->Printf("DEBUG: LoadResource - Successfully loaded sample '%s'\n", GetName());
            
            bool buildResources = cvarSystem->GetCVarBool( "fs_buildresources" );
            common->Printf("DEBUG: LoadResource - fs_buildresources = %s\n", buildResources ? "true" : "false");
            
            if ( buildResources ) {
                common->Printf("DEBUG: LoadResource - Building resources for '%s'\n", GetName());
                fileSystem->AddSamplePreload( GetName() );
                WriteAllSamples( GetName() );

                if ( sampleName.Find( "/vo/" ) >= 0 ) {
                    common->Printf("DEBUG: LoadResource - Processing voice localization for '%s'\n", GetName());
                    for ( int j = 0; j < Sys_NumLangs(); j++ ) {
                        const char * lang = Sys_Lang( j );
                        if ( idStr::Icmp( lang, ID_LANG_ENGLISH ) == 0 ) {
                            common->Printf("DEBUG: LoadResource - Skipping English localization\n");
                            continue;
                        }
                        idStrStatic< MAX_OSPATH > locName = GetName();
                        locName.Replace( "/vo/", va( "/vo/%s/", Sys_Lang( j ) ) );
                        common->Printf("DEBUG: LoadResource - Processing localization: '%s'\n", locName.c_str());
                        WriteAllSamples( locName );
                    }
                }
            } else {
                common->Printf("DEBUG: LoadResource - fs_buildresources is false, not generating idwav files\n");
            }
            return;
        } else {
            common->Printf("DEBUG: LoadResource - Failed to load sample on iteration %d\n", i);
        }
    }

    if ( !loaded ) {
        common->Printf("ERROR: LoadResource - All loading attempts failed for '%s', using default\n", GetName());
        // make it default if everything else fails
        MakeDefault();
    }
    return;
}

/*
========================
LoadWav - WITH DETAILED DEBUG PRINTS - FIXED XMA2 playLength
========================
*/
bool idSoundSample_XAudio2::LoadWav( const idStr & filename ) {
    common->Printf("=== DEBUG: LoadWav - Starting load for '%s' ===\n", filename.c_str());

    // load the wave
    idWaveFile wave;
    if ( !wave.Open( filename ) ) {
        common->Printf("ERROR: LoadWav - Failed to open wave file '%s'\n", filename.c_str());
        return false;
    }

    common->Printf("DEBUG: LoadWav - Wave file opened successfully\n");

    idStrStatic< MAX_OSPATH > sampleName = filename;
    sampleName.SetFileExtension( "amp" );
    LoadAmplitude( sampleName );

    const char * formatError = wave.ReadWaveFormat( format );
    if ( formatError != NULL ) {
        common->Printf("ERROR: LoadWav - Format error in '%s': %s\n", filename.c_str(), formatError);
        idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), formatError );
        MakeDefault();
        return false;
    }
    
    common->Printf("DEBUG: LoadWav - Format tag: %d, Channels: %d, SampleRate: %d, BitsPerSample: %d\n", 
        format.basic.formatTag, format.basic.numChannels, format.basic.samplesPerSec, format.basic.bitsPerSample);
    
    timestamp = wave.Timestamp();

    totalBufferSize = wave.SeekToChunk( 'data' );
    common->Printf("DEBUG: LoadWav - Total buffer size from 'data' chunk: %u bytes\n", totalBufferSize);

    if ( format.basic.formatTag == idWaveFile::FORMAT_PCM || format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE ) {
        common->Printf("DEBUG: LoadWav - Processing PCM/EXTENSIBLE format\n");

        if ( format.basic.bitsPerSample != 16 ) {
            common->Printf("ERROR: LoadWav - Not 16-bit PCM: %d bits per sample\n", format.basic.bitsPerSample);
            idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "Not a 16 bit PCM wav file" );
            MakeDefault();
            return false;
        }

        playBegin = 0;
        playLength = ( totalBufferSize ) / format.basic.blockSize;
        common->Printf("DEBUG: LoadWav - PCM PlayLength: %u samples (blockSize: %d)\n", playLength, format.basic.blockSize);

        buffers.SetNum( 1 );
        buffers[0].bufferSize = totalBufferSize;
        buffers[0].numSamples = playLength;
        buffers[0].buffer = AllocBuffer( totalBufferSize, GetName() );
        
        common->Printf("DEBUG: LoadWav - PCM Buffer[0] allocated: size=%u, samples=%u\n", 
            buffers[0].bufferSize, buffers[0].numSamples);

        wave.Read( buffers[0].buffer, totalBufferSize );

        if ( format.basic.bitsPerSample == 16 ) {
            idSwap::LittleArray( (short *)buffers[0].buffer, totalBufferSize / sizeof( short ) );
        }

        buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

    } else if ( format.basic.formatTag == idWaveFile::FORMAT_ADPCM ) {
        common->Printf("DEBUG: LoadWav - Processing ADPCM format\n");

        playBegin = 0;
        playLength = ( ( totalBufferSize / format.basic.blockSize ) * format.extra.adpcm.samplesPerBlock );
        common->Printf("DEBUG: LoadWav - ADPCM PlayLength: %u samples (samplesPerBlock: %d)\n", 
            playLength, format.extra.adpcm.samplesPerBlock);

        buffers.SetNum( 1 );
        buffers[0].bufferSize = totalBufferSize;
        buffers[0].numSamples = playLength;
        buffers[0].buffer  = AllocBuffer( totalBufferSize, GetName() );
        
        common->Printf("DEBUG: LoadWav - ADPCM Buffer[0] allocated: size=%u, samples=%u\n", 
            buffers[0].bufferSize, buffers[0].numSamples);
        
        wave.Read( buffers[0].buffer, totalBufferSize );

        buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

    } else if ( format.basic.formatTag == idWaveFile::FORMAT_XMA2 ) {
        common->Printf("=== DEBUG: LoadWav - Processing XMA2 format ===\n");
        common->Printf("DEBUG: LoadWav - XMA2 format.extra.xma2.blockCount: %d\n", format.extra.xma2.blockCount);
        common->Printf("DEBUG: LoadWav - XMA2 format.extra.xma2.bytesPerBlock: %d\n", format.extra.xma2.bytesPerBlock);
        common->Printf("DEBUG: LoadWav - XMA2 format.extra.xma2.loopBegin: %u\n", format.extra.xma2.loopBegin);
        common->Printf("DEBUG: LoadWav - XMA2 format.extra.xma2.loopLength: %u\n", format.extra.xma2.loopLength);

        if ( format.extra.xma2.blockCount == 0 ) {
            common->Printf("ERROR: LoadWav - XMA2 has no data blocks (blockCount=0)\n");
            idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "No data blocks in file" );
            MakeDefault();
            return false;
        }

        int bytesPerBlock = format.extra.xma2.bytesPerBlock;
        common->Printf("DEBUG: LoadWav - XMA2 BlockCount: %d, BytesPerBlock: %d\n", 
            format.extra.xma2.blockCount, bytesPerBlock);
        
        // Calculate expected values
        uint32 expectedBlocks = ALIGN( totalBufferSize, bytesPerBlock ) / bytesPerBlock;
        common->Printf("DEBUG: LoadWav - XMA2 Expected blocks: %u, Actual blocks: %d\n", 
            expectedBlocks, format.extra.xma2.blockCount);
        
        assert( format.extra.xma2.blockCount == ALIGN( totalBufferSize, bytesPerBlock ) / bytesPerBlock );
        assert( format.extra.xma2.blockCount * bytesPerBlock >= totalBufferSize );
        assert( format.extra.xma2.blockCount * bytesPerBlock < totalBufferSize + bytesPerBlock );

        common->Printf("DEBUG: LoadWav - XMA2 Creating %d buffers\n", format.extra.xma2.blockCount);
        buffers.SetNum( format.extra.xma2.blockCount );
        
        for ( int i = 0; i < buffers.Num(); i++ ) {
            if ( i == buffers.Num() - 1 ) {
                buffers[i].bufferSize = totalBufferSize - ( i * bytesPerBlock );
            } else {
                buffers[i].bufferSize = bytesPerBlock;
            }

            common->Printf("DEBUG: LoadWav - XMA2 Buffer[%d]: About to allocate size=%u\n", i, buffers[i].bufferSize);
            buffers[i].buffer = AllocBuffer( buffers[i].bufferSize, GetName() );
            
            common->Printf("DEBUG: LoadWav - XMA2 Buffer[%d]: Reading %u bytes from wave file\n", i, buffers[i].bufferSize);
            int bytesRead = wave.Read( buffers[i].buffer, buffers[i].bufferSize );
            common->Printf("DEBUG: LoadWav - XMA2 Buffer[%d]: Actually read %d bytes\n", i, bytesRead);
            
            buffers[i].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[i].buffer );
        }

        common->Printf("DEBUG: LoadWav - XMA2 Looking for 'seek' chunk\n");
        int seekTableSize = wave.SeekToChunk( 'seek' );
        common->Printf("DEBUG: LoadWav - XMA2 Seek table size: %d (expected: %d)\n", 
            seekTableSize, 4 * buffers.Num());
        
        if ( seekTableSize != 4 * buffers.Num() ) {
            common->Printf("ERROR: LoadWav - XMA2 Wrong seek table size: got %d, expected %d\n", 
                seekTableSize, 4 * buffers.Num());
            idLib::Warning( "LoadWav( %s ) : %s", filename.c_str(), "Wrong number of entries in seek table" );
            MakeDefault();
            return false;
        }

        common->Printf("DEBUG: LoadWav - XMA2 Reading sample counts from seek table\n");
        for ( int i = 0; i < buffers.Num(); i++ ) {
            wave.Read( &buffers[i].numSamples, sizeof( buffers[i].numSamples ) );
            idSwap::Big( buffers[i].numSamples );
            common->Printf("DEBUG: LoadWav - XMA2 Buffer[%d]: NumSamples=%u (from seek table)\n", i, buffers[i].numSamples);
        }

        playBegin = format.extra.xma2.loopBegin;
        playLength = format.extra.xma2.loopLength;
        common->Printf("DEBUG: LoadWav - XMA2 PlayBegin: %u, PlayLength: %u (from XMA2 header)\n", playBegin, playLength);

        // CRITICAL FIX: If playLength is 0, calculate it from the total samples in all buffers
        if ( playLength == 0 ) {
            common->Printf("DEBUG: LoadWav - XMA2 PlayLength is 0, calculating from buffer samples\n");
            uint32 totalSamples = 0;
            for ( int i = 0; i < buffers.Num(); i++ ) {
                totalSamples += buffers[i].numSamples;
            }
            playLength = totalSamples - playBegin;
            common->Printf("DEBUG: LoadWav - XMA2 Calculated PlayLength: %u (total samples: %u, playBegin: %u)\n", 
                playLength, totalSamples, playBegin);
        }

        if ( buffers[buffers.Num()-1].numSamples < playBegin + playLength ) {
            common->Printf("WARNING: LoadWav - XMA2 Adjusting play length from %u to %u\n", 
                playLength, buffers[buffers.Num()-1].numSamples - playBegin);
            // This shouldn't happen, but it's not fatal if it does
            playLength = buffers[buffers.Num()-1].numSamples - playBegin;
        } else {
            // Discard samples beyond playLength
            for ( int i = 0; i < buffers.Num(); i++ ) {
                if ( buffers[i].numSamples > playBegin + playLength ) {
                    common->Printf("DEBUG: LoadWav - XMA2 Trimming buffer[%d] from %u to %u samples\n", 
                        i, buffers[i].numSamples, playBegin + playLength);
                    buffers[i].numSamples = playBegin + playLength;
                    // Ideally, the following loop should always have 0 iterations because playBegin + playLength ends in the last block already
                    // But there is no guarantee for that, so to be safe, discard all buffers beyond this one
                    for ( int j = i + 1; j < buffers.Num(); j++ ) {
                        common->Printf("DEBUG: LoadWav - XMA2 Discarding buffer[%d]\n", j);
                        FreeBuffer( buffers[j].buffer );
                    }
                    buffers.SetNum( i + 1 );
                    break;
                }
            }
        }

    } else {
        common->Printf("ERROR: LoadWav - Unsupported wave format %d in '%s'\n", format.basic.formatTag, filename.c_str());
        idLib::Warning( "LoadWav( %s ) : Unsupported wave format %d", filename.c_str(), format.basic.formatTag );
        MakeDefault();
        return false;
    }

    wave.Close();

    if ( format.basic.formatTag == idWaveFile::FORMAT_EXTENSIBLE ) {
        common->Printf("DEBUG: LoadWav - Converting FORMAT_EXTENSIBLE to basic format %d\n", format.extra.extensible.subFormat.data1);
        // HACK: XAudio2 doesn't really support FORMAT_EXTENSIBLE so we convert it to a basic format after extracting the channel mask
        format.basic.formatTag = format.extra.extensible.subFormat.data1;
    }

    // sanity check... (only if playLength > 0)
    if ( playLength > 0 ) {
        assert( buffers[buffers.Num()-1].numSamples == playBegin + playLength );
    }

    common->Printf("=== DEBUG: LoadWav - FINAL SUMMARY for '%s' ===\n", filename.c_str());
    common->Printf("DEBUG: LoadWav - Format tag: %d\n", format.basic.formatTag);
    common->Printf("DEBUG: LoadWav - Total buffers: %d\n", buffers.Num());
    common->Printf("DEBUG: LoadWav - Total buffer size: %u\n", totalBufferSize);
    common->Printf("DEBUG: LoadWav - Play begin: %u, Play length: %u\n", playBegin, playLength);
    
    for (int i = 0; i < buffers.Num(); i++) {
        common->Printf("DEBUG: LoadWav - Final Buffer[%d]: size=%u, samples=%u\n", 
            i, buffers[i].bufferSize, buffers[i].numSamples);
    }
    common->Printf("=== DEBUG: LoadWav - END SUMMARY ===\n");

    return true;
}

/*
========================
idSoundSample_XAudio2::MakeDefault
========================
*/
void idSoundSample_XAudio2::MakeDefault() {
    common->Printf("DEBUG: MakeDefault - Creating default sample for '%s'\n", GetName());
    
    FreeData();

    static const int DEFAULT_NUM_SAMPLES = 256;

    timestamp = FILE_NOT_FOUND_TIMESTAMP;
    loaded = true;

    memset( &format, 0, sizeof( format ) );
    format.basic.formatTag = idWaveFile::FORMAT_PCM;
    format.basic.numChannels = 1;
    format.basic.bitsPerSample = 16;
    format.basic.samplesPerSec = XAUDIO2_MIN_SAMPLE_RATE;
    format.basic.blockSize = format.basic.numChannels * format.basic.bitsPerSample / 8;
    format.basic.avgBytesPerSec = format.basic.samplesPerSec * format.basic.blockSize;

    assert( format.basic.blockSize == 2 );

    totalBufferSize = DEFAULT_NUM_SAMPLES * 2;

    short * defaultBuffer = (short *)AllocBuffer( totalBufferSize, GetName() );
    for ( int i = 0; i < DEFAULT_NUM_SAMPLES; i += 2 ) {
        defaultBuffer[i + 0] = SHRT_MIN;
        defaultBuffer[i + 1] = SHRT_MAX;
    }

    buffers.SetNum( 1 );
    buffers[0].buffer = defaultBuffer;
    buffers[0].bufferSize = totalBufferSize;
    buffers[0].numSamples = DEFAULT_NUM_SAMPLES;
    buffers[0].buffer = GPU_CONVERT_CPU_TO_CPU_CACHED_READONLY_ADDRESS( buffers[0].buffer );

    playBegin = 0;
    playLength = DEFAULT_NUM_SAMPLES;
    
    common->Printf("DEBUG: MakeDefault - Default sample created: %d samples, %u bytes\n", 
        DEFAULT_NUM_SAMPLES, totalBufferSize);
}

/*
========================
idSoundSample_XAudio2::FreeData

Called before deleting the object and at the start of LoadResource()
========================
*/
void idSoundSample_XAudio2::FreeData() {
    common->Printf("DEBUG: FreeData - Freeing data for sample '%s' (%d buffers)\n", GetName(), buffers.Num());
    
    if ( buffers.Num() > 0 ) {
        soundSystemLocal.StopVoicesWithSample( (idSoundSample *)this );
        for ( int i = 0; i < buffers.Num(); i++ ) {
            FreeBuffer( buffers[i].buffer );
        }
        buffers.Clear();
    }
    amplitude.Clear();

    timestamp = FILE_NOT_FOUND_TIMESTAMP;
    memset( &format, 0, sizeof( format ) );
    loaded = false;
    totalBufferSize = 0;
    playBegin = 0;
    playLength = 0;
}

/*
========================
idSoundSample_XAudio2::LoadAmplitude
========================
*/
bool idSoundSample_XAudio2::LoadAmplitude( const idStr & name ) {
    common->Printf("DEBUG: LoadAmplitude - Attempting to load amplitude file '%s'\n", name.c_str());
    
    amplitude.Clear();
    idFileLocal f( fileSystem->OpenFileRead( name ) );
    if ( f == NULL ) {
        common->Printf("DEBUG: LoadAmplitude - Amplitude file '%s' not found\n", name.c_str());
        return false;
    }
    amplitude.SetNum( f->Length() );
    f->Read( amplitude.Ptr(), amplitude.Num() );
    common->Printf("DEBUG: LoadAmplitude - Loaded %d amplitude entries from '%s'\n", amplitude.Num(), name.c_str());
    return true;
}

/*
========================
idSoundSample_XAudio2::GetAmplitude
========================
*/
float idSoundSample_XAudio2::GetAmplitude( int timeMS ) const {
    if ( timeMS < 0 || timeMS > LengthInMsec() ) {
        return 0.0f;
    }
    if ( IsDefault() ) {
        return 1.0f;
    }
    int index = timeMS * 60 / 1000;
    if ( index < 0 || index >= amplitude.Num() ) {
        return 0.0f;
    }
    return (float)amplitude[index] / 255.0f;
}
