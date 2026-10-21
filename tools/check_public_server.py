"""Read-only RPLY availability probe; never queues a match or sends a join."""
import argparse
import json
import secrets
import socket
import struct


def probe(host, port, timeout=4):
    target = socket.gethostbyname(host), port
    nonce = secrets.randbits(64) or 1
    request = b"RPLY\x02\x01" + struct.pack("<QQ", nonce, 0)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.connect(target)
        sock.send(request)
        reply = sock.recv(512)
    if len(reply) < 30 or reply[:6] != b"RPLY\x02\x02" or struct.unpack_from("<Q", reply, 6)[0] != nonce:
        raise ValueError("Not a matching Replay session-discovery reply")
    state, playlist = reply[22:24]
    if state > 3 or len(reply) != (138 if state in (1, 2) else 30):
        raise ValueError("Invalid session-discovery state/length")
    result = {"server": f"{target[0]}:{port}", "state": ["unavailable", "party", "dedicated", "idle"][state],
              "playlist": playlist}
    if state in (1, 2):
        session = reply[30:]
        # Verified TU19 CommonAddr serializer20ECA10: five local6-byte endpoints
        # then public endpoint. Never print the remaining security ID/key bytes.
        result["local_game_endpoints"] = [f"{socket.inet_ntoa(session[i:i+4])}:{struct.unpack_from('<H', session, i+4)[0]}"
                                          for i in range(0, 30, 6) if struct.unpack_from('<H', session, i+4)[0]]
        result["public_game_endpoint"] = f"{socket.inet_ntoa(session[30:34])}:{struct.unpack_from('<H', session, 34)[0]}"
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=27126)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    try:
        print(json.dumps(probe(args.host, args.port), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"No valid reply: {error}\n")
