#include "stream_reassembler.hh"

#include <algorithm>
#include <stdexcept>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : buffer(capacity + 1)
    , head(0)
    , assembled_index(0)
    , head_index(0)
    , unassembled_list()
    , detect_eof(false)
    , eof_index(0)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // check if already assembled, out of capacity, or in unassembled list; already contain -> return
    if (index + data.size() >= assembled_index && index < acceptable_last_index() &&
        !substring_contains_in_unassembled_list(index, data.length())) {
        // copy substring into buffer, check eof
        size_t copy_start_index = max(index, assembled_index);
        size_t copy_end_index = min(index + data.length(), acceptable_last_index());
        for (size_t i = copy_start_index; i < copy_end_index; i++) {
            buffer[index2off(i)] = data[i - index];
        }
        if (copy_end_index == index + data.length() && eof) {
            detect_eof = true;
            eof_index = copy_end_index;
        }

        // combine with unassembled list region
        list<pair<size_t, size_t>>::iterator it = unassembled_list.begin();
        size_t start_index = copy_start_index, end_index = copy_end_index;
        while (it != unassembled_list.end()) {
            if (is_overlap(start_index, end_index, it->first, it->second)) {
                start_index = min(it->first, start_index);
                end_index = max(it->second, end_index);
                unassembled_list.erase(it++);
            } else {
                it++;
            }
        }

        // finally check if can combine into assemble data
        if (start_index == assembled_index) {
            assembled_index = end_index;
        } else {
            unassembled_list.push_back(pair<size_t, size_t>(start_index, end_index));
        }
    }

    // write to byte_stream, set eof if
    if (head_index < assembled_index) {
        size_t w_len = min(assembled_index - head_index, _output.remaining_capacity());
        _output.write(pop_string(w_len));
    }
    if (detect_eof && head_index == eof_index) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t bytes = 0;
    for (const auto &pir : unassembled_list) {
        bytes += pir.second - pir.first;
    }
    return bytes;
}

bool StreamReassembler::empty() const { return unassembled_list.empty(); }

// Implemented private functions
bool StreamReassembler::substring_contains_in_unassembled_list(const size_t index, const size_t str_len) const {
    for (const auto &pir : unassembled_list) {
        if (index >= pir.first && index + str_len <= pir.second) {
            return true;
        }
    }
    return false;
}

size_t StreamReassembler::index2off(const size_t index) const {
    if (index < head_index || index >= acceptable_last_index()) {
        throw std::runtime_error("index2off error: index out of bound");
    }
    return (index - head_index + head) % (_capacity + 1);
}

bool StreamReassembler::is_overlap(const size_t s1, const size_t e1, const size_t s2, const size_t e2) const {
    if ((s2 < s1 && e2 >= s1) || (s2 >= s1 && s2 <= e1)) {
        return true;
    } else {
        return false;
    }
}

string StreamReassembler::pop_string(const size_t length) {
    string r;
    for (size_t i = 0; i < length; i++) {
        r += buffer[head];
        head = (head + 1) % (_capacity + 1);
        head_index++;
    }
    return r;
}