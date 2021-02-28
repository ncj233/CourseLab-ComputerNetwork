#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity_sd): 
    buffer(capacity_sd+1), capacity(capacity_sd), buf_len(capacity_sd+1), head(0),
    tail(0), iseof(false), b_read(0), b_written(0) {}

size_t ByteStream::write(const string &data) {
    if (iseof) {
        return 0;
    }

    size_t written = 0;
    for (const char c: data) {
        if ((tail+1)%buf_len != head) {
            buffer[tail++] = c;
            tail %= buf_len;
            written++;
        } else {
            break;
        }
    }
    b_written = b_written + written;
    return written;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string r;
    size_t read = 0;
    size_t p_head = head;

    while (p_head != tail && read < len) {
        r += buffer[p_head++];
        p_head %= buf_len;
        read++;
    }

    return r;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t size = buffer_size();
    if (len >= size) {
        b_read += size;
        head = tail;
    } else {
        b_read += len;
        head = (head + len) % buf_len;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string r = peek_output(len);
    pop_output(len);
    b_read = b_read + r.size();
    return r;
}

void ByteStream::end_input() {
    iseof = true;
}

//! 'true' if the stream input has ended
bool ByteStream::input_ended() const {
    return iseof;
}

size_t ByteStream::buffer_size() const {
    return head <= tail? tail-head: tail+buf_len-head;
}

bool ByteStream::buffer_empty() const {
    return tail == head;
}

bool ByteStream::eof() const {
    return buffer_empty() && input_ended();
}

size_t ByteStream::bytes_written() const {
    return b_written;
}

size_t ByteStream::bytes_read() const {
    return b_read;
}

size_t ByteStream::remaining_capacity() const {
    return capacity - buffer_size();
}
