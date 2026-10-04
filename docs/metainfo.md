# Metainfo Format

The metainfo file stores information about the file and its pieces.

- Filename: Name of the file
- File Size: Size of the file in bytes
- Piece Size: Size of each piece
- Number of Pieces: Total number of pieces
- Piece Hashes: SHA-256 hash of each piece

Example:

Filename: test.txt
File Size: 5000 bytes
Piece Size: 1024 bytes
Number of Pieces: 5

Piece 0 Hash: <hash>
Piece 1 Hash: <hash>
Piece 2 Hash: <hash>
Piece 3 Hash: <hash>
Piece 4 Hash: <hash>
