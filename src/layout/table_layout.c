#include "quanton.h"

#include <math.h>
#include <stdlib.h>

#define Q_DEFAULT_BACKGROUND 0xF2F2F2FFu
#define Q_DEFAULT_BORDER 0x303030FFu
#define Q_DEFAULT_BORDER_WIDTH 1.0f

static q_box_t *q_table_create_anonymous_box(q_box_type_t type)
{
    q_box_t *box = (q_box_t *) calloc(1, sizeof(*box));
    if (box == NULL) {
        return NULL;
    }

    box->type = type;
    box->background_color = Q_DEFAULT_BACKGROUND;
    box->border_color[0] = Q_DEFAULT_BORDER;
    box->border_color[1] = Q_DEFAULT_BORDER;
    box->border_color[2] = Q_DEFAULT_BORDER;
    box->border_color[3] = Q_DEFAULT_BORDER;
    box->border_width[0] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[1] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[2] = Q_DEFAULT_BORDER_WIDTH;
    box->border_width[3] = Q_DEFAULT_BORDER_WIDTH;
    box->style_top = (float) NAN;
    box->style_right = (float) NAN;
    box->style_bottom = (float) NAN;
    box->style_left = (float) NAN;
    box->style_width = (float) NAN;
    box->style_height = (float) NAN;

    return box;
}

static void q_box_detach(q_box_t *child)
{
    if (child == NULL) {
        return;
    }

    child->parent = NULL;
    child->next_sibling = NULL;
    child->prev_sibling = NULL;
}

static void q_box_append_child(q_box_t *parent, q_box_t *child)
{
    if (parent == NULL || child == NULL) {
        return;
    }

    child->parent = parent;
    child->prev_sibling = parent->last_child;
    child->next_sibling = NULL;
    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

static void q_table_fixup_table_box(q_box_t *table_box)
{
    q_box_t *child;
    q_box_t *next;
    q_box_t *anon_section = NULL;
    q_box_t *anon_row = NULL;

    if (table_box == NULL) {
        return;
    }

    child = table_box->first_child;
    table_box->first_child = NULL;
    table_box->last_child = NULL;

    while (child != NULL) {
        next = child->next_sibling;
        q_box_detach(child);

        switch (child->type) {
            case Q_BOX_TABLE_SECTION:
                q_box_append_child(table_box, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            case Q_BOX_TABLE_ROW:
                if (anon_section == NULL) {
                    anon_section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                    if (anon_section == NULL) {
                        q_box_append_child(table_box, child);
                        anon_row = NULL;
                        break;
                    }
                    q_box_append_child(table_box, anon_section);
                }
                q_box_append_child(anon_section, child);
                anon_row = child;
                break;
            case Q_BOX_TABLE_CELL:
                if (anon_section == NULL) {
                    anon_section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                    if (anon_section == NULL) {
                        q_box_append_child(table_box, child);
                        anon_row = NULL;
                        break;
                    }
                    q_box_append_child(table_box, anon_section);
                }
                if (anon_row == NULL) {
                    anon_row = q_table_create_anonymous_box(Q_BOX_TABLE_ROW);
                    if (anon_row == NULL) {
                        q_box_append_child(table_box, child);
                        break;
                    }
                    q_box_append_child(anon_section, anon_row);
                }
                q_box_append_child(anon_row, child);
                break;
            case Q_BOX_TABLE_CAPTION:
                q_box_append_child(table_box, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            default:
            {
                q_box_t *section = q_table_create_anonymous_box(Q_BOX_TABLE_SECTION);
                q_box_t *row = q_table_create_anonymous_box(Q_BOX_TABLE_ROW);
                q_box_t *cell = q_table_create_anonymous_box(Q_BOX_TABLE_CELL);
                if (section == NULL || row == NULL || cell == NULL) {
                    free(section);
                    free(row);
                    free(cell);
                    q_box_append_child(table_box, child);
                    anon_section = NULL;
                    anon_row = NULL;
                    break;
                }
                q_box_append_child(table_box, section);
                q_box_append_child(section, row);
                q_box_append_child(row, cell);
                q_box_append_child(cell, child);
                anon_section = NULL;
                anon_row = NULL;
                break;
            }
        }

        child = next;
    }
}

void q_table_fixup_anonymous(q_box_t *root)
{
    q_box_t *child;

    if (root == NULL) {
        return;
    }

    if (root->type == Q_BOX_TABLE) {
        q_table_fixup_table_box(root);
    }

    for (child = root->first_child; child != NULL; child = child->next_sibling) {
        q_table_fixup_anonymous(child);
    }
}
