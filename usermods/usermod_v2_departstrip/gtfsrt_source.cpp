#include "gtfsrt_source.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>

#include <pb_decode.h>

namespace {

// Trim leading and trailing ASCII whitespace from a std::string.
static void trimStringInPlace(std::string& s) {
  size_t begin = 0;
  while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
  size_t end = s.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
  if (begin == 0 && end == s.size()) return;
  s = s.substr(begin, end - begin);
}

// Lowercase a string (ASCII) for case-insensitive comparisons.
static std::string toLowerCopy(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

struct ParseContext {
  String agency;
  String stopCode;
  std::string stopCodeStd;
  std::string stopCodeLower;
  std::string stopCodeShort;
  std::string stopCodeShortLower;
  time_t now;
  time_t apiTimestamp = 0;
  size_t feedEntityCount = 0;
  size_t tripUpdateCount = 0;
  size_t tripUpdateMatched = 0;
  size_t stopUpdatesTotal = 0;
  size_t stopUpdatesMatched = 0;
  size_t bytesRead = 0;
  size_t logMatches = 0;
  std::vector<DepartModel::Entry::Item> items;

  ParseContext(const String& agencyIn, const String& stopIn, time_t nowIn)
    : agency(agencyIn), stopCode(stopIn), stopCodeStd(stopIn.c_str()), now(nowIn) {
    trimStringInPlace(stopCodeStd);
    stopCodeLower = toLowerCopy(stopCodeStd);
    size_t colon = stopCodeStd.find(':');
    if (colon != std::string::npos && colon + 1 < stopCodeStd.size()) {
      stopCodeShort = stopCodeStd.substr(colon + 1);
      trimStringInPlace(stopCodeShort);
      stopCodeShortLower = toLowerCopy(stopCodeShort);
    }
    items.reserve(16);
  }
};

struct TripAccumulator {
  struct PendingStop {
    int64_t epoch = 0;
    bool hasSequence = false;
    uint32_t stopSequence = 0;
  };
  struct TripInfo {
    std::string routeId;
    std::string tripId;
  } trip;
  std::vector<PendingStop> matches;
  size_t totalStopUpdates = 0;
  size_t matchedStopUpdates = 0;

  TripAccumulator() { matches.reserve(4); }
};

struct HttpStreamState {
  Stream* stream = nullptr;
  ParseContext* ctx = nullptr;
  bool limited = false;
  size_t remaining = 0;
  bool shortRead = false;
};

static bool matchesStopId(const std::string& rawId, const ParseContext& ctx) {
  if (rawId.empty()) return false;
  std::string cand = rawId;
  trimStringInPlace(cand);
  if (cand.empty()) return false;
  std::string candLower = toLowerCopy(cand);
  if (cand == ctx.stopCodeStd || candLower == ctx.stopCodeLower) return true;
  if (!ctx.stopCodeShort.empty()) {
    if (cand == ctx.stopCodeShort || candLower == ctx.stopCodeShortLower) return true;
  }
  size_t colon = cand.find(':');
  if (colon != std::string::npos && colon + 1 < cand.size()) {
    std::string tail = cand.substr(colon + 1);
    trimStringInPlace(tail);
    if (!tail.empty()) {
      std::string tailLower = toLowerCopy(tail);
      if (tail == ctx.stopCodeStd || tailLower == ctx.stopCodeLower) return true;
      if (!ctx.stopCodeShort.empty() && (tail == ctx.stopCodeShort || tailLower == ctx.stopCodeShortLower)) return true;
    }
  }
  return false;
}

static bool streamRead(pb_istream_t* stream, pb_byte_t* buf, size_t count) {
  if (!stream || !stream->state) return false;
  HttpStreamState* state = static_cast<HttpStreamState*>(stream->state);
  if (!state || !state->stream) return false;
  if (state->limited && count > state->remaining) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT stream requested %u beyond remaining %u\n",
                 (unsigned)count,
                 (unsigned)state->remaining);
    stream->bytes_left = 0;
    return false;
  }
  size_t total = 0;
  while (total < count) {
    size_t n = state->stream->readBytes(reinterpret_cast<char*>(buf) + total, count - total);
    if (n == 0) {
      if (!state->shortRead) {
        state->shortRead = true;
        DEBUG_PRINTF("DepartStrip: GTFS-RT stream short read after %u bytes (need %u more)\n",
                     (unsigned)total,
                     (unsigned)(count - total));
      }
      if (state->limited) {
        state->remaining = 0;
        stream->bytes_left = 0;
      }
      return false;
    }
    total += n;
    if (state->ctx) state->ctx->bytesRead += n;
    if (state->limited && state->remaining >= n) state->remaining -= n;
  }
  return true;
}

