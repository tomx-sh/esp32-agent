#include "config_page.h"

#include "generated/config_page_html.h"

String render_config_page(const String &stationStatus, const String &baseUrl) {
  String html(kConfigPageHtml);
  html.replace("{{STATION_STATUS}}", stationStatus);
  html.replace("{{BASE_URL}}", baseUrl);
  return html;
}
