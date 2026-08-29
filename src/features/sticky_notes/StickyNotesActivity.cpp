#include "StickyNotesActivity.h"

#if CROSSINK_ENABLE_STICKY_NOTES

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/ScreenshotUtil.h"

namespace fui = freeink::ui;

namespace {
constexpr const char* LOG_TAG = "NOTE";
constexpr const char* WEEKDAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char* MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void formatNoteDate(const sticky_note::Note& note, char* output, const size_t outputSize) {
  std::tm date{};
  date.tm_year = static_cast<int>(note.year) - 1900;
  date.tm_mon = static_cast<int>(note.month) - 1;
  date.tm_mday = note.day;
  date.tm_hour = 12;
  date.tm_isdst = -1;
  mktime(&date);
  const int weekday = std::clamp(date.tm_wday, 0, 6);
  snprintf(output, outputSize, "%s, %s %02u %04u", WEEKDAY_NAMES[weekday], MONTH_NAMES[note.month - 1],
           static_cast<unsigned>(note.day), static_cast<unsigned>(note.year));
}
}  // namespace

StickyNotesActivity::StickyNotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("StickyNotes", renderer, mappedInput),
      uiTarget_(makeUiTarget(renderer)),
      app_(uiTarget_, uiTarget_.deviceContext())
#ifndef SIMULATOR
      ,
      pendingMutex_(xSemaphoreCreateMutex())
#endif
{
}

StickyNotesActivity::~StickyNotesActivity() {
#ifndef SIMULATOR
  if (pendingMutex_) {
    vSemaphoreDelete(pendingMutex_);
    pendingMutex_ = nullptr;
  }
#endif
}

void StickyNotesActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  app_.setTheme(uiThemeTokens(uiTarget_));
  app_.on(ACTION_RECEIVE, &StickyNotesActivity::onRowEvent, this);
  app_.setScreen(&StickyNotesActivity::menuScreen, this);
  setState(State::Ready);
}

void StickyNotesActivity::onExit() {
  Activity::onExit();
  stopReceiving();
  if (radioUsed_) silentRestart();
}

void StickyNotesActivity::loop() {
  processPendingNote();

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitActivity();
    return;
  }

  if ((state_ == State::Ready || state_ == State::Error) &&
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    mappedInput.suppressNextConfirmRelease();
    startReceiving();
    return;
  }

  if ((state_ == State::Ready || state_ == State::Error) && uiReady_) {
    const fui::InputSnapshot snapshot = touchSnapshotFrom(mappedInput);
    if (snapshot.touchPressed || snapshot.touchReleased) {
      const auto event = app_.route(snapshot);
      if (app_.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  if (state_ == State::Listening && millis() - listeningStartedMs_ >= RECEIVE_TIMEOUT_MS) {
    stopReceiving();
    setError(StrId::STR_STICKY_NOTE_TIMEOUT);
  }
}

void StickyNotesActivity::render(RenderLock&&) {
  if (state_ == State::Saved || state_ == State::Applying) {
    drawNoteTemplate(state_ == State::Saved);
    renderer.displayBuffer(screenTransitionRefresh_.modeFor(static_cast<uint8_t>(state_)));
    return;
  }

  const char* status = tr(STR_STICKY_NOTE_READY);
  if (state_ == State::Listening) {
    status = tr(STR_STICKY_NOTE_LISTENING);
  } else if (state_ == State::Error) {
    status = I18n::getInstance().get(errorId_);
  }
  drawStatusScreen(status, state_ == State::Ready || state_ == State::Error);
  renderer.displayBuffer(screenTransitionRefresh_.modeFor(static_cast<uint8_t>(state_)));
}

void StickyNotesActivity::menuScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<StickyNotesActivity*>(user)->buildMenuScreen(screen);
}

void StickyNotesActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<StickyNotesActivity*>(user);
  if (event.value != 0 || (self->state_ != State::Ready && self->state_ != State::Error)) return;
  self->app_.clearTapFlash();
  self->startReceiving();
}

void StickyNotesActivity::buildMenuScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput) +
                                       metrics.verticalSpacing),
                  0, static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  fui::ListItem item;
  item.label = tr(STR_RECEIVE_STICKY_NOTE);
  item.actionValue = 0;
  fui::ListProps props;
  props.items = &item;
  props.count = 1;
  props.selectedIndex = 0;
  props.action = ACTION_RECEIVE;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  configureUiList(props, screen.theme(), screen.body());
  screen.list(props);
}

