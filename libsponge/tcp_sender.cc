#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timer(retx_timeout)
    , _last_ackno(0)
    , _last_windowsize(1)
    , _FIN_setted(false) {}

uint64_t TCPSender::bytes_in_flight() const {
    // cout << "seq_no:" << _next_seqno << ", ack_no:" << abs_ackno << endl;
    return _next_seqno - _last_ackno;
}

void TCPSender::fill_window() {
    bool ahead = _last_ackno + _last_windowsize < _next_seqno;
    uint64_t window_left = ahead? 0: _last_ackno + _last_windowsize - _next_seqno;

    // _last_windowsize == 0?
    if ((_stream.buffer_size() > 0 || (!_FIN_setted && _stream.eof())) && _last_windowsize == 0 && !ahead) {
        TCPSegment segment;
        // TODO: only send once here?
        segment.header().seqno = next_seqno();
        if (_stream.buffer_size() > 0) {
            segment.payload() = Buffer(_stream.read(1));
        } else {
            segment.header().fin = 1;
            _FIN_setted = 1;
        }
        _next_seqno++;
        send_tcpsegment(segment, false);
        return;
    }

    // continue send segment, until window=0 or output stream empty
    while (1) {
        TCPSegment segment;

        // store current seqno into header, may change later
        segment.header().seqno = next_seqno();

        // step 1: check SYN
        if (_next_seqno == 0) {
            segment.header().syn = 1;
            _next_seqno++;
            window_left--;
        }

        // step 2.a: calculate payload_size
        // it should be min of buffer available, window_left, and MAX_PAYLOAD_SIZE(1452 Bytes)
        uint64_t payload_size = min(_stream.buffer_size(), window_left);
        payload_size = min(payload_size, TCPConfig::MAX_PAYLOAD_SIZE);

        // step 2.b: read payload
        if (payload_size > 0) {
            segment.payload() = Buffer(_stream.read(payload_size));
            _next_seqno += payload_size;
            window_left -= payload_size;
        }

        // step 3: check FIN
        // check window size > 0
        // cout << "seqno_limit: " << seqno_limit << ", _next_seqno: " << _next_seqno << endl;
        if (window_left && !_FIN_setted && _stream.eof()) {
            segment.header().fin = 1;
            _next_seqno++;
            _FIN_setted = true;
            window_left--;
        }

        // send segment
        if (segment.header().syn || segment.header().fin || payload_size > 0) {
            send_tcpsegment(segment);
        } else {
            break;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // check if the ackno is the newest
    uint64_t recv_ackno = unwrap(ackno, _isn, _next_seqno);
    if (recv_ackno < _last_ackno) {
        return;
    } else if (recv_ackno == _last_ackno) {
        _last_windowsize = max(static_cast<uint64_t>(window_size), _last_windowsize);
        return;
    }

    _last_ackno = recv_ackno;
    _last_windowsize = window_size;

    // receiver sucessful receipt of new data
    while (!_outstanding_segments.empty()) {
        const TCPSegment &top_segment = _outstanding_segments.front();
        uint64_t seqno = unwrap(top_segment.header().seqno, _isn, _next_seqno);
        if (seqno + top_segment.length_in_sequence_space() <= recv_ackno) {
            _outstanding_segments.pop();
        } else {
            break;
        }
    }

    // set timer
    if (_outstanding_segments.empty()) {
        _timer.close();
    } else {
        _timer.init();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_timer.is_running())
        return;

    _timer.consume(ms_since_last_tick);
    if (_timer.is_alarm()) {
        resend_tcpsegment();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _timer.get_retransmission_count(); }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}

// implementation private functions
void TCPSender::send_tcpsegment(const TCPSegment &segment, bool need_back_off_rto) {
    _segments_out.push(segment);
    _outstanding_segments.push(segment);
    if (!_timer.is_running()) {
        _timer.init(need_back_off_rto);
    }
}

void TCPSender::resend_tcpsegment() {
    const TCPSegment &top_segment = _outstanding_segments.front();
    _segments_out.push(top_segment);
    _timer.restart();
}
