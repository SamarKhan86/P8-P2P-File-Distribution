#include "piece_manager.h"
#include <stdlib.h>

// Initialize the piece manager
int piece_manager_init(PieceManager *manager, int piece_count)
{
    if (manager == NULL || piece_count < 0)
        return -1;

    manager->piece_count = piece_count;
    manager->pieces = NULL;

    if (piece_count == 0)
        return 0;

    manager->pieces = calloc(piece_count, sizeof(int));

    if (manager->pieces == NULL)
        return -1;

    return 0;
}

// Mark a piece as available
int mark_piece(PieceManager *manager, int index)
{
    if (manager == NULL || manager->pieces == NULL)
        return -1;

    if (index < 0 || index >= manager->piece_count)
        return -1;

    manager->pieces[index] = 1;

    return 0;
}

// Check whether a piece is available
int has_piece(PieceManager *manager, int index)
{
    if (manager == NULL || manager->pieces == NULL)
        return 0;

    if (index < 0 || index >= manager->piece_count)
        return 0;

    return manager->pieces[index];
}

// Find the first missing piece
int get_next_missing_piece(PieceManager *manager)
{
    if (manager == NULL || manager->pieces == NULL)
        return -1;

    for (int i = 0; i < manager->piece_count; i++)
    {
        if (manager->pieces[i] == 0)
            return i;
    }

    return -1;
}

// Free the piece manager
void piece_manager_destroy(PieceManager *manager)
{
    if (manager == NULL)
        return;

    free(manager->pieces);

    manager->pieces = NULL;
    manager->piece_count = 0;
}
