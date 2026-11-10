#include "BambuLink.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <new>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureClient.h>
#endif

namespace bambu {

namespace {

constexpr uint16_t kKeepAliveSeconds = 60;
constexpr unsigned long kPingEveryMs = 30000;
// Two missed pings: the printer is off, asleep or off the network.
constexpr unsigned long kSilenceMs = 95000;
constexpr unsigned long kStepTimeoutMs = 10000;
// One read's worth. Heap, not stack: the loop task's stack is small.
constexpr size_t kChunk = 2048;
// Bytes read per poll(), so a 30KB pushall does not hold the loop for its
// whole length; the rest arrives on the next passes.
constexpr size_t kPollBudget = 16 * 1024;

}  // namespace

struct Link::Impl {
#if defined(FREEINK_NET_WOLFSSL)
  freeink::SecureClient client;
#endif
  std::unique_ptr<uint8_t[]> chunk;
  mqtt::Reader reader;
  std::string serial;
  bool up = false;
  unsigned long lastSentAt = 0;
  unsigned long lastHeardAt = 0;

  bool send(const std::string& bytes) {
#if defined(FREEINK_NET_WOLFSSL)
    if (client.write(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) != bytes.size()) return false;
    lastSentAt = millis();
    return true;
#else
    (void)bytes;
    return false;
#endif
  }

  // Reads exactly `n` bytes before `deadline`, for the handshake's replies.
  bool readExact(uint8_t* out, const size_t n, const unsigned long deadline, const AbortCallback& shouldAbort) {
#if defined(FREEINK_NET_WOLFSSL)
    size_t got = 0;
    while (got < n) {
      if (static_cast<long>(millis() - deadline) >= 0) return false;
      if (shouldAbort && shouldAbort()) return false;
      if (!client.connected() && client.available() <= 0) return false;
      const int r = client.read(out + got, n - got);
      if (r > 0) {
        got += static_cast<size_t>(r);
      } else {
        delay(10);
      }
    }
    return true;
#else
    (void)out;
    (void)n;
    (void)deadline;
    (void)shouldAbort;
    return false;
#endif
  }
};

Link::Link() : impl_(new (std::nothrow) Impl()) {}
Link::~Link() { stop(); }

bool Link::connected() const { return impl_ && impl_->up; }

bool Link::connect(const Config& config, const std::string& clientId, const AbortCallback& shouldAbort,
                   std::string& why) {
  stop();
  if (!impl_) {
    why = "There was not enough memory to connect.";
    return false;
  }
#if !defined(FREEINK_NET_WOLFSSL)
  (void)config;
  (void)clientId;
  (void)shouldAbort;
  why = "This build has no TLS, so it cannot reach the printer.";
  return false;
#else
  Impl& m = *impl_;
  if (!m.chunk) m.chunk = makeUniqueNoThrow<uint8_t[]>(kChunk);
  if (!m.chunk) {
    LOG_ERR("BAMBU", "OOM: %u byte read buffer", static_cast<unsigned>(kChunk));
    why = "There was not enough memory to connect.";
    return false;
  }
  m.serial = config.serial;
  m.client.setInsecure();
  m.client.setTimeout(kStepTimeoutMs);
  if (shouldAbort) m.client.setAbortCallback(shouldAbort);
  if (!m.client.connect(config.host.c_str(), kPort)) {
    if (m.client.aborted()) {
      why = "Stopped.";
    } else {
      why = "The printer at " + config.host +
            " did not answer. Check the address, that it is on, and that the reader is on the same Wi-Fi.";
    }
    m.client.stop();
    return false;
  }
  LOG_INF("BAMBU", "TLS up to %s", config.host.c_str());

  if (!m.send(mqtt::connectPacket(clientId, kUser, config.accessCode, kKeepAliveSeconds))) {
    why = "The printer closed the connection.";
    m.client.stop();
    return false;
  }
  uint8_t ack[4] = {};
  if (!m.readExact(ack, sizeof(ack), millis() + kStepTimeoutMs, shouldAbort)) {
    why = "The printer accepted the connection but never answered it. Try LAN-only mode on the printer.";
    m.client.stop();
    return false;
  }
  if ((ack[0] >> 4) != mqtt::kConnAck || ack[1] != 2) {
    why = "The printer answered with something that is not MQTT.";
    m.client.stop();
    return false;
  }
  if (ack[3] != 0) {
    LOG_ERR("BAMBU", "CONNACK %u", static_cast<unsigned>(ack[3]));
    why = mqtt::connAckReason(ack[3]);
    m.client.stop();
    return false;
  }
  if (!m.send(mqtt::subscribePacket(1, mqtt::reportTopic(config.serial))) ||
      !m.send(mqtt::publishPacket(mqtt::requestTopic(config.serial), mqtt::pushAllPayload()))) {
    why = "The printer closed the connection.";
    m.client.stop();
    return false;
  }
  m.reader.reset();
  m.up = true;
  m.lastHeardAt = millis();
  return true;
#endif
}

Link::Poll Link::poll(PrinterState& state, std::string& why) {
  if (!impl_ || !impl_->up) return Poll::Dropped;
#if !defined(FREEINK_NET_WOLFSSL)
  (void)state;
  (void)why;
  return Poll::Dropped;
#else
  Impl& m = *impl_;
  bool changed = false;
  bool bad = false;
  size_t budget = kPollBudget;
  while (budget > 0) {
    const int avail = m.client.available();
    if (avail <= 0) break;
    size_t want = static_cast<size_t>(avail);
    if (want > kChunk) want = kChunk;
    if (want > budget) want = budget;
    const int r = m.client.read(m.chunk.get(), want);
    if (r <= 0) break;
    m.lastHeardAt = millis();
    budget -= static_cast<size_t>(r);
    m.reader.consume(m.chunk.get(), static_cast<size_t>(r), state, changed, bad);
    if (bad) break;
  }
  if (bad) {
    why = "The printer sent something this reader cannot read.";
    stop();
    return Poll::Dropped;
  }
  if (!m.client.connected() && m.client.available() <= 0) {
    why = "The printer closed the connection.";
    stop();
    return Poll::Dropped;
  }
  const unsigned long now = millis();
  if (now - m.lastHeardAt > kSilenceMs) {
    why = "The printer stopped answering.";
    stop();
    return Poll::Dropped;
  }
  if (now - m.lastSentAt > kPingEveryMs && !m.send(mqtt::pingPacket())) {
    why = "The printer closed the connection.";
    stop();
    return Poll::Dropped;
  }
  return changed ? Poll::Changed : Poll::Idle;
#endif
}

bool Link::requestPushAll() {
  if (!impl_ || !impl_->up) return false;
  return impl_->send(mqtt::publishPacket(mqtt::requestTopic(impl_->serial), mqtt::pushAllPayload()));
}

void Link::stop() {
  if (!impl_) return;
#if defined(FREEINK_NET_WOLFSSL)
  if (impl_->up) impl_->send(mqtt::disconnectPacket());
  impl_->client.stop();
#endif
  impl_->up = false;
  impl_->reader.reset();
}

}  // namespace bambu