static bool readStringToStd(pb_istream_t* stream, std::string& out) {
  out.clear();
  size_t remaining = stream->bytes_left;
  if (remaining == 0) return true;
  out.reserve(out.size() + remaining);
  uint8_t buffer[64];
  while (stream->bytes_left > 0) {
    size_t take = stream->bytes_left;
    if (take > sizeof(buffer)) take = sizeof(buffer);
    if (!pb_read(stream, buffer, take)) return false;
    out.append(reinterpret_cast<const char*>(buffer), take);
  }
  return true;
}

static bool decodeStopTimeEvent(pb_istream_t* stream, int64_t& outTime, bool& hasTime) {
  outTime = 0;
  hasTime = false;
  int64_t scheduledTime = 0;
  bool hasScheduled = false;
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    switch (tag) {
      case 2: { // time
        if (wireType != PB_WT_VARINT) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT StopTimeEvent unexpected wireType %u for time\n", wireType);
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        uint64_t raw = 0;
        if (!pb_decode_varint(stream, &raw)) return false;
        outTime = static_cast<int64_t>(raw);
        hasTime = true;
        break;
      }
      case 4: { // scheduled_time (experimental)
        if (wireType != PB_WT_VARINT) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT StopTimeEvent unexpected wireType %u for scheduled_time\n", wireType);
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        uint64_t raw = 0;
        if (!pb_decode_varint(stream, &raw)) return false;
        scheduledTime = static_cast<int64_t>(raw);
        hasScheduled = true;
        break;
      }
      default:
        if (!pb_skip_field(stream, wireType)) return false;
        break;
    }
  }
  if (!eof) {
    DEBUG_PRINTLN(F("DepartStrip: GTFS-RT StopTimeEvent missing EOF"));
    return false;
  }
  if (!hasTime && hasScheduled) {
    outTime = scheduledTime;
    hasTime = true;
  }
  return true;
}

static bool decodeStopTimeProperties(pb_istream_t* stream, std::string& assignedStopId) {
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    if (tag == 1 && wireType == PB_WT_STRING) {
      pb_istream_t sub;
      if (!pb_make_string_substream(stream, &sub)) return false;
      bool ok = readStringToStd(&sub, assignedStopId);
      pb_close_string_substream(stream, &sub);
      if (!ok) return false;
    } else {
      if (!pb_skip_field(stream, wireType)) return false;
    }
  }
  return eof;
}

