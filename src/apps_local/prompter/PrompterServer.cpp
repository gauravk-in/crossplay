#include "PrompterServer.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <string>

#include "PrompterCore.h"
#include "PrompterPageHtml.generated.h"
#include "PrompterStore.h"

namespace {
constexpr uint16_t kPort = 80;
}  // namespace

bool PrompterServer::begin() {
  if (running_) return true;
#ifndef SIMULATOR
  if (WiFi.status() != WL_CONNECTED) return false;
#endif
  server_ = makeUniqueNoThrow<WebServer>(kPort);
  if (!server_) {
    LOG_ERR("PROMPT", "OOM: WebServer");
    return false;
  }
  // A sleeping radio drops the phone's request and the page blames the network.
  WiFi.setSleep(false);
  server_->on("/prompter", HTTP_GET, [this] { handlePage(); });
  server_->on("/prompter/scripts", HTTP_GET, [this] { handleList(); });
  server_->on("/prompter/script", HTTP_GET, [this] { handleRead(); });
  // PUT, not POST: this core hands one callback to both the multipart and the
  // raw paths, and a script arrives as a plain body.
  server_->on("/prompter/script", HTTP_PUT, [this] { handleSave(); });
  server_->on("/prompter/script", HTTP_DELETE, [this] { handleDelete(); });
  server_->onNotFound([this] { server_->send(404, "text/plain", "Not found"); });
  static const char* kHeaders[] = {"If-None-Match"};
  server_->collectHeaders(kHeaders, 1);
  server_->begin();
  running_ = true;
  return true;
}

void PrompterServer::stop() {
  if (!server_) return;
  running_ = false;
  server_->stop();
  server_.reset();
}

void PrompterServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void PrompterServer::handlePage() {
  // Baked into flash at build time, so the ETag is stable for the image.
  if (server_->header("If-None-Match") == PrompterPageHtmlETag) {
    server_->sendHeader("ETag", PrompterPageHtmlETag);
    server_->send(304);
    return;
  }
  server_->sendHeader("Content-Encoding", "gzip");
  server_->sendHeader("ETag", PrompterPageHtmlETag);
  server_->sendHeader("Cache-Control", "no-cache");
  server_->send_P(200, "text/html", PrompterPageHtml, sizeof(PrompterPageHtml));
}

std::string PrompterServer::nameArg() {
  const String raw = server_->arg("name");
  const std::string name(raw.c_str(), raw.length());
  if (!prompter::isScriptName(name)) {
    server_->send(400, "text/plain", "That is not a script name.");
    return std::string();
  }
  return name;
}

void PrompterServer::handleList() {
  std::string text;
  for (const std::string& name : prompter::store::listScripts()) text += name + "\n";
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void PrompterServer::handleRead() {
  const std::string name = nameArg();
  if (name.empty()) return;
  const std::string text = prompter::store::read(prompter::store::scriptPath(name).c_str(), prompter::kMaxScriptBytes);
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", text.c_str());
}

void PrompterServer::handleSave() {
  // The page sends the name it wants; the reader decides the one it keeps.
  const String wanted = server_->arg("name");
  const std::string name = prompter::safeName(std::string(wanted.c_str(), wanted.length()));
  const String raw = server_->arg("plain");
  if (raw.length() > prompter::kMaxScriptBytes) {
    server_->send(413, "text/plain", "That script is too long for the reader. Split it in two.");
    return;
  }
  const std::string text = prompter::cleanScript(std::string(raw.c_str(), raw.length()));
  if (text.empty()) {
    server_->send(400, "text/plain", "That script is empty.");
    return;
  }
  if (!prompter::store::write(prompter::store::scriptPath(name).c_str(), text)) {
    server_->send(500, "text/plain", "The card would not take it.");
    return;
  }
  LOG_INF("PROMPT", "saved %s (%u bytes) from the phone", name.c_str(), static_cast<unsigned>(text.size()));
  lastSaved_ = name;
  changed_ = true;
  server_->sendHeader("Cache-Control", "no-store");
  server_->send(200, "text/plain; charset=utf-8", name.c_str());
}

void PrompterServer::handleDelete() {
  const std::string name = nameArg();
  if (name.empty()) return;
  if (!prompter::store::removeScript(name)) {
    server_->send(404, "text/plain", "That script is not on the reader.");
    return;
  }
  lastSaved_.clear();
  changed_ = true;
  server_->send(200, "text/plain", "Deleted.");
}