void StickyNotesActivity::startReceiving() {
#ifdef SIMULATOR
  setError(StrId::STR_STICKY_NOTE_SIMULATOR_UNAVAILABLE);
#else
  stopReceiving();
  sdFontSystem.releaseLoadedFont(renderer);
  radioUsed_ = true;
  if (!pendingMutex_) {
    setError(StrId::STR_STICKY_NOTE_RADIO_FAILED);
    return;
  }
  xSemaphoreTake(pendingMutex_, portMAX_DELAY);
  pending_ = false;
  xSemaphoreGive(pendingMutex_);
  if (!radio_.begin(ESPNOW_CHANNEL, &StickyNotesActivity::onReceive, this)) {
    setError(StrId::STR_STICKY_NOTE_RADIO_FAILED);
    return;
  }
  listeningStartedMs_ = millis();
  setState(State::Listening);
#endif
}

void StickyNotesActivity::stopReceiving() {
#ifndef SIMULATOR
  radio_.end();
#endif
}

void StickyNotesActivity::processPendingNote() {
#ifndef SIMULATOR
  if (!pendingMutex_) return;
  sticky_note::Note pendingNote;
  std::array<uint8_t, 6> sourceMac{};
  bool hasPending = false;
  xSemaphoreTake(pendingMutex_, portMAX_DELAY);
  if (pending_) {
    pendingNote = pendingNote_;
    sourceMac = pendingSourceMac_;
    pending_ = false;
    hasPending = true;
  }
  xSemaphoreGive(pendingMutex_);
  if (!hasPending) return;

  note_ = pendingNote;
  noteFontId_ = UI_12_FONT_ID;
  if (SETTINGS.stickyNoteSdFontFamilyName[0] != '\0') {
    const auto activation = sdFontSystem.activateDictionaryFont(renderer, SETTINGS.stickyNoteSdFontFamilyName,
                                                                SETTINGS.stickyNoteFontPointSize);
    if (activation.fontId != 0) noteFontId_ = activation.fontId;
    sdFontSystem.releaseRegistry();
  }
  setState(State::Applying);
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered || !saveNoteSleepImage() || !selectNoteSleepImage()) {
    setError(StrId::STR_STICKY_NOTE_SAVE_FAILED);
    return;
  }

  const bool ackQueued = sendAck(sourceMac.data(), note_.sequence);
  delay(30);
  stopReceiving();
  if (!ackQueued) LOG_ERR(LOG_TAG, "Note saved but ACK could not be queued");
  setState(State::Saved);
#endif
}

void StickyNotesActivity::setState(const State state) {
  state_ = state;
  requestUpdate();
}

void StickyNotesActivity::setError(const StrId errorId) {
  errorId_ = errorId;
  LOG_ERR(LOG_TAG, "%s", I18n::getInstance().get(errorId));
  stopReceiving();
  setState(State::Error);
}

void StickyNotesActivity::exitActivity() {
  mappedInput.suppressNextBackRelease();
  finish();
}

void StickyNotesActivity::drawStatusScreen(const char* status, const bool showReceiveAction) {
  renderer.clearScreen();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget_, header, tr(STR_STICKY_NOTES), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_STICKY_NOTES));
  }

  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int statusY = safeArea.y + safeArea.height / 3;
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, status, safeArea.width - 32, 3);
  int y = statusY;
  for (const auto& line : lines) {
    UITheme::drawCenteredText(renderer, safeArea, UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }

  uiReady_ = false;
  if (showReceiveAction) app_.render();
  uiReady_ = showReceiveAction;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), showReceiveAction ? tr(STR_SELECT) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void StickyNotesActivity::drawNoteTemplate(const bool showSavedStatus) {
  renderer.clearScreen();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, false, false);
  const int sideInset = std::max(12, safeArea.width / 24);
  const int left = safeArea.x + sideInset;
  const int right = safeArea.x + safeArea.width - sideInset - 1;
  const int titleY = safeArea.y + 16;
  const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  UITheme::drawCenteredText(renderer, safeArea, UI_12_FONT_ID, titleY, tr(STR_NOTES_TITLE), true, EpdFontFamily::BOLD);

  const int firstRuleY = titleY + titleLineHeight + 14;
  renderer.drawLine(left, firstRuleY, right, firstRuleY, 2, true);
  char dateLine[40];
  formatNoteDate(note_, dateLine, sizeof(dateLine));
  const int dateY = firstRuleY + 16;
  UITheme::drawCenteredText(renderer, safeArea, noteFontId_, dateY, dateLine);

  const int secondRuleY = dateY + renderer.getLineHeight(noteFontId_) + 14;
  renderer.drawLine(left, secondRuleY, right, secondRuleY, 2, true);
  const int footerReserve = showSavedStatus ? renderer.getLineHeight(SMALL_FONT_ID) + 20 : 0;
  const int messageTop = secondRuleY + 22;
  const int messageBottom = safeArea.y + safeArea.height - footerReserve;
  const int lineHeight = renderer.getLineHeight(noteFontId_) + 5;

  const int textLeft = left;
  const int textWidth = std::max(1, right - textLeft + 1);
  int textY = messageTop;
  char* row = note_.message.data();

  while (row && *row && textY + lineHeight <= messageBottom) {
    char* newline = strchr(row, '\n');
    if (newline) *newline = '\0';
    const int remainingLines = std::max(1, (messageBottom - textY) / lineHeight);
    const auto lines = renderer.wrappedText(noteFontId_, row, textWidth, remainingLines);
    for (const auto& line : lines) {
      if (textY + lineHeight > messageBottom) break;
      renderer.drawText(noteFontId_, textLeft, textY, line.c_str());
      textY += lineHeight;
    }

    if (newline) {
      *newline = '\n';
      row = newline + 1;
      textY += 4;
    } else {
      break;
    }
  }

  if (showSavedStatus) {
    UITheme::drawCenteredText(renderer, safeArea, SMALL_FONT_ID,
                              safeArea.y + safeArea.height - renderer.getLineHeight(SMALL_FONT_ID) - 4,
                              tr(STR_STICKY_NOTE_SAVED));
  }
}