static bool decodeStopTimeUpdate(pb_istream_t* stream, TripAccumulator& accum, ParseContext& ctx) {
  uint32_t stopSequence = 0;
  bool hasStopSequence = false;
  std::string stopId;
  int64_t arrivalTime = 0;
  bool hasArrival = false;
  int64_t departureTime = 0;
  bool hasDeparture = false;
  int scheduleRelationship = 0; // SCHEDULED by default

  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    switch (tag) {
      case 1: { // stop_sequence
        if (wireType != PB_WT_VARINT) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        uint64_t raw = 0;
        if (!pb_decode_varint(stream, &raw)) return false;
        stopSequence = static_cast<uint32_t>(raw);
        hasStopSequence = true;
        break;
      }
      case 4: { // stop_id
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open stop_id substream"));
          return false;
        }
        bool ok = readStringToStd(&sub, stopId);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (ctx.stopUpdatesTotal < 5) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT raw stop_id='%s'\n", stopId.c_str());
        }
        break;
      }
      case 2: { // arrival
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open arrival substream"));
          return false;
        }
        bool ok = decodeStopTimeEvent(&sub, arrivalTime, hasArrival);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (!hasArrival) DEBUG_PRINTLN(F("DepartStrip: GTFS-RT arrival missing time"));
        break;
      }
      case 3: { // departure
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open departure substream"));
          return false;
        }
        bool ok = decodeStopTimeEvent(&sub, departureTime, hasDeparture);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (!hasDeparture) DEBUG_PRINTLN(F("DepartStrip: GTFS-RT departure missing time"));
        break;
      }
      case 5: { // schedule relationship
        if (wireType != PB_WT_VARINT) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        uint64_t raw = 0;
        if (!pb_decode_varint(stream, &raw)) return false;
        scheduleRelationship = static_cast<int>(raw);
        break;
      }
      case 6: { // StopTimeProperties (assigned_stop_id)
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open StopTimeProperties substream"));
          return false;
        }
        std::string assigned;
        bool ok = decodeStopTimeProperties(&sub, assigned);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (stopId.empty() && !assigned.empty()) stopId = assigned;
        break;
      }
      default:
        if (!pb_skip_field(stream, wireType)) return false;
        break;
    }
  }
  if (!eof) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT StopTimeUpdate missing EOF (stopUpdates=%u)\n",
                 (unsigned)ctx.stopUpdatesTotal);
    return false;
  }

  ctx.stopUpdatesTotal++;
  accum.totalStopUpdates++;

  // Skip SKIPPED or NO_DATA updates.
  if (scheduleRelationship == 1 || scheduleRelationship == 2) {
    if (ctx.stopUpdatesTotal <= 5) {
      DEBUG_PRINTLN(F("DepartStrip: GTFS-RT stop skipped/no-data"));
    }
    return true;
  }

  int64_t chosen = 0;
  if (hasDeparture) chosen = departureTime;
  else if (hasArrival) chosen = arrivalTime;

  if (chosen <= 0) {
    if (ctx.stopUpdatesTotal <= 5) {
      DEBUG_PRINTF("DepartStrip: GTFS-RT stop had no usable time (stopId='%s')\n", stopId.c_str());
    }
    return true;
  }

  if (stopId.empty()) {
    if (ctx.stopUpdatesTotal <= 5) {
      DEBUG_PRINTLN(F("DepartStrip: GTFS-RT stop missing stop_id"));
    }
    // Without a stop_id we have no reliable match for a single-stop source.
    return true;
  }

  bool stopMatched = matchesStopId(stopId, ctx);
  if (!stopMatched) {
    if (ctx.stopUpdatesTotal <= 5 || (ctx.stopUpdatesTotal % 200) == 0) {
      DEBUG_PRINTF("DepartStrip: GTFS-RT stop_id '%s' ignored (want '%s')\n",
                   stopId.c_str(),
                   ctx.stopCodeStd.c_str());
    }
    return true;
  }

  if (ctx.stopUpdatesMatched < 5 || (ctx.stopUpdatesMatched % 200) == 0) {
    char seqBuf[12];
    if (hasStopSequence) {
      snprintf(seqBuf, sizeof(seqBuf), "%u", (unsigned)stopSequence);
    } else {
      seqBuf[0] = '?'; seqBuf[1] = '\0';
    }
    DEBUG_PRINTF("DepartStrip: GTFS-RT stop matched '%s' (seq=%s)\n",
                 stopId.c_str(),
                 seqBuf);
  }

  TripAccumulator::PendingStop pending;
  pending.epoch = chosen;
  pending.hasSequence = hasStopSequence;
  pending.stopSequence = stopSequence;
  accum.matches.push_back(pending);
  accum.matchedStopUpdates++;
  return true;
}

