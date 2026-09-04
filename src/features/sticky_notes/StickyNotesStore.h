#pragma once

#include "StickyNotesConfig.h"

#if CROSSINK_ENABLE_STICKY_NOTES

#include <cstdint>

#include "StickyNoteProtocol.h"

namespace sticky_note {

class Store {
 public:
  static bool save(const Note& note);
  static bool load(uint16_t year, uint8_t month, uint8_t day, Note& note);
  static bool has(uint16_t year, uint8_t month, uint8_t day);

 private:
  static bool formatPath(char* output, size_t outputSize, uint16_t year, uint8_t month, uint8_t day,
                         const char* suffix = "");
};

}  // namespace sticky_note

#endif  // CROSSINK_ENABLE_STICKY_NOTES
