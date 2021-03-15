#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!active()) {
        return;
    }

    // need to check for rst fisrt
    if (seg.header().rst) {
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        return;
    }

    // if haven't received syn (bug founded)
    if (!_receiver.ackno().has_value() && !seg.header().syn) {
        return;
    }

    _time_since_last_segment_received = 0;

    _receiver.segment_received(seg);
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    // check if the other side: fin acked --> no need to linger
    if (_receiver.stream_out().input_ended() && !_sender.fin_setted()) {
        _linger_after_streams_finish = false;
    }
    // old is wrong: if (seg.length_in_sequence_space() > 0) {
    _sender.fill_window();  // new ack means have extra windows size, need to fill window again
    if (seg.length_in_sequence_space() > 0 && _sender.segments_out().empty()) {
        _sender.send_empty_segment();
    }
    _flush_segments_out();
}

bool TCPConnection::active() const {
    // RST
    if (_receiver.stream_out().error() || _sender.stream_in().error()) {
        return false;
    }
    // both FIN
    if (_stream_finish() &&
        (!_linger_after_streams_finish || (_linger_after_streams_finish && _linger_time >= 10 * _cfg.rt_timeout))) {
        return false;
    }
    return true;
}

size_t TCPConnection::write(const string &data) {
    if (!active()) {
        return 0;
    }

    size_t len = _sender.stream_in().write(data);
    if (len > 0) {
        _sender.fill_window();
        _flush_segments_out();
    }

    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!active()) {
        return;
    }

    _time_since_last_segment_received += ms_since_last_tick;
    // 1. tell the TCPSender about the passage of time;
    _sender.tick(ms_since_last_tick);
    // 2. if retransmission more than MAX_RETX_ATTEMPS, send RST segment;
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _rst();
        return;
    }
    // flush segments need after MAX_RETX_ATTEMPS check
    _flush_segments_out();
    // 3. end the connection cleany;
    if (_stream_finish() && _linger_after_streams_finish) {
        _linger_time += ms_since_last_tick;
    }
}

void TCPConnection::end_input_stream() {
    // if (_receiver.stream_out().input_ended()) {
    //     _linger_after_streams_finish = false;
    // }
    _sender.stream_in().end_input();
    _sender.fill_window();
    _flush_segments_out();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _flush_segments_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _rst();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

// my private functions
void TCPConnection::_flush_segments_out() {
    while (!_sender.segments_out().empty()) {
        TCPSegment &seg = _sender.segments_out().front();
        _sender.segments_out().pop();

        // set ACK (from receiver)
        if (_receiver.ackno().has_value()) {
            seg.header().ack = 1;
            seg.header().ackno = _receiver.ackno().value();
        }
        // set window size
        seg.header().win = _receiver.window_size() > 0xffff ? static_cast<uint16_t>(0xffff)
                                                            : static_cast<uint16_t>(_receiver.window_size());

        _segments_out.push(seg);
    }
}

void TCPConnection::_rst() {
    // if send rst, disguard other segment ...
    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }
    // send rst
    _sender.send_rst_segment();
    _flush_segments_out();
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
}