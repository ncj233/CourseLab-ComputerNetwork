#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address), _arp_map(), _time_stamp_ms(0) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    std::optional<EthernetAddress> next_hop_mac = _arp_query(next_hop_ip);
    if (next_hop_mac.has_value()) {
        _send_ethernet_frame(next_hop_mac.value(), _ethernet_address, EthernetHeader::TYPE_IPv4, dgram.serialize());
    } else {
        bool in_waiting_list = false;
        for (ARPWaitingFrames &arp_waiting_frames : _arp_waiting_frames_list) {
            if (arp_waiting_frames.ip == next_hop_ip) {
                arp_waiting_frames.datagrams.push_back(dgram);
                in_waiting_list = true;
                break;
            }
        }
        if (in_waiting_list == false) {
            ARPWaitingFrames waiting_frames(next_hop_ip, _time_stamp_ms);
            waiting_frames.datagrams.push_back(dgram);
            _arp_waiting_frames_list.push_back(waiting_frames);

            _broadcast_arp_request(next_hop_ip);
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const EthernetHeader &header = frame.header();

    if (header.type == EthernetHeader::TYPE_IPv4 && header.dst == _ethernet_address) {
        InternetDatagram dgram;
        ParseResult rst = dgram.parse(frame.payload());
        if (rst == ParseResult::NoError) {
            return dgram;
        }
    } else if (header.type == EthernetHeader::TYPE_ARP &&
               (header.dst == ETHERNET_BROADCAST || header.dst == _ethernet_address)) {
        ARPMessage arp;
        ParseResult rst = arp.parse(frame.payload());
        if (rst == ParseResult::NoError) {
            _arp_update(arp.sender_ip_address, arp.sender_ethernet_address);
            if (arp.opcode == ARPMessage::OPCODE_REQUEST && arp.target_ip_address == _ip_address.ipv4_numeric()) {
                _arp_response(arp.sender_ip_address, arp.sender_ethernet_address);
            }
        }
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _time_stamp_ms = _time_stamp_ms + ms_since_last_tick;
    for (ARPWaitingFrames &arp_waiting_frames : _arp_waiting_frames_list) {
        if (arp_waiting_frames.time_stamp + _ARP_REQUEST_RESNED < _time_stamp_ms) {
            uint32_t ip = arp_waiting_frames.ip;
            _broadcast_arp_request(ip);
            arp_waiting_frames.time_stamp = _time_stamp_ms;
        }
    }
}

// my private functions
void NetworkInterface::_arp_update(const uint32_t ip, const EthernetAddress &mac) {
    // update arp mapping
    bool ip_in_mapping = false;
    for (ARPMapping &item : _arp_map) {
        if (item.ip == ip) {
            ip_in_mapping = true;
            item.mac = mac;
            item.time_stamp = _time_stamp_ms;
            break;
        }
    }

    if (!ip_in_mapping) {
        _arp_map.push_back(ARPMapping(ip, mac, _time_stamp_ms));
    }

    // check if ip in arp waiting frames
    list<ARPWaitingFrames>::iterator it = _arp_waiting_frames_list.begin();
    while (it != _arp_waiting_frames_list.end()) {
        auto p = it;
        it++;
        if (p->ip == ip) {
            for (const auto &datagram : p->datagrams) {
                _send_ethernet_frame(mac, _ethernet_address, EthernetHeader::TYPE_IPv4, datagram.serialize());
            }
            _arp_waiting_frames_list.erase(p);
        }
    }
}

std::optional<EthernetAddress> NetworkInterface::_arp_query(const uint32_t ip) const {
    for (const ARPMapping &item : _arp_map) {
        if (item.ip == ip) {
            if (item.time_stamp + _ARP_EXPIRE_TIME >= _time_stamp_ms) {
                return item.mac;
            } else {
                break;
            }
        }
    }
    return {};
}

void NetworkInterface::_send_ethernet_frame(const EthernetAddress &dst,
                                            const EthernetAddress &src,
                                            const uint16_t type,
                                            const BufferList &payload) {
    EthernetFrame frame;
    frame.header() = {dst, src, type};
    frame.payload() = payload;
    _frames_out.push(frame);
}

void NetworkInterface::_broadcast_arp_request(const uint32_t ip) {
    ARPMessage arpmessage;
    arpmessage.opcode = ARPMessage::OPCODE_REQUEST;
    arpmessage.sender_ethernet_address = _ethernet_address;
    arpmessage.sender_ip_address = _ip_address.ipv4_numeric();
    // arpmessage.target_ethernet_address(0);
    arpmessage.target_ip_address = ip;
    _send_ethernet_frame(
        ETHERNET_BROADCAST, _ethernet_address, EthernetHeader::TYPE_ARP, BufferList(arpmessage.serialize()));
}

void NetworkInterface::_arp_response(const uint32_t response_ip, const EthernetAddress &response_mac) {
    ARPMessage arpmessage;
    arpmessage.opcode = ARPMessage::OPCODE_REPLY;
    arpmessage.sender_ethernet_address = _ethernet_address;
    arpmessage.sender_ip_address = _ip_address.ipv4_numeric();
    arpmessage.target_ethernet_address = response_mac;
    arpmessage.target_ip_address = response_ip;
    _send_ethernet_frame(response_mac, _ethernet_address, EthernetHeader::TYPE_ARP, BufferList(arpmessage.serialize()));
}
