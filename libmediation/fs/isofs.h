#ifndef LIBMEDIATION_ISOFS_H
#define LIBMEDIATION_ISOFS_H

#include <cstdint>
#include <string>
#include <vector>

// Files inside a Blu-ray disc image, read in place without mounting it. A path that runs through
// an image names a file on the disc: "D:/films/movie.iso/BDMV/STREAM/00001.m2ts". The image is a
// UDF file system (2.50 with a metadata partition on a Blu-ray, 1.02 on older images), parsed
// once and cached. Names inside the image are matched whatever their case, so "00001.m2ts"
// finds the 00001.M2TS a disc stores. Read only: nothing can be written into an image this way.
namespace isofs
{
// True when the path runs through an existing disc image file. Cheap for an ordinary path.
bool isInside(const std::string& path);

// An open file inside an image, or nullptr when the path names no file on the disc.
void* open(const std::string& path);
void close(void* handle);
// Reads at the handle's position and moves it. Returns the bytes read, 0 at the end, -1 on error.
int read(void* handle, void* buffer, uint32_t count);
int64_t size(void* handle);
// whence: 0 from the start, 1 from the current position, 2 from the end. Returns the new position.
int64_t seek(void* handle, int64_t offset, int whence);

bool exists(const std::string& path);
// -1 when the path names no file on the disc.
int64_t fileSize(const std::string& path);
// The files of a folder of the disc matching mask ('*' and '?'), as findFiles() lists them: the
// bare names, or dir + name when savePaths is set. recursive also lists the subfolders' files,
// always with their paths. false when the folder does not exist.
bool findFiles(const std::string& dir, const std::string& mask, std::vector<std::string>* fileList, bool savePaths,
               bool recursive);
}  // namespace isofs

#endif  // LIBMEDIATION_ISOFS_H
