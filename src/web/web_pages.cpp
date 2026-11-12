#include "web/web_pages.h"

#include "web/web_access.h"
#include "app/app_hooks.h"

// The core's pages. The project's own pages (PROJECT_WEB_PAGES, declared in
// project/) go between Status and Settings, in the order the project lists
// them, so the tab strip reads: Status, <project pages>, Settings, Network,
// Firmware, Logs.
//
// Adding a page is a file under pages/ (or project/) plus its line in the
// right table. Nothing is keyed by index.
extern const WebPage PAGE_STATUS;
extern const WebPage PAGE_SETTINGS;
extern const WebPage PAGE_CONFIG;
extern const WebPage PAGE_FIRMWARE;
extern const WebPage PAGE_LOGS;

static const WebPage* const CORE_HEAD[] = { &PAGE_STATUS };
static const WebPage* const CORE_TAIL[] = { &PAGE_SETTINGS, &PAGE_CONFIG, &PAGE_FIRMWARE, &PAGE_LOGS };

static const size_t HEAD_N = sizeof(CORE_HEAD) / sizeof(CORE_HEAD[0]);
static const size_t TAIL_N = sizeof(CORE_TAIL) / sizeof(CORE_TAIL[0]);

size_t webPageCount() { return HEAD_N + PROJECT_WEB_PAGE_COUNT + TAIL_N; }

const WebPage* webPageAt(size_t i) {
  if (i < HEAD_N) return CORE_HEAD[i];
  i -= HEAD_N;
  if (i < PROJECT_WEB_PAGE_COUNT) return PROJECT_WEB_PAGES[i];
  i -= PROJECT_WEB_PAGE_COUNT;
  return i < TAIL_N ? CORE_TAIL[i] : nullptr;
}

const char* webPageLabel(const WebPage &p) { return p.label(); }

bool webPageVisible(const WebPage &p) {
  if (p.applies && !p.applies()) return false;
  return webGateOpen(p.gate);
}
