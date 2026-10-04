Component Interfaces
The project is divided into separate components. Each component performs a specific task and communicates with other components through defined interfaces.

1. Metainfo Manager
Purpose: Handles creation and reading of the metainfo file.
Functions:
createMetainfo(file)
readMetainfo(metainfo)
getPieceHash(pieceIndex)

Example:
createMetainfo("test.txt")
readMetainfo("test.meta")
getPieceHash(2)

3. File Manager
Purpose: Handles splitting the original file into pieces and combining pieces back into the original file.
Functions:
splitFile(file, pieceSize)
combinePieces(pieces)

Example:
splitFile("test.txt", 1024)
combinePieces(pieces)

4. Peer Manager
Purpose: Handles connections between peers.
Functions:
connectToPeer(peerAddress)
acceptPeer()
disconnectPeer(peer)

Example:
connectToPeer("192.168.1.10:5000")
acceptPeer()
disconnectPeer(peer)

5. Piece Manager
Purpose: Handles requesting, receiving, storing, and verifying file pieces.
Functions:
requestPiece(peer, pieceIndex)
receivePiece(piece)
storePiece(piece)
verifyPiece(piece)

Example:
requestPiece(peer, 2)
receivePiece(piece)
storePiece(piece)
verifyPiece(piece)