static bool decodeTripDescriptor(pb_istream_t* stream, TripAccumulator& accum, ParseContext& ctx) {
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    switch (tag) {
      case 1: { // trip_id
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT TripDescriptor failed substream (tag=%u)\n", tag);
          return false;
        }
        bool ok = readStringToStd(&sub, accum.trip.tripId);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (accum.trip.tripId.size() && ctx.tripUpdateCount <= 5) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT TripDescriptor trip_id='%s'\n", accum.trip.tripId.c_str());
        }
        break;
      }
      case 5: { // route_id
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT TripDescriptor failed substream (route tag=%u)\n", tag);
          return false;
        }
        bool ok = readStringToStd(&sub, accum.trip.routeId);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        if (accum.trip.routeId.size() && ctx.tripUpdateCount <= 5) {
          DEBUG_PRINTF("DepartStrip: GTFS-RT TripDescriptor route_id='%s'\n", accum.trip.routeId.c_str());
        }
        break;
      }
      default:
        if (!pb_skip_field(stream, wireType)) return false;
        break;
    }
  }
  if (!eof) {
    DEBUG_PRINTLN(F("DepartStrip: GTFS-RT TripDescriptor missing EOF"));
    return false;
  }
  return true;
}

static time_t clampToTimeT(int64_t value) {
  if (value <= 0) return 0;
  const int64_t maxT = static_cast<int64_t>(std::numeric_limits<time_t>::max());
  if (value > maxT) return static_cast<time_t>(maxT);
  return static_cast<time_t>(value);
}

static size_t flushTripAccumulator(TripAccumulator& accum, ParseContext& ctx) {
  if (accum.matches.empty()) return 0;
  String lineRef;
  if (!accum.trip.routeId.empty()) lineRef = accum.trip.routeId.c_str();
  else if (!accum.trip.tripId.empty()) lineRef = accum.trip.tripId.c_str();
  if (lineRef.length() == 0) lineRef = F("?");

  size_t added = 0;
  for (const auto& pending : accum.matches) {
    time_t dep = clampToTimeT(pending.epoch);
    if (dep == 0) continue;
    // Discard stale departures more than ~1 hour in the past.
    if (ctx.now > 0 && dep + 3600 < ctx.now) continue;
    DepartModel::Entry::Item item;
    item.estDep = dep;
    item.lineRef = lineRef;
    ctx.items.push_back(std::move(item));
    ctx.stopUpdatesMatched++;
    ++added;

    if (ctx.logMatches < 6) {
      char timeBuf[24];
      departstrip::util::fmt_local(timeBuf, sizeof(timeBuf), item.estDep, "%H:%M:%S");
      char seqBuf[12];
      if (pending.hasSequence) snprintf(seqBuf, sizeof(seqBuf), "%u", (unsigned)pending.stopSequence);
      else { seqBuf[0] = '?'; seqBuf[1] = '\0'; }
      DEBUG_PRINTF("DepartStrip: GTFS-RT match #%u: line='%s' dep=%s seq=%s\n",
                   (unsigned)(ctx.logMatches + 1),
                   item.lineRef.c_str(),
                   timeBuf,
                   seqBuf);
      ctx.logMatches++;
    }
  }
  return added;
}

