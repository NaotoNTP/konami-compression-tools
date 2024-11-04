
/* ================================================================================= *
 * Konami's LZSS variant 1 (LZKN1) compressor/decompressor							 *
 * API implementation																 *
 *																					 *
 * (c) 2020, Vladikcomper															 *
 * ================================================================================= */

#include <stdlib.h>		// for "malloc"
#include <stdio.h>		// for "size_t", "printf" etc
#include <stdint.h>		// for "uint8_t" etc.

#include "lzkn.h"

/**
 * Compression function
 * 
 * Returns size of the compressed buffer
 */
lz_error nlzss_compress(const uint8_t *inBuff, const size_t inBuffSize, uint8_t *outBuff, size_t outBuffSize, size_t* compressedSize) {

	lz_error result = 0;					// default return value (success)

	const int32_t sizeWindow = 0x2000;		// sliding window (displacement) maximum size
	const int32_t sizeCopy = 0x100;			// maximum size of the bytes (string) to copy

	#define FLAG_COPY_MODE1		0
	#define FLAG_COPY_MODE2		1

	int32_t inBuffPos = 0;					// input buffer position
	int32_t inBuffLastCopyPos = 0;			// position of the last copied byte to the uncompressed stream
	int32_t outBuffPos = 0;					// output buffer position

	// Define basic helper macros
	#define MIN(a,b)	((a) < (b) ? (a) : (b))
	#define MAX(a,b)	((a) > (b) ? (a) : (b))

	// Description field helpers
	uint8_t * descFieldPtr = NULL;
	uint16_t descFieldVar = 0;
	int descFieldCurrentBit = 0;

	#define BYTE_FLAG	0
	#define BYTE_RAW	1

	#define PUSH_DESC_FIELD_BIT(BIT)	\
		if (descFieldPtr == NULL) { \
			descFieldPtr = outBuff + outBuffPos++; \
			*descFieldPtr = BIT; \
			descFieldCurrentBit = 1; \
		} \
		else { \
			*descFieldPtr |= (BIT << descFieldCurrentBit++); \
			if (descFieldCurrentBit >= 8) { \
				descFieldPtr = NULL; \
			} \
		}

	// Put uncompressed size ...
	outBuff[outBuffPos++] = inBuffSize >> 8;
	outBuff[outBuffPos++] = inBuffSize & 0xFF;

	int matchNearShort = 0;
	int matchNearLong = 0;
	int matchFarShort = 0;
	int matchFarLong = 0;

	int maxCopySize = 0;

	// Main compression loop ...
	while ((inBuffPos < inBuffSize) && (outBuffPos < outBuffSize)) {

		// Attempt to find the longest matching string in the input buffer ...
		int32_t matchStrPos = -1;
		int32_t matchStrSize = 0;
		const int32_t matchStrMaxCopy = MIN(sizeCopy, inBuffSize - inBuffPos);
		const int32_t matchWindowBoundary = MAX(inBuffPos - sizeWindow, 0);

		for (int32_t matchPos = inBuffPos - 1; matchPos >= matchWindowBoundary; --matchPos) {
			int32_t currentMatchSize = 0;

			while (inBuff[matchPos + currentMatchSize] == inBuff[inBuffPos + currentMatchSize]) {
				++currentMatchSize;

				if (currentMatchSize >= matchStrMaxCopy) {
					break;
				}
			}

			if (currentMatchSize > matchStrSize) {
				matchStrSize = currentMatchSize;
				matchStrPos = matchPos;
			}
		}

		int32_t matchStrDisp = inBuffPos - matchStrPos;	// matching string displacement

		// Now, decide on the compression mode ...
		int32_t queuedRawCopySize = inBuffPos - inBuffLastCopyPos;
		uint8_t suggestedMode = 0xFF;
		
		if ((matchStrSize >= 2) && (matchStrDisp <= 64)) {		// Uncompressed stream copy (Mode 2)
			suggestedMode = FLAG_COPY_MODE2;
		}
		else if (matchStrSize >= 3) {
			suggestedMode = FLAG_COPY_MODE1;
		}

		// Initiate raw bytes transfer until the current location in the following cases:
		//	-- If the copy mode was suggested, but there are raw bytes queued, render them first
		//	-- If the input buffer exhausted and should be flushed immidiately
		if (((suggestedMode != 0xFF) && (queuedRawCopySize >= 1)) || (inBuffPos + 1 == inBuffSize)) {
				for (int32_t i = 0; i < queuedRawCopySize; i += 1) {
					PUSH_DESC_FIELD_BIT(BYTE_RAW);
					outBuff[outBuffPos++] = inBuff[inBuffLastCopyPos++];
				}
		}

		// Now, render compression modes, if any was suggested ...
		if (suggestedMode == FLAG_COPY_MODE2) {
			PUSH_DESC_FIELD_BIT(BYTE_FLAG);
			PUSH_DESC_FIELD_BIT(FLAG_COPY_MODE2);

			if (matchStrSize <= 4) {
				outBuff[outBuffPos++] = (((matchStrDisp - 1) & 0x1F) << 2) | (matchStrSize - 1);
				matchNearShort++;
			}
			else {
				outBuff[outBuffPos++] = (((matchStrDisp - 1) & 0x1F) << 2);
				outBuff[outBuffPos++] = (matchStrSize - 1);
				matchNearLong++;
			}

			maxCopySize = (matchStrSize > maxCopySize) ? matchStrSize : maxCopySize;

		//	outBuff[outBuffPos++] = (FLAG_COPY_MODE2) | ((matchStrDisp - 1) & 0x7F);
		//	outBuff[outBuffPos++] = (matchStrSize - 4);
			inBuffPos += matchStrSize;
			inBuffLastCopyPos = inBuffPos;
		}
		else if (suggestedMode == FLAG_COPY_MODE1) {
			PUSH_DESC_FIELD_BIT(BYTE_FLAG);
			PUSH_DESC_FIELD_BIT(FLAG_COPY_MODE1);
			
			if (matchStrSize <= 8) {
				outBuff[outBuffPos++] = (((matchStrDisp - 1) & 0xF00) >> 5) | (matchStrSize - 1);
				outBuff[outBuffPos++] = ((matchStrDisp - 1) & 0xFF);
				matchFarShort++;
			} 
			else {
				outBuff[outBuffPos++] = (((matchStrDisp - 1) & 0xF00) >> 5);
				outBuff[outBuffPos++] = ((matchStrDisp - 1) & 0xFF);
				outBuff[outBuffPos++] = (matchStrSize - 1);
				matchFarLong++;
			}
		
			maxCopySize = (matchStrSize > maxCopySize) ? matchStrSize : maxCopySize;
		
		//	outBuff[outBuffPos++] = (FLAG_COPY_MODE1) | ((matchStrSize - 4) << 2) | (((matchStrDisp - 1) & 0x300) >> 8);
		//	outBuff[outBuffPos++] = ((matchStrDisp - 1) & 0xFF);

			inBuffPos += matchStrSize;
			inBuffLastCopyPos = inBuffPos;
		
		}
		else {
			inBuffPos += 1;
		}

	}

	// Detect buffer overflow errors
	if (inBuffPos > inBuffSize) {
		result |= LZ_INBUFF_OVERFLOW;
	}
	if (outBuffPos > outBuffSize) {
		result |= LZ_OUTBUFF_OVERFLOW;
	}

	// Finalize compression buffer
	PUSH_DESC_FIELD_BIT(BYTE_FLAG);
	outBuff[outBuffPos++] = 0x00;
	outBuff[outBuffPos++] = 0x00;

	// Return compressed data size
	*compressedSize = outBuffPos;

	printf("\nNearby Dictionary Matches (Short): %d", matchNearShort);
	printf("\nNearby Dictionary Matches (Long): %d", matchNearLong);
	printf("\nFar Dictionary Matches (Short): %d", matchFarShort);
	printf("\nFar Dictionary Matches (Long): %d", matchFarLong);

	printf("\n\nMax Copy Length: %d", maxCopySize);

	return result;

}



