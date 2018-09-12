#include "wallet_direct_io.h"
#include "p2p/connection.h"
#include <assert.h>

namespace beam {

namespace {

void append(SerializedMsg& dst, const SerializedMsg& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

class DirectPeer {
public:
    using OnDisconnected = std::function<void(const WalletID&, io::ErrorCode)>;

    using Ptr = std::shared_ptr<DirectPeer>;


    static Ptr create(const WalletID& id, OnDisconnected&& callback) {
        return std::make_shared<DirectPeer>(id, std::move(callback));
    }

    DirectPeer(const WalletID& id, OnDisconnected&& callback) :
        _id(id),
        _disconnectedCallback(std::move(callback))
    {
        assert(_disconnectedCallback);
    }

    /// called when connected
    void connected(Connection::Ptr&& conn) {
        _connection = std::move(conn);
        if (!_pendingMessages.empty()) {
            io::Result res = _connection->write_msg(_pendingMessages);
            if (res) {
                _pendingMessages.clear();
            } else {
                _connection.reset();
                _disconnectedCallback(_id, res.error());
            }
        }
    }

    /*
    void tx_finished(const TxID& tx) {
        _currentTransactions.erase(tx);
        if (_currentTransactions.empty() && _connection) {
            _connection.reset();
        }
    }
    */

    /// returns true if direct connection established
    bool send(const SerializedMsg& msg) {
        //_currentTransactions.insert(tx);
        if (_connection) {
            io::Result res = _connection->write_msg(msg);
            if (!res) {
                append(_pendingMessages, msg);
                _connection.reset();
                _disconnectedCallback(_id, res.error());
            }
        } else {
            append(_pendingMessages, msg);
            _disconnectedCallback(_id, io::EC_ENOTCONN);
        }

        // TODO
        return true;
    }

    /// returns unsent to be forwarded to BBS
    void get_pending_msg(SerializedMsg& out) {
        out.swap(_pendingMessages);
        _pendingMessages.clear();
    }

private:
    WalletID _id;
    OnDisconnected _disconnectedCallback;
    //std::set<TxID> _currentTransactions;
    SerializedMsg _pendingMessages;
    Connection::Ptr _connection;
};

} //namespace

class WalletDirectIOImpl : public WalletDirectIO, public IErrorHandler {
public:
    WalletDirectIOImpl(io::Address listenTo, Callback&& callback) :
        _listenTo(listenTo),
        _callback(std::move(callback)),
        _protocol(116, 116, 1, proto::BbsMsg::s_Code + 1, *this, 4096)
    {
        assert(_callback);
        _protocol.add_message_handler<WalletDirectIOImpl, proto::BbsMsg, &WalletDirectIOImpl::on_message>(proto::BbsMsg::s_Code, this, 32, 4096);
    }

private:
    using Connections = std::map<WalletID, DirectPeer>;

    /// associates direct connection address with peer or disables by passing empty address
    void assign_direct_peer(const WalletID& walletAddress, io::Address networkAddress) override {
        if (networkAddress.empty()) {
            _associations.erase(walletAddress);
            _knownConnections.erase(walletAddress);
        } else if (!_associations.count(walletAddress)) {
            _associations[walletAddress] = networkAddress;
            _knownConnections.insert({ walletAddress, DirectPeer::create(walletAddress, BIND_THIS_MEMFN(on_peer_disconnected))});
        }
    }

    /// tries to send msg via direct io, returns false if no direct address assigned to peer
    bool try_send_direct(const WalletID& walletAddress, proto::BbsMsg&& msg) override {
        // TODO NYI
        //return false;

        auto it = _knownConnections.find(walletAddress);
        if (it == _knownConnections.end()) {
            return false;
        }

        _protocol.serialize(_serialized, proto::BbsMsg::s_Code, msg);
        bool success = it->second->send(_serialized);
        _serialized.clear();
        return success;
    }

    void on_peer_disconnected(const WalletID& id, io::ErrorCode what) {
        // TODO
    }

    /// Handles protocol errors
    virtual void on_protocol_error(uint64_t fromStream, ProtocolError error) {}

    /// Handles network connection errors
    virtual void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) {}

    bool on_message(uint64_t from, proto::BbsMsg&& msg) {
        bool txEnded = false;
        WalletID* peer = _callback(std::move(msg), txEnded);
        if (peer) {
            io::Address peerAddress = io::Address::from_u64(from);
            if (txEnded) {
                close_connection(*peer);
                return false;
            }
            else associate_connection(peerAddress, *peer);
        }
        return true;
    }

    void close_connection(const WalletID& peer) {
        _associations.erase(peer);
        _knownConnections.erase(peer);
    }

    void associate_connection(io::Address peerAddress, const WalletID& peer) {
        if (!_associations.count(peer) && _unknownConnections.count(peerAddress)) {
            _associations[peer] = peerAddress;
            _knownConnections[peer] = std::move(_unknownConnections[peerAddress]);
            _unknownConnections.erase(peerAddress);
        }
    }

    io::Address _listenTo;
    Callback _callback;
    Protocol _protocol;
    SerializedMsg _serialized;

    std::map<WalletID, io::Address> _associations;
    std::map<io::Address, DirectPeer::Ptr> _unknownConnections;
    std::map<WalletID, DirectPeer::Ptr> _knownConnections;

};

WalletDirectIO::Ptr WalletDirectIO::create(io::Address listenTo, Callback&& callback) {
    return std::make_unique<WalletDirectIOImpl>(listenTo, std::move(callback));
}

} //namespace
