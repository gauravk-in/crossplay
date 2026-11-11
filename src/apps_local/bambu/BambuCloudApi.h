#pragma once

// The reader's requests to Bambu's account service: sign in, the emailed
// code, and the printers on the account. Verified TLS (BambuCloudRoots.h, or
// /.crosspoint/bambu/.roots.pem), because these carry the account's password
// and token.
//
// Device only. The simulator has no TLS stack, and every call there fails
// with a sentence that says so.

#include <string>

#include "BambuCloud.h"

namespace bambu {
namespace cloud {

// The roots to verify the API and the broker against: the card's bundle when
// it has a usable one, else the baked one.
const char* caRoots();

// The first step. On Token, `account` is filled (user name and expiry
// included); on NeedCode, Bambu has already been asked to email a code.
Login signIn(const std::string& email, const std::string& password, Account& account, std::string& message);
// The second step, with the code from the email.
Login signInWithCode(const std::string& email, const std::string& code, Account& account, std::string& message);
// The printers bound to the account. -1 on failure, with `message` set.
int listPrinters(const Account& account, Printer* out, int max, std::string& message);
// Closes the connection the calls above keep open between them.
void hangUp();

}  // namespace cloud
}  // namespace bambu
