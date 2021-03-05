#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (!isn.has_value()) {
        // detect SYN (LISTEN)
        if (seg.header().syn) {
            isn = make_optional<WrappingInt32>(seg.header().seqno);
            absolute_ackno++;
        }
    }

    if (isn.has_value() && !stream_out().input_ended()) {
        // receive data (SYN_RECV)
        WrappingInt32 payload_seqno = seg.header().syn ? seg.header().seqno + 1 : seg.header().seqno;
        uint64_t payload_absolute_seqno = unwrap(payload_seqno, isn.value(), absolute_ackno);
        _reassembler.push_substring(
            static_cast<string>(seg.payload().str()), payload_absolute_seqno - 1, seg.header().fin);

        absolute_ackno = _reassembler.get_assembled_index() + 1;
        if (stream_out().input_ended()) {
            absolute_ackno += 1;
        }
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!isn.has_value()) {
        return {};
    } else {
        return wrap(absolute_ackno, isn.value());
    }
}

size_t TCPReceiver::window_size() const {
    if (!stream_out().input_ended()) {
        return _capacity - stream_out().buffer_size();
    } else {
        return 0;
    }
}
