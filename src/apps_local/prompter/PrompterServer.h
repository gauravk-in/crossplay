#pragma once

// The phone page's server. GET /prompter is the page; /prompter/scripts lists
// the scripts on the card and /prompter/script reads, writes and deletes one.
// Nothing else is routed.
//
// Its own small WebServer rather than another surface on CrossPointWebServer,
// so adding an app costs no upstream file.

#include <WebServer.h>

#include <memory>
#include <string>

class PrompterServer {
 public:
  ~PrompterServer() { stop(); }

  // False when the server could not be made or there is no network to put it
  // on. The simulator has neither, and the screen is still drawn there.
  bool begin();
  void stop();
  bool isRunning() const { return running_; }
  void handleClient();

  // True once a phone has saved or deleted a script; `name` is the one saved,
  // empty after a delete. Cleared by the reader of it.
  bool takeChanged(std::string& name) {
    if (!changed_) return false;
    changed_ = false;
    name = lastSaved_;
    return true;
  }

 private:
  void handlePage();
  void handleList();
  void handleRead();
  void handleSave();
  void handleDelete();
  // The ?name= argument, checked; empty and a 400 sent when it is not a name.
  std::string nameArg();

  std::unique_ptr<WebServer> server_;
  std::string lastSaved_;
  bool running_ = false;
  bool changed_ = false;
};