static bool decodeTripUpdate(pb_istream_t* stream, ParseContext& ctx) {
  TripAccumulator accum;
  uint64_t tripTimestamp = 0;
  ctx.tripUpdateCount++;

  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    switch (tag) {
      case 1: { // TripDescriptor
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open TripDescriptor substream"));
          return false;
        }
        bool ok = decodeTripDescriptor(&sub, accum, ctx);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        break;
      }
      case 2: { // StopTimeUpdate
        if (wireType != PB_WT_STRING) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        pb_istream_t sub;
        if (!pb_make_string_substream(stream, &sub)) {
          DEBUG_PRINTLN(F("DepartStrip: GTFS-RT failed to open StopTimeUpdate substream"));
          return false;
        }
        bool ok = decodeStopTimeUpdate(&sub, accum, ctx);
        pb_close_string_substream(stream, &sub);
        if (!ok) return false;
        break;
      }
      case 4: { // timestamp
        if (wireType != PB_WT_VARINT) {
          if (!pb_skip_field(stream, wireType)) return false;
          break;
        }
        uint64_t raw = 0;
        if (!pb_decode_varint(stream, &raw)) return false;
        if (raw > tripTimestamp) tripTimestamp = raw;
        break;
      }
      default:
        if (!pb_skip_field(stream, wireType)) return false;
        break;
    }
  }
  if (!eof) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT TripUpdate missing EOF after %u stopUpdates\n",
                 (unsigned)accum.totalStopUpdates);
    return false;
  }

  if (tripTimestamp > 0) {
    time_t t = clampToTimeT(static_cast<int64_t>(tripTimestamp));
    if (t > ctx.apiTimestamp) ctx.apiTimestamp = t;
  }

  size_t added = flushTripAccumulator(accum, ctx);
  if (added > 0) {
    ctx.tripUpdateMatched++;
    DEBUG_PRINTF("DepartStrip: GTFS-RT trip matched route='%s' trip='%s' stopUpdates=%u matchedStops=%u added=%u\n",
                 accum.trip.routeId.c_str(),
                 accum.trip.tripId.c_str(),
                 (unsigned)accum.totalStopUpdates,
                 (unsigned)accum.matchedStopUpdates,
                 (unsigned)added);
  } else if (ctx.tripUpdateCount <= 5 || (ctx.tripUpdateCount % 50 == 0)) {
    const char* route = accum.trip.routeId.empty() ? "" : accum.trip.routeId.c_str();
    const char* trip = accum.trip.tripId.empty() ? "" : accum.trip.tripId.c_str();
    DEBUG_PRINTF("DepartStrip: GTFS-RT trip had no matching stops (route='%s' trip='%s' stopUpdates=%u)\n",
                 route,
                 trip,
                 (unsigned)accum.totalStopUpdates);
  }
  return true;
}

static bool decodeFeedEntity(pb_istream_t* stream, ParseContext& ctx) {
  ctx.feedEntityCount++;
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    if (tag == 3 && wireType == PB_WT_STRING) { // trip_update
      pb_istream_t sub;
      if (!pb_make_string_substream(stream, &sub)) return false;
      bool ok = decodeTripUpdate(&sub, ctx);
      pb_close_string_substream(stream, &sub);
      if (!ok) return false;
    } else {
      if (!pb_skip_field(stream, wireType)) return false;
    }
  }
  if (!eof) {
    DEBUG_PRINTLN(F("DepartStrip: GTFS-RT FeedEntity missing EOF"));
    return false;
  }
  return true;
}

static bool decodeFeedHeader(pb_istream_t* stream, ParseContext& ctx) {
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    if (tag == 3 && wireType == PB_WT_VARINT) { // timestamp
      uint64_t raw = 0;
      if (!pb_decode_varint(stream, &raw)) return false;
      time_t t = clampToTimeT(static_cast<int64_t>(raw));
      if (t > ctx.apiTimestamp) ctx.apiTimestamp = t;
      if (raw > 0 && ctx.feedEntityCount == 0 && ctx.tripUpdateCount == 0) {
        char tsBuf[24];
        departstrip::util::fmt_local(tsBuf, sizeof(tsBuf), ctx.apiTimestamp, "%H:%M:%S");
        DEBUG_PRINTF("DepartStrip: GTFS-RT header timestamp %s\n", tsBuf);
      }
    } else {
      if (!pb_skip_field(stream, wireType)) return false;
    }
  }
  return eof;
}

static bool decodeFeedMessage(pb_istream_t* stream, ParseContext& ctx) {
  pb_wire_type_t wireType;
  uint32_t tag;
  bool eof = false;
  bool sawHeader = false;
  while (pb_decode_tag(stream, &wireType, &tag, &eof)) {
    if (tag == 1 && wireType == PB_WT_STRING) {
      pb_istream_t sub;
      if (!pb_make_string_substream(stream, &sub)) return false;
      bool ok = decodeFeedHeader(&sub, ctx);
      pb_close_string_substream(stream, &sub);
      if (!ok) return false;
      sawHeader = true;
    } else if (tag == 2 && wireType == PB_WT_STRING) {
      pb_istream_t sub;
      if (!pb_make_string_substream(stream, &sub)) return false;
      bool ok = decodeFeedEntity(&sub, ctx);
      pb_close_string_substream(stream, &sub);
      if (!ok) return false;
    } else {
      if (!pb_skip_field(stream, wireType)) return false;
    }
  }
  if (!sawHeader) {
    DEBUG_PRINTLN(F("DepartStrip: GTFS-RT feed missing header"));
  }
  if (!eof) {
    DEBUG_PRINTLN(F("DepartStrip: GTFS-RT FeedMessage missing EOF"));
  }
  return eof && sawHeader;
}

