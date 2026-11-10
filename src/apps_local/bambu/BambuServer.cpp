#include "BambuServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>

#include "BambuCore.h"
#include "BambuPageHtml.generated.h"
#include "BambuStore.h"

namespace {
constexpr uint16_t kPort = 80;
}  // namespace

bool BambuServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("BAMBU", "OOM: WebServer");
    return false;
  }
  // A sleeping radio drops the phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/bambu", HTTP_GET, [this] { handlePage(); });
  server_->on("/bambu/config", HTTP_GET, [this] { handleRead(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and the settings are small enough to arrive as a plain body.
  server_->on("/bambu/config", HTTP_PUT, [this] { handleSave(); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match"};
  server_->collectHeaders(kHeaders, 1);
  server_->begin();
  running_ = true;
  return true;
}

void BambuServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void BambuServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void BambuServer::handlePage() {
  // Baked into flash at build time, so the ETag is stable for the image.
  if (server_->header("If-None-Match") == BambuPageHtmlETag) {
    server_->sendHeader("ETag", BambuPageHtmlETag);
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", BambuPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", BambuPageHtml, sizeof(BambuPageHtml));
}

void BambuServer::handleRead() {
  server_->sendHeader("Cache-Control", "no-store");
  // Normalised on the way out, so the page parses the one shape the reader
  // writes rather than whatever a computer left in the file.
  const std::string text =
      bambu::formatConfig(bambu::parseConfig(bambu::store::read(bambu::store::kConfigPath, bambu::kMaxConfigBytes))) +
      facts_;
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void BambuServer::handleSave() {
  const String raw = server_->arg("plain");
  if (raw.length() > bambu::kMaxConfigBytes) {
    server_->send(413, "text/plain", "Those settings are too long for the reader.");
    return;
  }
  // A form post or an empty body would parse as all defaults and drop the code.
  if (raw.indexOf("host=") < 0) {
    server_->send(400, "text/plain", "Send the settings as plain key=value lines.");
    return;
  }
  // Parsed and written back out, so every limit in BambuCore applies to a
  // phone exactly as it does to a file.
  const std::string text = bambu::formatConfig(bambu::parseConfig(std::string(raw.c_str(), raw.length())));
  if (!bambu::store::write(bambu::store::kConfigPath, text)) {
    server_->send(500, "text/plain", "The card would not take it.");
    return;
  }
  changed_ = true;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", (text + facts_).c_str());
}
