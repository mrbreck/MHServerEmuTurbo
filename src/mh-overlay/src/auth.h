#pragma once
#include <string>
#include <vector>

struct AuthCredentials
{
    std::string email;
    std::string platformTicket;
    bool        ready = false;
};

AuthCredentials& GetAuthCredentials();
bool InstallAuthHook();

// Diagnostic log — ring buffer of the last N auth hook events.
// Read by the overlay to display status in the UI.
struct AuthLog
{
    static constexpr int kMax = 30;
    std::string lines[kMax];
    int         count = 0;

    void Add(const std::string& s);
    // Returns lines as a single \n-joined string for ImGui::Text
    std::string Dump() const;
};
AuthLog& GetAuthLog();