static bool parseGtfsRtStream(Stream& body, size_t contentLength, ParseContext& ctx, const char** errOut) {
  HttpStreamState state;
  state.stream = &body;
  state.ctx = &ctx;
  if (contentLength > 0) {
    state.limited = true;
    state.remaining = contentLength;
  }
  pb_istream_t istream = {streamRead, &state, contentLength > 0 ? contentLength : PB_SIZE_MAX};
#ifndef PB_NO_ERRMSG
  istream.errmsg = nullptr;
#endif
  ctx.bytesRead = 0;
  bool ok = decodeFeedMessage(&istream, ctx);
  if (errOut) {
#ifndef PB_NO_ERRMSG
    *errOut = ok ? nullptr : istream.errmsg;
#else
    *errOut = ok ? nullptr : "decode error";
#endif
  }
  if (state.shortRead) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT stream short read flagged bytes=%u remaining=%u\n",
                 (unsigned)ctx.bytesRead,
                 (unsigned)state.remaining);
  }
  if (state.limited && state.remaining > 0) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT stream finished with %u bytes remaining\n",
                 (unsigned)state.remaining);
  }
  return ok;
}

} // namespace

GtfsRtSource::GtfsRtSource(const char* key) {
  if (key && *key) configKey_ = key;
}

std::unique_ptr<DepartModel> GtfsRtSource::fetch(std::time_t now) {
  if (!enabled_ || now == 0) return nullptr;
  if (now < nextFetch_) {
    uint32_t interval = updateSecs_ > 0 ? updateSecs_ : 60;
    if (lastBackoffLog_ == 0 || now - lastBackoffLog_ >= interval) {
      lastBackoffLog_ = now;
      long rem = (long)(nextFetch_ - now);
      if (rem < 0) rem = 0;
      if (backoffMult_ > 1) {
        DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: backoff x%u %s, next in %lds\n",
                     (unsigned)backoffMult_, sourceKey().c_str(), rem);
      } else {
        DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: waiting %s, next in %lds\n",
                     sourceKey().c_str(), rem);
      }
    }
    return nullptr;
  }

  String url = composeUrl(agency_, stopCode_);
  String redacted = url;
  auto redactParam = [&](const __FlashStringHelper* key) {
    String k(key);
    int idx = redacted.indexOf(k);
    if (idx >= 0) {
      int valStart = idx + k.length();
      int valEnd = redacted.indexOf('&', valStart);
      if (valEnd < 0) valEnd = redacted.length();
      redacted = redacted.substring(0, valStart) + F("REDACTED") + redacted.substring(valEnd);
    }
  };
  redactParam(F("api_key="));
  redactParam(F("apikey="));
  redactParam(F("key="));
  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: URL: %s\n", redacted.c_str());

  int contentLen = 0;
  int httpStatus = 0;
  if (!httpBegin(url, contentLen, httpStatus)) {
    long delay = (long)updateSecs_ * (long)backoffMult_;
    DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: scheduling backoff x%u %s for %lds (HTTP error)\n",
                 (unsigned)backoffMult_, sourceKey().c_str(), delay);
    nextFetch_ = now + delay;
    if (backoffMult_ < 16) backoffMult_ *= 2;
    return nullptr;
  }

  String ctype = http_.header("Content-Type");
  String clen = http_.header("Content-Length");
  String cenc = http_.header("Content-Encoding");
  String tenc = http_.header("Transfer-Encoding");

  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: status=%d type='%s' lenHint=%d contentLengthHdr=%s encoding='%s' transfer='%s'\n",
               httpStatus,
               ctype.c_str(),
               contentLen,
               clen.c_str(),
               cenc.c_str(),
               tenc.c_str());
  ParseContext ctx(agency_, stopCode_, now);
  const char* decodeErr = nullptr;
  bool parsed = false;

  Stream& body = http_.getStream();
  size_t streamLen = (contentLen > 0) ? (size_t)contentLen : 0;
  parsed = parseGtfsRtStream(body, streamLen, ctx, &decodeErr);

  http_.end();

  nextFetch_ = now + updateSecs_;
  backoffMult_ = 1;
  lastBackoffLog_ = 0;

  if (!parsed) {
    DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: protobuf decode failed (%s) bytes=%u feedEntities=%u tripUpdates=%u stopUpdates=%u\n",
                 decodeErr ? decodeErr : "unknown error",
                 (unsigned)ctx.bytesRead,
                 (unsigned)ctx.feedEntityCount,
                 (unsigned)ctx.tripUpdateCount,
                 (unsigned)ctx.stopUpdatesTotal);
    return nullptr;
  }

  DEBUG_PRINTF("DepartStrip: GTFS-RT decode complete bytes=%u feedEntities=%u tripUpdates=%u matchedTrips=%u stopUpdates=%u matchedStopUpdates=%u\n",
               (unsigned)ctx.bytesRead,
               (unsigned)ctx.feedEntityCount,
               (unsigned)ctx.tripUpdateCount,
               (unsigned)ctx.tripUpdateMatched,
               (unsigned)ctx.stopUpdatesTotal,
               (unsigned)ctx.stopUpdatesMatched);

  if (ctx.items.empty()) {
    DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: no departures parsed (feedEntities=%u tripUpdates=%u stopUpdates=%u matched=%u bytes=%u)\n",
                 (unsigned)ctx.feedEntityCount,
                 (unsigned)ctx.tripUpdateCount,
                 (unsigned)ctx.stopUpdatesTotal,
                 (unsigned)ctx.stopUpdatesMatched,
                 (unsigned)ctx.bytesRead);
    return nullptr;
  }

  std::sort(ctx.items.begin(), ctx.items.end(), [](const DepartModel::Entry::Item& a, const DepartModel::Entry::Item& b) {
    if (a.estDep != b.estDep) return a.estDep < b.estDep;
    return a.lineRef.compareTo(b.lineRef) < 0;
  });
  ctx.items.erase(std::unique(ctx.items.begin(), ctx.items.end(), [](const DepartModel::Entry::Item& a, const DepartModel::Entry::Item& b) {
    return a.estDep == b.estDep && a.lineRef == b.lineRef;
  }), ctx.items.end());

  const size_t MAX_ITEMS = 24;
  if (ctx.items.size() > MAX_ITEMS) {
    DEBUG_PRINTF("DepartStrip: GTFS-RT truncating items %u->%u\n",
                 (unsigned)ctx.items.size(),
                 (unsigned)MAX_ITEMS);
    ctx.items.resize(MAX_ITEMS);
  }

  DepartModel::Entry board;
  board.key.reserve(agency_.length() + 1 + stopCode_.length());
  board.key = agency_;
  board.key += ':';
  board.key += stopCode_;
  board.batch.apiTs = ctx.apiTimestamp ? ctx.apiTimestamp : now;
  if (board.batch.apiTs == 0) board.batch.apiTs = now;
  board.batch.ourTs = now;
  board.batch.items = std::move(ctx.items);

  std::unique_ptr<DepartModel> model(new DepartModel());
  model->boards.push_back(std::move(board));

  const auto& items = model->boards.front().batch.items;
  DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: parsed feedEntities=%u tripUpdates=%u matchedTrips=%u stopUpdates=%u matchedStopUpdates=%u bytes=%u items=%u\n",
               (unsigned)ctx.feedEntityCount,
               (unsigned)ctx.tripUpdateCount,
               (unsigned)ctx.tripUpdateMatched,
               (unsigned)ctx.stopUpdatesTotal,
               (unsigned)ctx.stopUpdatesMatched,
               (unsigned)ctx.bytesRead,
               (unsigned)items.size());

  return model;
}