bool StickyNotesActivity::saveNoteSleepImage() {
  if (!Storage.exists("/.sleep") && !Storage.mkdir("/.sleep")) {
    LOG_ERR(LOG_TAG, "Failed to create /.sleep");
    return false;
  }
  if (Storage.exists(NOTE_TEMP_PATH)) Storage.remove(NOTE_TEMP_PATH);
  if (!ScreenshotUtil::saveFramebufferAsBmp(NOTE_TEMP_PATH, renderer.getFrameBuffer(), renderer.getDisplayWidth(),
                                            renderer.getDisplayHeight())) {
    LOG_ERR(LOG_TAG, "Failed to write temporary note bitmap");
    return false;
  }

  if (Storage.exists(NOTE_BACKUP_PATH)) Storage.remove(NOTE_BACKUP_PATH);
  const bool hadExisting = Storage.exists(NOTE_PATH);
  if (hadExisting && !Storage.rename(NOTE_PATH, NOTE_BACKUP_PATH)) {
    Storage.remove(NOTE_TEMP_PATH);
    LOG_ERR(LOG_TAG, "Failed to back up current note bitmap");
    return false;
  }
  if (!Storage.rename(NOTE_TEMP_PATH, NOTE_PATH)) {
    if (hadExisting) Storage.rename(NOTE_BACKUP_PATH, NOTE_PATH);
    Storage.remove(NOTE_TEMP_PATH);
    LOG_ERR(LOG_TAG, "Failed to install note bitmap");
    return false;
  }
  if (hadExisting) Storage.remove(NOTE_BACKUP_PATH);
  return true;
}

bool StickyNotesActivity::selectNoteSleepImage() {
  APP_STATE.favoriteSleepImagePath = NOTE_PATH;
  SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR(LOG_TAG, "Failed to pin note sleep image");
    return false;
  }
  if (!SETTINGS.saveToFile()) {
    LOG_ERR(LOG_TAG, "Failed to select custom sleep mode");
    return false;
  }
  return true;
}

#ifndef SIMULATOR
void StickyNotesActivity::onReceive(const uint8_t* sourceMac, const uint8_t* data, const int length, void* context) {
  if (!context) return;
  static_cast<StickyNotesActivity*>(context)->enqueueNote(sourceMac, data, length);
}

void StickyNotesActivity::enqueueNote(const uint8_t* sourceMac, const uint8_t* data, const int length) {
  if (!pendingMutex_ || !sourceMac || length <= 0) return;
  sticky_note::Note parsed;
  if (!sticky_note::parseNote(data, static_cast<size_t>(length), parsed)) return;
  if (xSemaphoreTake(pendingMutex_, 0) != pdTRUE) return;
  pendingNote_ = parsed;
  std::copy(sourceMac, sourceMac + pendingSourceMac_.size(), pendingSourceMac_.begin());
  pending_ = true;
  xSemaphoreGive(pendingMutex_);
}

bool StickyNotesActivity::sendAck(const uint8_t* peerMac, const uint32_t sequence) {
  const auto ack = sticky_note::makeAck(sequence);
  return radio_.send(peerMac, ack.data(), ack.size());
}
#endif

#endif  // CROSSINK_ENABLE_STICKY_NOTES
