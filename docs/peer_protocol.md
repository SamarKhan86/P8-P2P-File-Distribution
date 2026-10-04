Peer Wire Protocol
The Peer Wire Protocol defines how two peers communicate with each other during file sharing.

Message Format

Each message follows the format:

Message Type | Data
Message Types

1. HANDSHAKE
Purpose: Establish a connection between two peers.
Format:
HANDSHAKE | Peer_ID: <peer_id>
Example:
HANDSHAKE | Peer_ID: P001

2. HAVE
Purpose: Inform another peer that a particular piece is available.
Format:
HAVE | Piece_Index: <index>
Example:
HAVE | Piece_Index: 2

3. REQUEST
Purpose: Request a specific piece from another peer.
Format:
REQUEST | Piece_Index: <index>
Example:
REQUEST | Piece_Index: 2

4. PIECE
Purpose: Send the requested piece to another peer.
Format:
PIECE | Piece_Index: <index> | Data: <piece_data>
Example:
PIECE | Piece_Index: 2 | Data: <piece_data>

5. NOT_INTERESTED
Purpose: Inform another peer that no available piece is currently required.
Format:
NOT_INTERESTED | None
Example:
NOT_INTERESTED | None

6. CLOSE
Purpose: Close the connection between two peers.
Format:
CLOSE | Reason: <reason>
Example:
CLOSE | Reason: Transfer_Complete

7. CHOKE
Purpose: Temporarily stop a peer from requesting or receiving pieces.
Format:
CHOKE | None
Example:
CHOKE | None
This means the peer is temporarily not allowed to request pieces.

8. UNCHOKE
Purpose: Allow a previously choked peer to request and receive pieces again.
Format:
UNCHOKE | None
Example:
UNCHOKE | None
This means the peer can request pieces again.

Example Communication
Peer A                              Peer B
  |                                   |
  |--- HANDSHAKE | Peer_ID: P001 --->|
  |<-- HANDSHAKE | Peer_ID: P002 ----|
  |                                   |
  |<-- HAVE | Piece_Index: 2 ---------|
  |                                   |
  |--- REQUEST | Piece_Index: 2 ----->|
  |                                   |
  |<-- PIECE | Piece_Index: 2 --------|
  |                                   |
  |--- CLOSE | Reason: Complete ----->|
  |                                   |