/**
 * Decompression function
 * 
 * Returns size of the decompressed buffer
 */
lz_error nlzss_decompress(uint8_t *inBuff, size_t inBuffSize, uint8_t** outBuffPtr, size_t *decompressedSize) {
	
	lz_error result = 0;

	int32_t inBuffPos = 0;
	int32_t outBuffPos = 0;

	#define FLAG_COPY_MODE1		0x00
	#define FLAG_COPY_MODE2		0x80
	#define FLAG_COPY_RAW		0xC0

	#define BYTE_FLAG	1
	#define BYTE_RAW	0

	// Get uncompressed buffer size and allocate the buffer
	size_t outBuffSize = (inBuff[inBuffPos] << 8) + inBuff[inBuffPos+1];
	uint8_t *outBuff = malloc(outBuffSize);
	inBuffPos += 2;

	if (!outBuff) {
		result |= LZ_ALLOC_FAILED;
		return result;
	}

	uint8_t done = 0;
	uint8_t descField;
	int8_t descFieldRemainingBits = 0;

	while (!done && (outBuffPos <= outBuffSize)) {

		// Fetch a new description field if necessary
		if (!descFieldRemainingBits--) {
			descField = inBuff[inBuffPos++];
			descFieldRemainingBits = 7;
		}

		// Get successive description field bit, rotate the field
		uint8_t bit = descField & 1;
		descField = descField >> 1;

		// If bit indicates a raw byte ("BYTE_RAW") in the stream, copy it over ...
		if (bit == BYTE_RAW) {
			outBuff[outBuffPos++] = inBuff[inBuffPos++];
		}

		// Otherwise, it indicates a flag ("BYTE_FLAG"), so decode it ...
		else {
			uint8_t flag = inBuff[inBuffPos++];

			if (flag == 0x1F) {
				done = 1;
			}
			else if (flag >= FLAG_COPY_RAW) {
				int32_t copySize = (int32_t)flag - (int32_t)FLAG_COPY_RAW + 8;

            	for (int32_t i = 0; i < copySize; ++i) {
					outBuff[outBuffPos++] = inBuff[inBuffPos++];
            	}
			}
			else if (flag >= FLAG_COPY_MODE2) {
				int32_t copyDisp = ((int32_t)flag & 0xF);
				int32_t copySize = ((int32_t)flag >> 4) - 6;

            	for (int32_t i = 0; i < copySize; ++i) {
					outBuff[outBuffPos] = outBuff[outBuffPos - copyDisp];
					outBuffPos++;
            	}
			}
			else {	// "FLAG_COPY_MODE1"
            	int32_t copyDisp = (inBuff[inBuffPos++]) | (((int32_t)flag << 3) & 0x300);
            	int32_t copySize = (flag & 0x1F) + 3;

            	for (int32_t i = 0; i < copySize; ++i) {
					outBuff[outBuffPos] = outBuff[outBuffPos - copyDisp];
					outBuffPos++;
            	}
			}
		}

	}

	// Detect buffer errors
	if (outBuffPos < outBuffSize) {
		result |= LZ_OUTBUFF_UNDERFLOW;
	}
	else if (outBuffPos > outBuffSize) {
		result |= LZ_OUTBUFF_OVERFLOW;
	}

	if (inBuffPos < inBuffSize) {
		result |= LZ_INBUFF_UNDERFLOW;
	}
	else if (inBuffPos > inBuffSize) {
		result |= LZ_INBUFF_OVERFLOW;
	}

	// Return pointer to the uncompressed buffer and its size
	*outBuffPtr = outBuff;
	*decompressedSize = outBuffSize;

	return result;
}
