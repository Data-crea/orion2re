#pragma once
// TCP server internals for the extension API.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include <atomic>

namespace ext {

// Wire protocol constants
constexpr uint32_t MAGIC = 0x4F325845; // "O2XE"
constexpr uint16_t PROTO_VERSION = 1;

// Server → Client message types
constexpr uint16_t MSG_HELLO_REPLY   = 0x01;
constexpr uint16_t MSG_STATE         = 0x10;
constexpr uint16_t MSG_FIELDS        = 0x11;
constexpr uint16_t MSG_VISUAL        = 0x12;
constexpr uint16_t MSG_EVENT         = 0x13;
// OrionLayer: the GAME popup's ten save slots, sent right after a
// field list while the Load or Save dialog is up. Payload: uint8
// screen_data, uint8 count, then count x (int8 status, int8 game type,
// char[37] description, char[25] stardate, char[25] date).
// See doc/ext_save_slots.patch.
constexpr uint16_t MSG_SAVE_SLOTS    = 0x14;
// OrionLayer, open fix 93. The records the wire carries as raw bytes, member by member: name, offset,
// size (src/ext/ext_layout.h, docs/extension-api.md). Sent once, right after
// HELLO_REPLY, when HELLO_REPLY's capabilities carry CAP_LAYOUT.
constexpr uint16_t MSG_LAYOUT        = 0x15;

// HELLO_REPLY's second word: what this server sends beyond protocol 1.
constexpr uint16_t CAP_LAYOUT        = 0x0001;

// Client → Server message types
constexpr uint16_t MSG_HELLO         = 0x01;
constexpr uint16_t MSG_ACTIVATE      = 0x80;
constexpr uint16_t MSG_INJECT_KEY    = 0x81;
constexpr uint16_t MSG_INJECT_CLICK  = 0x82;
constexpr uint16_t MSG_CANCEL_FIELD  = 0x83;
// OrionLayer: set the job of one or more pops in ONE colony.
// Payload: int16 colony_idx, uint8 count, then count pairs of
// (uint8 pop_idx, uint8 new_job). See doc/ext_move_pop.patch.
constexpr uint16_t MSG_SET_JOBS      = 0x84;
// OrionLayer, open fix 21: select or deselect ONE ship in the open
// fleet box. int16 ship_idx, uint8 selected (0 or 1). Applied only if
// every precondition holds; a refused command writes nothing.
constexpr uint16_t MSG_SELECT_SHIP   = 0x85;
// OrionLayer, open fix 43. Show (1) or hide (0) the engine's own window: uint8 show.
constexpr uint16_t MSG_SHOW_WINDOW   = 0x86;
// OrionLayer, open fix 61. A RIGHT click at (x, y) in 640x480 space: int16 x, int16 y.
constexpr uint16_t MSG_INJECT_RIGHT_CLICK = 0x87;
// OrionLayer, open fix 58. One battle action: uint16 serial, int16 unit, uint8 op, int16 a, int16 b, int16 c.
constexpr uint16_t MSG_COMBAT_COMMAND = 0x88;
// OrionLayer, open fix 64. The Races screen's spies: uint8 n, n x (uint8 player, uint8 spies, uint8 mission 1-3 or 0 kept), uint8 agent pool.
constexpr uint16_t MSG_SET_SPIES      = 0x89;
// OrionLayer, open fix 89. uint16: a key code as platform.cpp translates F1-F10 (0x3B00-0x4400), Alt+F1-F10 (0x6800-0x7100) and Alt+letter (scan << 8, 0x1000-0x3200); anything else is dropped.
constexpr uint16_t MSG_INJECT_RAW_KEY = 0x8A;
// OrionLayer, open fix 92. Colonists of the colony screen's colony to another colony at the same star: int16 source colony, int16 destination colony, uint8 n, n x uint8 pop index; all or nothing.
constexpr uint16_t MSG_SETTLE_POPS = 0x8B;

// Subscription flags (bitmask)
constexpr uint16_t SUB_STATE         = 0x01;
constexpr uint16_t SUB_FIELDS        = 0x02;
constexpr uint16_t SUB_VISUAL        = 0x04;
constexpr uint16_t SUB_EVENTS        = 0x08;

// Event flags (bitmask in event message)
constexpr uint16_t EVT_SCREEN_CHANGED = 0x01;
constexpr uint16_t EVT_TURN_ENDED     = 0x02;
constexpr uint16_t EVT_GAME_STARTED   = 0x04;
constexpr uint16_t EVT_GAME_ENDED     = 0x08;

// Input command from a client
struct InputCommand {
    uint16_t type;     // MSG_ACTIVATE, MSG_INJECT_KEY, etc.
    int16_t  param1;   // field_id or keysym or x or colony_idx
    int16_t  param2;   // 0 or y or pop count
    // MSG_SET_JOBS only: count x (pop_idx, new_job) byte pairs.
    // Empty for every other command, which keeps all of those on
    // the two fixed fields exactly as before.
    std::vector<uint8_t> pairs;
};

// Per-client state
struct Client {
    int fd;
    uint16_t subscriptions;
    uint32_t sequence;
    bool alive;
};

// Frame header on the wire
struct FrameHeader {
    uint32_t magic;
    uint32_t length;
};

// Message header on the wire
struct MsgHeader {
    uint16_t msg_type;
    uint16_t flags;
    uint32_t sequence;
};

class Server {
public:
    Server();
    ~Server();

    bool Start(uint16_t port);
    void Stop();

    /// Accept new connections (non-blocking).
    void AcceptClients();

    /// Read input commands from all clients (non-blocking).
    void ReadInput();

    /// Send a framed message to all clients with matching subscription.
    void SendToSubscribers(uint16_t sub_flag, uint16_t msg_type,
                           const uint8_t* data, size_t len);

    /// Pop next input command. Returns false if queue is empty.
    bool PopInput(InputCommand& cmd);

    /// Number of connected clients.
    int ClientCount() const;

    /// True once after a client completed HELLO: the caller must
    /// resend the full FIELD_LIST so the new client does not wait
    /// for the next field-count change. Cleared by the call.
    bool TakeFieldsDirty() { return fields_dirty_.exchange(false); }

private:
    int listen_fd_;
    std::vector<Client> clients_;
    std::vector<InputCommand> input_queue_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_;
    std::atomic<bool> fields_dirty_{false};

    bool SendFrame(int fd, uint16_t msg_type, uint16_t flags,
                   uint32_t seq, const uint8_t* data, size_t len);
    bool RecvExact(int fd, void* buf, size_t len);
    void HandleClientMessage(Client& client);
    void RemoveDeadClients();
    void SetNonBlocking(int fd);
};

} // namespace ext
