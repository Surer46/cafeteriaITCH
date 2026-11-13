#include "app/domain/clock.hpp"
#include <cstdio>
#include <ctime>

namespace app::domain {

namespace {

std::tm to_local_tm(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::tm to_utc_tm(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    gmtime_s(&out, &t);
#else
    gmtime_r(&t, &out);
#endif
    return out;
}

} // namespace

Timestamp timestamp_from(std::chrono::system_clock::time_point tp) {
    using namespace std::chrono;
    const auto ms_total = duration_cast<milliseconds>(tp.time_since_epoch()).count();
    // Floor hacia el pasado también para instantes anteriores a 1970.
    long long secs = ms_total / 1000;
    long long ms = ms_total % 1000;
    if (ms < 0) {
        ms += 1000;
        secs -= 1;
    }
    const std::time_t t = static_cast<std::time_t>(secs);
    const std::tm local_tm = to_local_tm(t);
    const std::tm utc_tm = to_utc_tm(t);

    char buf[40];
    Timestamp ts;
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
                  local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
    ts.local = buf;
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
                  utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec, static_cast<int>(ms));
    ts.utc = buf;
    return ts;
}

Timestamp now() {
    return timestamp_from(std::chrono::system_clock::now());
}

} // namespace app::domain
