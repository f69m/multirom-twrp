

#include "blobunpack.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "multirom.h"
#include "twcommon.h"


#define ANDROID_MAGIC       "ANDROID!"
#define ANDROID_MAGIC_SIZE  8

#define SIGNATURE_SIZE        28
#define SIGNATURE_MAGIC       "-SIGNED-BY-SIGNBLOB-"
#define SIGNATURE_MAGIC_SIZE  20

#define HEADER_SIZE        32
#define HEADER_MAGIC       "MSM-RADIO-UPDATE"
#define HEADER_MAGIC_SIZE  16

#define PART_ENTRY_SIZE  16

#define BUFFER_SIZE  4096


bool blobUnPackBootImage(const char *bootBlob, const char *bootImg)
{
	bool result = false;

	int fdImg = -1;
	int fdBlob = open(bootBlob, O_RDONLY);
	if (fdBlob < 0) {
		LOGERR("Failed to open boot blob: %s\n", strerror(errno));
		return false;
	}

	char buf[BUFFER_SIZE];
	char *bufPtr;
	int32_t offset = 0;
	int32_t size = 0;

	// Read header
	if (read(fdBlob, buf, SIGNATURE_SIZE+HEADER_SIZE) != SIGNATURE_SIZE+HEADER_SIZE) {
		LOGERR("Failed to read boot blob header.\n");
		goto done;
	}
	bufPtr = buf;

	// Just rename Android boot images
	if (memcmp(bufPtr, ANDROID_MAGIC, ANDROID_MAGIC_SIZE) == 0) {
		snprintf(buf, BUFFER_SIZE, "cp \"%s\" \"%s\"", bootBlob, bootImg);
		result = (system(buf) == 0);
		if (!result)
			LOGERR("Failed to copy boot blob to boot image.\n");
		goto done;
	}

	// Skip signature, remember size
	int signatureSize;
	if (memcmp(bufPtr, SIGNATURE_MAGIC, SIGNATURE_MAGIC_SIZE) == 0)
		signatureSize = SIGNATURE_SIZE;
	else
		signatureSize = 0;
	bufPtr += signatureSize;

	// Check header magic
	if (memcmp(bufPtr, HEADER_MAGIC, HEADER_MAGIC_SIZE) != 0) {
		LOGERR("Invalid boot blob file.\n");
		goto done;
	}
	bufPtr += HEADER_MAGIC_SIZE;

	// Parse headerSkip version and size, read partition offset and count
	int32_t partOffset, partCount;
	bufPtr += 4;  // skip version
	bufPtr += 4;  // skip size
	memcpy(&partOffset, bufPtr, 4); bufPtr += 4;
	memcpy(&partCount, bufPtr, 4); bufPtr += 4;

	// Safety check, don't want to read beyond buffer
	if (partCount > 20) partCount = 20;

	// Add signature size to partition offset
	partOffset += signatureSize;

	// Skip to start of partiton list and read it
	lseek(fdBlob, partOffset, SEEK_SET);
	if (read(fdBlob, buf, partCount*PART_ENTRY_SIZE) != partCount*PART_ENTRY_SIZE) {
		LOGERR("Failed to read boot blob partition list.\n");
		goto done;
	}
	bufPtr = buf;

	// Look for boot image in partition list
	for (int i=0; i<partCount; ++i) {
		// We are only interested in the boot image 'LNX\0'
		if (memcmp(bufPtr, "LNX", 4) != 0) {
			bufPtr += PART_ENTRY_SIZE;
			continue;
		}
		bufPtr += 4;
		// Copy offset and size
		memcpy(&offset, bufPtr, 4); bufPtr += 4;
		memcpy(&size, bufPtr, 4); bufPtr += 4;
		break;
    }

	if (size > 0) {
		// Add signature size to offset
		offset += signatureSize;
		// Seek to start of boot image
		lseek(fdBlob, offset, SEEK_SET);
		// Open output image file
		fdImg = open(bootImg, O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (fdImg < 0) {
			LOGERR("Failed to open boot image: %s\n", strerror(errno));
			goto done;
		}

		// Copy image data
		while (size > 0) {
			int n = (size>BUFFER_SIZE)? BUFFER_SIZE: size;
			n = read(fdBlob, buf, n);
			if ((n < 0) || (n > size)) {
				LOGERR("Failed to read boot image from boot blob.\n");
				goto done;
			}
			if (n == 0) break;
			if (write(fdImg, buf, n) != n) {
				LOGERR("Failed to write boot image.\n");
				goto done;
			}
			size -= n;
		}
		result = (size == 0);
	}

done:

	// Close the files
	close(fdBlob);
	if ((fdImg >= 0) && (close(fdImg) < 0)) {
		LOGERR("Failed to flush boot image: %s\n", strerror(errno));
		result = false;
	}

	return result;
}

