/**
 * @file
 * @brief Functions for corpse butchering & bottling.
 **/

#pragma once

#include "maybe-bool.h"

bool devour_corpse(item_def* specific_corpse = nullptr, bool only_auto = false);
void finish_butchering(item_def& corpse);

void maybe_drop_monster_hide(const item_def &corpse);
bool turn_corpse_into_skeleton(item_def &item);
void butcher_corpse(item_def &item, bool skeleton = true);
void heal_from_devouring(const int equiv_chunks);
bool player_wants_devour_corpse(bool silent = true);