void GtfsRtSource::reload(std::time_t now) {
  nextFetch_ = now;
  backoffMult_ = 1;
  lastBackoffLog_ = 0;
}

String GtfsRtSource::sourceKey() const {
  String k(agency_);
  k += ':';
  k += stopCode_;
  return k;
}

void GtfsRtSource::addToConfig(JsonObject& root) {
  root["Enabled"] = enabled_;
  root["Type"] = F("gtfsrt");
  root["UpdateSecs"] = updateSecs_;
  root["TemplateUrl"] = baseUrl_;
  root["ApiKey"] = apiKey_;
  String key;
  key.reserve(agency_.length() + 1 + stopCode_.length());
  key = agency_;
  key += ':';
  key += stopCode_;
  root["AgencyStopCode"] = key;
}

bool GtfsRtSource::readFromConfig(JsonObject& root, bool startup_complete, bool& invalidate_history) {
  bool ok = true;
  bool prevEnabled = enabled_;
  String prevAgency = agency_;
  String prevStop = stopCode_;
  String prevBase = baseUrl_;

  ok &= getJsonValue(root["Enabled"], enabled_, enabled_);
  ok &= getJsonValue(root["UpdateSecs"], updateSecs_, updateSecs_);
  ok &= getJsonValue(root["TemplateUrl"], baseUrl_, baseUrl_);
  ok &= getJsonValue(root["ApiKey"], apiKey_, apiKey_);

  String keyStr;
  bool haveKey = getJsonValue(root["AgencyStopCode"], keyStr, (const char*)nullptr);
  if (!haveKey) haveKey = getJsonValue(root["Key"], keyStr, (const char*)nullptr);
  if (haveKey && keyStr.length() > 0) {
    int colon = keyStr.indexOf(':');
    if (colon > 0) {
      agency_ = keyStr.substring(0, colon);
      stopCode_ = keyStr.substring(colon + 1);
    }
  } else {
    ok &= getJsonValue(root["Agency"], agency_, agency_);
    ok &= getJsonValue(root["StopCode"], stopCode_, stopCode_);
  }

  if (updateSecs_ < 10) updateSecs_ = 10;

  invalidate_history |= (agency_ != prevAgency) || (stopCode_ != prevStop) || (baseUrl_ != prevBase);

  {
    String label = F("GtfsRtSource_");
    label += agency_;
    label += '_';
    label += stopCode_;
    configKey_ = std::string(label.c_str());
  }

  if (startup_complete && !prevEnabled && enabled_) reload(departstrip::util::time_now_utc());
  return ok;
}

