
#pragma once

#include "spl-cast.h"
#include "transformation.h"

spret cast_deaths_door(int pow, bool fail);
void remove_ice_armour();
void reset_ice_armour_duration(bool cast = false);
spret ice_armour(int pow, bool fail);

int harvest_corpses(const actor &harvester,
                    bool dry_run = false, bool defy_god = false);
spret corpse_armour(int pow, bool fail);

spret cast_regen(int pow, bool fail);
spret cast_revivification(int pow, bool fail);

spret cast_swiftness(int power, bool fail);

int cast_selective_amnesia(const string &pre_msg = "");
spret cast_silence(int pow, bool fail = false);

spret cast_infusion(int pow, bool fail);
spret cast_song_of_slaying(int pow, bool fail);

spret cast_liquefaction(int pow);
spret cast_shroud_of_golubria(int pow, bool fail);
spret cast_transform(int pow, transformation which_trans, bool fail);
