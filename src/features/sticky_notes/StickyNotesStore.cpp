#include "StickyNotesStore.h"

#if CROSSINK_ENABLE_STICKY_NOTES

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace sticky_note {
namespace {
constexpr char LOG_TAG[] = "NCAL";
constexpr char STORE_ROOT[] = "/.crosspoint/calendar";
constexpr std::array<uint8_t, 4> FILE_MAGIC = {'C', 'A', 'L', 'S'};
constexpr uint8_t FILE_VERSION = 1;
constexpr size_t FILE_HEADER_BYTES = 12;
constexpr size_t PATH_BYTES = 48;

bool installTempFile(const char* path, const char* tempPath, const char* backupPath) {
  bool hadExisting = Storage.exists(path);
  if (!hadExisting && Storage.exists(backupPath)) {
    if (!Storage.rename(backupPath, path)) {
      LOG_ERR(LOG_TAG, "Failed to recover calendar note backup: %s", path);
      Storage.remove(tempPath);
      return false;
    }
    hadExisting = true;
  }
  if (Storage.exists(backupPath) && hadExisting) Storage.remove(backupPath);
  if (hadExisting && !Storage.rename(path, backupPath)) {
    LOG_ERR(LOG_TAG, "Failed to back up calendar note: %s", path);
    Storage.remove(tempPath);
    return false;
  }
  if (!Storage.rename(tempPath, path)) {
    LOG_ERR(LOG_TAG, "Failed to install calendar note: %s", path);
    if (hadExisting) Storage.rename(backupPath, path);
    Storage.remove(tempPath);
    return false;
  }
  if (Storage.exists(backupPath)) Storage.remove(backupPath);
  return true;
}
}  // namespace

bool Store::formatPath(char* output, const size_t outputSize, const uint16_t year, const uint8_t month,
                       const uint8_t day, const char* suffix) {
  if (!output || outputSize == 0 || !suffix || !validDate(year, month, day)) return false;
  const int length = snprintf(output, outputSize, "%s/%04u-%02u-%02u.bin%s", STORE_ROOT,
                              static_cast<unsigned>(year), static_cast<unsigned>(month), static_cast<unsigned>(day),
                              suffix);
  return length > 0 && static_cast<size_t>(length) < outputSize;
}

bool Store::save(const Note& note) {
  if (note.messageLength == 0 || note.messageLength > MAX_MESSAGE_BYTES ||
      !validDate(note.year, note.month, note.day) ||
      !validUtf8(reinterpret_cast<const uint8_t*>(note.message.data()), note.messageLength)) {
    LOG_ERR(LOG_TAG, "Refusing invalid calendar note");
    return false;
  }
  if (!Storage.exists(STORE_ROOT) && !Storage.mkdir(STORE_ROOT)) {
    LOG_ERR(LOG_TAG, "Failed to create calendar directory");
    return false;
  }

  char path[PATH_BYTES];
  char tempPath[PATH_BYTES];
  char backupPath[PATH_BYTES];
  if (!formatPath(path, sizeof(path), note.year, note.month, note.day) ||
      !formatPath(tempPath, sizeof(tempPath), note.year, note.month, note.day, ".tmp") ||
      !formatPath(backupPath, sizeof(backupPath), note.year, note.month, note.day, ".bak")) {
    LOG_ERR(LOG_TAG, "Failed to build calendar note path");
    return false;
  }
  if (Storage.exists(tempPath)) Storage.remove(tempPath);

  FsFile file = Storage.open(tempPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR(LOG_TAG, "Failed to open calendar temp file: %s", tempPath);
    return false;
  }
  std::array<uint8_t, FILE_HEADER_BYTES> header{};
  std::copy(FILE_MAGIC.begin(), FILE_MAGIC.end(), header.begin());
  header[4] = FILE_VERSION;
  writeU16Le(header.data() + 6, note.messageLength);
  writeU32Le(header.data() + 8,
             crc32(reinterpret_cast<const uint8_t*>(note.message.data()), note.messageLength));
  const bool wrote = file.write(header.data(), header.size()) == header.size() &&
                     file.write(reinterpret_cast<const uint8_t*>(note.message.data()), note.messageLength) ==
                         note.messageLength &&
                     file.sync();
  file.close();
  if (!wrote) {
    LOG_ERR(LOG_TAG, "Failed to write calendar note: %s", tempPath);
    Storage.remove(tempPath);
    return false;
  }
  if (!installTempFile(path, tempPath, backupPath)) return false;
  LOG_INF(LOG_TAG, "Saved calendar note %04u-%02u-%02u (%u bytes)", static_cast<unsigned>(note.year),
          static_cast<unsigned>(note.month), static_cast<unsigned>(note.day),
          static_cast<unsigned>(note.messageLength));
  return true;
}

bool Store::load(const uint16_t year, const uint8_t month, const uint8_t day, Note& note) {
  char path[PATH_BYTES];
  if (!formatPath(path, sizeof(path), year, month, day)) return false;
  FsFile file;
  if (!Storage.openFileForRead(LOG_TAG, path, file)) return false;

  std::array<uint8_t, FILE_HEADER_BYTES> header{};
  const bool headerRead = file.read(header.data(), header.size()) == static_cast<int>(header.size());
  const uint16_t messageLength = headerRead ? readU16Le(header.data() + 6) : 0;
  const bool validHeader = headerRead && std::equal(FILE_MAGIC.begin(), FILE_MAGIC.end(), header.begin()) &&
                           header[4] == FILE_VERSION && header[5] == 0 && messageLength > 0 &&
                           messageLength <= MAX_MESSAGE_BYTES && file.fileSize() == FILE_HEADER_BYTES + messageLength;
  if (!validHeader || file.read(note.message.data(), messageLength) != static_cast<int>(messageLength)) {
    file.close();
    LOG_ERR(LOG_TAG, "Invalid calendar note file: %s", path);
    return false;
  }
  file.close();

  const auto* text = reinterpret_cast<const uint8_t*>(note.message.data());
  if (crc32(text, messageLength) != readU32Le(header.data() + 8) || !validUtf8(text, messageLength)) {
    LOG_ERR(LOG_TAG, "Calendar note checksum/text failed: %s", path);
    return false;
  }
  note.sequence = 0;
  note.year = year;
  note.month = month;
  note.day = day;
  note.messageLength = messageLength;
  note.message[messageLength] = '\0';
  return true;
}

bool Store::has(const uint16_t year, const uint8_t month, const uint8_t day) {
  char path[PATH_BYTES];
  return formatPath(path, sizeof(path), year, month, day) && Storage.exists(path);
}

}  // namespace sticky_note

#endif  // CROSSINK_ENABLE_STICKY_NOTES
