#ifndef PIECE_MANAGER_H
#define PIECE_MANAGER_H

typedef struct {
    int piece_count;
    int *pieces;
} PieceManager;

int piece_manager_init(PieceManager *manager, int piece_count);
int mark_piece(PieceManager *manager, int index);
int has_piece(PieceManager *manager, int index);
int get_next_missing_piece(PieceManager *manager);
void piece_manager_destroy(PieceManager *manager);

#endif
