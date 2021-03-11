#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>
#include <iostream>

class RetransTimer {
  private:
    const uint32_t _initial_retransmission_timout;
    uint32_t _retrans_timeout;  // RTO
    uint32_t _retrans_count;    // count of "consecutive retransmissions"

    bool _running;
    uint32_t _time_left;
    bool _need_back_off_rto;

  public:
    RetransTimer(const uint32_t initial_timeout)
        : _initial_retransmission_timout(initial_timeout)
        , _retrans_timeout(0)
        , _retrans_count(0)
        , _running(false)
        , _time_left(0)
        , _need_back_off_rto(true) {
          std::cout << initial_timeout << std::endl;
        }

    void init(bool need_back_off_rto = true) {
        _retrans_timeout = _initial_retransmission_timout;
        _retrans_count = 0;
        _running = true;
        _time_left = _retrans_timeout;
        _need_back_off_rto = need_back_off_rto;
    }

    void consume(const uint32_t time) {
        if (_running) {
            _time_left = _time_left > time ? _time_left - time : 0;
        }
    }

    void restart() {
        if (_running) {
            _retrans_count++;
            if (_need_back_off_rto) _retrans_timeout *= 2;
            _time_left = _retrans_timeout;
        }
    }

    void close() { _running = false; }

    bool is_alarm() const { return _running && !_time_left; }
    bool is_running() const { return _running; }
    uint32_t get_retransmission_count() const { return _retrans_count; }
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    // my private variables
    RetransTimer _timer;
    std::queue<TCPSegment> _outstanding_segments{};
    uint64_t _last_ackno;
    uint64_t _last_windowsize;
    bool _FIN_setted;

    // my private functions
    void send_tcpsegment(const TCPSegment &segment, bool need_back_off_rto = true);
    void resend_tcpsegment();

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