String GtfsRtSource::composeUrl(const String& agency, const String& stopCode) const {
  String url = baseUrl_;
  url.replace(F("{agency}"), agency);
  url.replace(F("{AGENCY}"), agency);
  url.replace(F("{stopcode}"), stopCode);
  url.replace(F("{stopCode}"), stopCode);
  url.replace(F("{STOPCODE}"), stopCode);
  if (apiKey_.length() > 0) {
    url.replace(F("{apikey}"), apiKey_);
    url.replace(F("{apiKey}"), apiKey_);
    url.replace(F("{APIKEY}"), apiKey_);
  }
  return url;
}

bool GtfsRtSource::httpBegin(const String& url, int& outLen, int& outStatus) {
  http_.setTimeout(10000);
  client_.setTimeout(10000);
  if (!http_.begin(client_, url)) {
    http_.end();
    DEBUG_PRINTLN(F("DepartStrip: GtfsRtSource::fetch: begin() failed"));
    return false;
  }
  http_.useHTTP10(true);
  http_.setUserAgent("WLED-GTFSRT/0.1");
  http_.setReuse(false);
  http_.addHeader("Connection", "close");
  http_.addHeader("Accept", "application/octet-stream", true, true);
  static const char* hdrs[] = {"Content-Type", "Content-Length", "Content-Encoding", "Transfer-Encoding"};
  http_.collectHeaders(hdrs, 4);

  int status = http_.GET();
  if (status < 200 || status >= 300) {
    if (status < 0) {
      String err = HTTPClient::errorToString(status);
      DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: HTTP error %d (%s)\n", status, err.c_str());
    } else {
      DEBUG_PRINTF("DepartStrip: GtfsRtSource::fetch: HTTP status %d\n", status);
    }
    http_.end();
    return false;
  }

  outStatus = status;
  outLen = http_.getSize();
  return true;
}
